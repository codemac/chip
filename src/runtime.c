#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include <chip/runtime.h>

/* run-of-the-mill gcc grossness */
#define unlikely(expr) __builtin_expect(!!(expr), 0)

/* try not to consume any stack space with runtime assertions */
#define runtime_assert_msg(expr, str) if (unlikely(!(expr))) \
	{ __panicstr(str, sizeof(str)-1); }

#define panic(str) __panicstr(str"\n", sizeof(str))

#define BUG_ON(expr) if (unlikely(expr)) panic(#expr)

__attribute__((cold))
__attribute__((noreturn))
static void  __panicstr(const char *msg, size_t len) {
	write(STDERR_FILENO, msg, len);
	raise(SIGABRT);
	_exit(1); /* we probably won't get here. */
}

/* --- OS-specific declarations --- */

static void poll(int ms);
static void pollinit(void);


#include "runtime_poller.h"

/* --- architecture-specific declarations --- */

typedef struct regctx_s regctx_t;

static inline word_t get_arg0(char *stack);
static inline void setup(regctx_t *ctx, char *stack, void (*retpc)(void), word_t arg0);
static void _swapctx(regctx_t *save, const regctx_t *load);

__attribute__((noreturn))
static void _loadctx(const regctx_t *load);

#include "runtime_arch.h"

/* possible task statuses */
enum {
	STATUS_EMPTY,    /* uninitialized */
	STATUS_RUNNABLE, /* able to run (use swapctx()) */
	STATUS_RUNNING,  /* running now */
	STATUS_PARKED,   /* was running; waiting for event */
	STATUS_IOWAIT,   /* waiting for poller */
};

typedef struct arena_s arena_t;

struct task_s {
	task_t 	   *next;
	int        status; /* STATUS_XXX */
	int        index;  /* index in arena */
	regctx_t   ctx;    /* saved register state, if not running */
	void       (*start)(word_t); 
	char       *stack;
	arena_t    *arena;
};

/* the global run queue/state */
static struct{
	task_t     *running;
	tasklist_t queue;    /* runnable */
	int        parked;   /* # of parked tasks */
	int        iowait;   /* # of tasks waiting for i/o */
	tasklist_t begin;    /* blocking requests to newtask() */
	task_t     t0;       /* the root task (taskmain()) */
	uintptr_t  t0_magic; /* t0->stack points here */
} runq;

#define STACK_SIZE 12288 /* three pages */
#define ARENA_TASKS (sizeof(uintptr_t)*8)
#define ARENA_STACK_MAPPING (ARENA_TASKS*STACK_SIZE)
/* all the stacks, plus the arena structure itself */
#define ARENA_MAPPING (ARENA_STACK_MAPPING+sizeof(arena_t))

struct arena_s {
	arena_t   *next;
	arena_t   *prev;
	uintptr_t bits;
	task_t    tasks[ARENA_TASKS];
};

/*
   mmap() a new arena.

   The arena struct itself occupies the memory beyond
   all of the stacks.
   +------------------------------- ... ----------+
   |  stack  |  stack  |  stack  |      | arena   |
   +------------------------------- ... ----------+
 */
static arena_t *map_arena(void) {
	char *mem;

do_map_arena:
	mem = mmap(NULL, ARENA_MAPPING, PROT_READ|PROT_WRITE,
			 MAP_PRIVATE|MAP_ANON, -1, 0);
	if (unlikely(mem == MAP_FAILED)) {
		if (errno == EINTR)
			goto do_map_arena;
		
		return NULL;
	}

	arena_t *out = (arena_t *)(mem + ARENA_STACK_MAPPING);

	for (int i=0; i<ARENA_TASKS; ++i) {
		char *bottom = mem + (i * STACK_SIZE);
		out->tasks[i].stack = bottom + STACK_SIZE;
		out->tasks[i].arena = out;
		out->tasks[i].index = i;
	}
	return out;
}

/*
   Rather than un-mapping the memory, we can tell the
   kernel that the memory no longer needs to remain
   valid (e.g. it can be unmapped and then zero-filled
   if it is faulted back in.)
 */
static void soft_offline_arena(arena_t *arena) {
	char *top = (char *)arena;
	char *base = top - ARENA_STACK_MAPPING;
	int flags;

	/*
	   MADV_FREE on BSD has more-or-less the same
	   semantics as MADV_DONTNEED on linux (for 
	   private anonymous mappings.) It's fine if the
	   kernel zero-fills these pages.
	 */
#ifdef MADV_FREE
	flags = MADV_FREE;
#else
	flags = MADV_DONTNEED;
#endif

	/* 
	   We only offline the stack pages; we keep the
	   arena page(s) because they contain the pointers
	   necessary to traverse the heap (we can't zero-fill them.)
	 */
	madvise(base, ARENA_STACK_MAPPING, flags);
}

/* get task or abort */
static task_t *arena_get_task(arena_t *arena) {
	uintptr_t v = ~(arena->bits);
	BUG_ON(v == 0);
	int index = __builtin_ffsl(v)-1;
	task_t *out = &arena->tasks[index];

	BUG_ON(out->status != STATUS_EMPTY);
	BUG_ON(out->index != index);

	/* set index bit  */
	arena->bits |= ((uintptr_t)1<<index);
	return out;
}

static void arena_put_task(task_t *task) {
	arena_t *arena = task->arena;
	uintptr_t old = arena->bits;
	arena->bits &= ~((uintptr_t)1<<(task->index));
	/* catch double-free */
	BUG_ON(arena->bits == old);
}

/* The task heap. */
static struct {
	arena_t *empty;
	arena_t *partial;
	arena_t *full;
	int     alloc;
} theap;

static int arena_is_full(arena_t *arena) {
	return (arena->bits == ~((uintptr_t)0));
}

static int arena_is_empty(arena_t *arena) {
	return (arena->bits == ((uintptr_t)0));
}

/*
   Each stack/task has a magic number that occupies
   the top word. If we find a stack without this, then
   a badly-behaved program clobbered it.
 */
static inline uintptr_t stack_magic(task_t *task) {
	return ((uintptr_t)task) ^ (((uintptr_t)task->stack)>>12);
}

static inline void push_magic(task_t *task) {
	uintptr_t magic = stack_magic(task);
	*(uintptr_t *)(task->stack - sizeof(uintptr_t)) = magic;
}

static task_t *new_task(void) {
	task_t *out;
	
	if (theap.partial) {
		out = arena_get_task(theap.partial);
		if (arena_is_full(theap.partial)) {
			arena_t *moving = theap.partial;
			theap.partial = moving->next;

			if (theap.partial)
				theap.partial->prev = NULL;

			moving->next = theap.full;
			if (theap.full)
				theap.full->prev = moving;

			theap.full = moving;
		}
		
	} else {
		arena_t *moving;
		if (theap.empty) {
			moving = theap.empty;
			theap.empty = moving->next;
			theap.empty->prev = NULL;
		} else {
			moving = map_arena();
			if (moving == NULL)
				return NULL;

			theap.alloc += ARENA_TASKS;
		}

		moving->next = theap.partial;
		if (theap.partial)
			theap.partial->prev = moving;

		theap.partial = moving;
		out = arena_get_task(moving);
	}

	/* may as well fault the stack now */
	push_magic(out);
	return out;
}

/* unlink this arena from its current location in the heap */
static void arena_unlink(arena_t **head, arena_t *arena) {
	if (*head == arena) {
		*head = arena->next;
		if (arena->next)
			arena->next->prev = NULL;
		
	} else {
		if (arena->prev)
			arena->prev->next = arena->next;
		
		if (arena->next)
			arena->next->prev = arena->prev;
		
	}
	arena->next = NULL;
	arena->prev = NULL;
}

/* release a task back to the heap */
static void free_task(task_t *task) {
	BUG_ON(task->status != STATUS_EMPTY);
	arena_t *arena = task->arena;
	int was_full = arena_is_full(arena);
	arena_put_task(task);
	if (was_full) {
		arena_unlink(&theap.full, arena);
		arena->next = theap.partial;
		if (theap.partial)
			theap.partial->prev = arena;

		theap.partial = arena;
	} else if (arena_is_empty(arena)) {
		arena_unlink(&theap.partial, arena);
		arena_t *old = theap.empty;
		theap.empty = arena;
	        if (old) {
			/* 
			   We only keep one 'empty'
			   arena that isn't soft-offlined.
			*/
			theap.empty->next = old;
			old->prev = theap.empty;
			soft_offline_arena(old);
		}
	}
}

__attribute__((noreturn))
static void _sbrt_exit(void);

static void add_stats_from(arena_t *arena, tsk_stats_t *stats) {
	int running = 0;
	for (arena_t *a = arena; a != NULL; a = a->next) {
		for (int i=0; i<ARENA_TASKS; ++i) {
			switch (a->tasks[i].status) {
			case STATUS_EMPTY:
				stats->free++;
				break;
			case STATUS_IOWAIT:
				stats->iowait++;
				break;
			case STATUS_PARKED:
				stats->parked++;
				break;
			case STATUS_RUNNABLE:
				stats->runnable++;
				break;
			case STATUS_RUNNING:
				BUG_ON(running != 0);
				running = 1;
				break;
			default:
				panic("unknown task status");
			}
		}
	}
}

void get_tsk_stats(tsk_stats_t *stats) {
	stats->free = 0;
	stats->parked = 0;
	stats->runnable = 0;
	stats->iowait = 0;
	switch (runq.t0.status) {
	default:
		panic("bad t0 status");
	case STATUS_IOWAIT:
		stats->iowait++;
		break;
	case STATUS_PARKED:
		stats->parked++;
		break;
	case STATUS_RUNNABLE:
		stats->runnable++;
		break;
	case STATUS_RUNNING:
		break;
	}
	
	add_stats_from(theap.full, stats);
	add_stats_from(theap.partial, stats);
	add_stats_from(theap.empty, stats);

	BUG_ON(stats->parked != runq.parked);
	BUG_ON(stats->iowait != runq.iowait);
}

static task_t *list_pop(tasklist_t *tl) {
	if (tl->top == NULL)
		return NULL;

	task_t *out = tl->top;
	tl->top = out->next;
	if (tl->top == NULL) {
		BUG_ON(out != tl->tail);
		tl->tail = NULL;
	}
	out->next = NULL;
	return out;
}

static void list_pushback(tasklist_t *tl, task_t *task) {
	if (tl->top == NULL) {
		BUG_ON(tl->tail != NULL);
		tl->top = task;
		tl->tail = task;
		return;
	}
	BUG_ON(tl->tail == NULL);
	tl->tail->next = task;
	tl->tail = task;
	task->next = NULL;
	return;
}

/* gift a task to one waiting to allocate */
static task_t *task_handoff(task_t *next) {
	task_t *work = list_pop(&runq.begin);
	work->next = next;
	work->status = STATUS_RUNNABLE;
	--runq.parked;
	return work;
}

static task_t *find_work(int must) {
	task_t *work = list_pop(&runq.queue);
	if (work == NULL) {
		if (runq.begin.top) {
			/* now we've proven we need to allocate */
			work = task_handoff(new_task());
		} else if (must) {
			if (unlikely(runq.iowait == 0))
				panic("deadlock");

		        poll(-1); /* TODO: timers */
			work = list_pop(&runq.queue);
			if (unlikely(work == NULL))
				panic("deadlock");

		}
	}
	return work;
}

static inline void smashing_check(task_t *task) {
	uintptr_t magic = *(uintptr_t *)(task->stack - sizeof(uintptr_t));
	if (unlikely(magic != stack_magic(task))) 
		panic("stack overflow detected!");

}

/* to de-schedule, set runq.running->status, then call swtch(find_work(1)) */
static void swtch(task_t *next) {
	/*
	 * unlikely but possible: the task that runs the poller
	 * is the first one to be available.
	 */
	if (unlikely(next == runq.running)) {
		runq.running->status = STATUS_RUNNING;
		return;
	}
	
	BUG_ON(next->status != STATUS_RUNNABLE);
	smashing_check(next);
	next->status = STATUS_RUNNING;
	task_t *me = runq.running;
	runq.running = next;
	_swapctx(&me->ctx, &next->ctx);
	return;
}

static int yield(int block, tasklist_t *target) {
	task_t *next = find_work(block);
	if (next == NULL) 
		return 0;

	list_pushback(target, runq.running);
	swtch(next);
	return 1;
}

void sched(void) {
	runq.running->status = STATUS_RUNNABLE;
	if (yield(0, &runq.queue) == 0)
		runq.running->status = STATUS_RUNNING;
}

/* park task on tasklist; deschedule */
void wait(tasklist_t *tl) {
	runq.running->status = STATUS_PARKED;
	++runq.parked;
	yield(1, tl);
}

static void ready(task_t *task) {
	task->status = STATUS_RUNNABLE;
	list_pushback(&runq.queue, task);
}

static void unpark(task_t *task) {
	BUG_ON(task->status != STATUS_PARKED);
	--runq.parked;
	ready(task);
}

static void io_unpark(task_t *task) {
	BUG_ON(task->status != STATUS_IOWAIT);
	--runq.iowait;
	ready(task);
}

/* schedule the target task *immediately* with i/o cancellation */
static void io_cancel_now(task_t *task) {
	BUG_ON(task->status != STATUS_IOWAIT);
	task->next = MAP_FAILED;
	task->status = STATUS_RUNNABLE;
	--runq.iowait;
	ready(runq.running); /* set currently-running task as runnable */
	swtch(task);
}

int wake(tasklist_t *tl) {
	BUG_ON(tl == &runq.queue);
	task_t *task = list_pop(tl);
	if (task)
		unpark(task);

	return (task) ? 1 : 0;
}

int wakeall(tasklist_t *tl) {
	int out = 0;
	while (wake(tl)) 
		++out;
	
	return out;
}

/* task entry point */
__attribute__((noreturn))
static void _sbrt_entry(void) {
	task_t *self = runq.running;
	self->start(get_arg0(self->stack));
	_sbrt_exit();
}

/* longjmp into a task (abandon the current one) */
__attribute__((noreturn))
static void run(task_t *task) {
	BUG_ON(task->status != STATUS_RUNNABLE);
	smashing_check(task);
	runq.running = task;
	task->status = STATUS_RUNNING;
	_loadctx(&task->ctx);
}

/* free running task; jump to next available work */
__attribute__((noreturn))
static void _sbrt_exit(void) {
	/* free/clear old task state */
	task_t *old = runq.running;
	BUG_ON(old->status != STATUS_RUNNING);
	old->status = STATUS_EMPTY;
	old->start = NULL;

	task_t *target;
	if (runq.begin.top) {
		target = task_handoff(old);
	} else {
		free_task(old);
		target = find_work(1);
	}
	run(target);
}

void spawn(void (*start)(word_t), word_t data) {
	task_t *t;
	
	if (runq.begin.top || runq.queue.top) {
		wait(&runq.begin);
		t = runq.running->next;
		runq.running->next = NULL;
	} else {
		t = new_task();
	}

	if (unlikely(t == NULL))
		panic("out of memory");

	t->start = start;
	setup(&t->ctx, t->stack, _sbrt_entry, data);
	ready(t);
	return;
}

static int park_and_iowait(task_t **addr) {
	*addr = runq.running;
	runq.running->status = STATUS_IOWAIT;
	++runq.iowait;
	swtch(find_work(1));
	*addr = NULL;

	/* async wakeup due to cancelation */
	if (unlikely(runq.running->next == MAP_FAILED)) {
		runq.running->next = NULL;
		errno = ECANCELED;
		return -1;
	}

	return 0;
}

void ioctx_cancel(ioctx_t *ctx) {
	if (ctx->writer)
		io_cancel_now(ctx->writer);
	
	if (ctx->reader)
		io_cancel_now(ctx->reader);
	
}

ssize_t ioctx_write(ioctx_t *ctx, char *buf, size_t bytes) {
	if (unlikely(ctx->fd == -1)) {
		errno = ECANCELED;
		return -1;
	}
	ssize_t amt;
try:
	amt = write(ctx->fd, buf, bytes);
	if (amt == -1) {
		switch (errno) {
		case EAGAIN:
			if (unlikely(ctx->writer))
				panic("concurrent calls to ioctx_write()");

			if (unlikely(park_and_iowait(&ctx->writer) < 0))
				return -1;

		case EINTR:
			goto try;
		}
	}
	return amt;
}

ssize_t ioctx_read(ioctx_t *ctx, char *buf, size_t max) {
	ssize_t amt;
try:
	amt = read(ctx->fd, buf, max);
	if (amt == -1) {
		switch (errno) {
		case EAGAIN:
			if (unlikely(ctx->reader))
				panic("concurrent calls to ioctx_read()");

			if (unlikely(park_and_iowait(&ctx->reader) < 0))
				return -1;

		case EINTR:
			goto try;
		}
	}
	return amt;
}

/* library bootstrap - set main()'s stack as t0 */
__attribute__((constructor))
void chip_init(void) {
	runq.t0.status = STATUS_RUNNING;
	runq.running = &runq.t0;

	/* 
	   t0 has a fake top-of-stack pointer
	   in order to make the stack-smashing 
	   detector happy.
	 */
	runq.t0.stack = ((char *)&runq.t0_magic) + sizeof(uintptr_t);
	runq.t0_magic = stack_magic(&runq.t0);
	pollinit();
}
