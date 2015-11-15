#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>
#include <signal.h>
#include <limits.h>

#ifdef __gnu_linux__
#include "runtime_epoll.h"
#elif __APPLE__
#include "runtime_kqueue.h"
#else
#error "unsupported OS"
#endif

/* run-of-the-mill gcc grossness */
#define noreturn void __attribute__((noreturn))
#define cold __attribute((cold))
#define CHIP_INIT __attribute__((constructor))
#define clobber_mem() __asm__("" : : : "memory")
#define unlikely(expr) __builtin_expect(!!(expr), 0)

/* try not to consume any stack space with runtime assertions */
#define runtime_assert_or(expr, str) if (unlikely(!(expr)))	\
	{ __panicstr(str, sizeof(str)-1); }

#define panic(str) __panicstr(str, sizeof(str)-1)

cold static noreturn  __panicstr(const char *msg, size_t len) {
	write(2, msg, len);
	raise(SIGABRT);
	_exit(1); /* we probably won't get here. */
}

/* for type-punning register values */
typedef union {
	void *ptr;
	uintptr_t val;
} word_t;

typedef struct {
/* N.B. keep in sync with context_{arch}.s  */
#ifdef __x86_64__
	word_t rbx;
	word_t rbp;
	word_t r10;
	word_t r11;
	word_t r12;
	word_t r13;
	word_t r14;
	word_t r15;
	word_t rsp;
#elif __arm__
	word_t r5;
	word_t r6;
	word_t r7;
	word_t r8;
	word_t r9;
	word_t r10;
	word_t r11;
	word_t r12;
	word_t r13;
	word_t r14;
#endif
} regctx_t;

static inline void setup(regctx_t *ctx, word_t stack, word_t retpc) {
#ifdef __x86_64__
	/* 
	   _sbrt_entry() presumes entry on an un-even stack, so we need
	   the stack to be 8- (but not 16-)byte aligned when we return
	   from the context we're creating.
	 */
	stack.val -= 16;
	ctx->rsp = stack;
	*(uintptr_t *)stack.ptr = retpc.val;
#elif __arm__
	ctx->r13 = stack;
	ctx->r14 = retpc;
#endif
	return;
}

extern void _swapctx(regctx_t *save, const regctx_t *load);
extern noreturn _loadctx(const regctx_t *load);

/* possible task statuses */
enum {
	STATUS_EMPTY,    /* uninitialized */
	STATUS_RUNNABLE, /* able to run (use swapctx()) */
	STATUS_RUNNING,  /* running now */
	STATUS_PARKED,   /* was running; waiting for event */
};

typedef struct arena_s arena_t;

struct task_s {
	task_t 	   *next;
	int        status; /* STATUS_XXX */
	int        index;  /* index in arena */
	regctx_t   ctx;    /* saved register state, if not running */
	void       (*start)(void*); 
	void       *udata; /* passed to task->start() */
	void       *stack;
	arena_t    *arena;
};

/* the global run queue/state */
static struct{
	task_t     *running;
	tasklist_t queue;    /* runnable */
	int        parked;   /* count of parked tasks */
	tasklist_t begin;    /* blocking requests to newtask() */
	task_t     t0;       /* the root task (taskmain()) */
} runq;

#define STACK_SIZE 8192
#define GUARD_SIZE 4096
#define FULL_STACK_SIZE (STACK_SIZE+GUARD_SIZE)
#define ARENA_TASKS (sizeof(uintptr_t)*8)
#define ARENA_STACK_MAPPING (ARENA_TASKS*FULL_STACK_SIZE)
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
  |guard|   stack   |guard|            | arena   |
  +------------------------------- ... ----------+
 */
static arena_t *map_arena(void) {
	void *mem = mmap(NULL, ARENA_MAPPING, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		return NULL;
	
	
	arena_t *out = (arena_t *)(mem + ARENA_STACK_MAPPING);

	for (int i=0; i<ARENA_TASKS; ++i) {
		void *bottom = mem + (i * FULL_STACK_SIZE);
		void *top = bottom + FULL_STACK_SIZE;
		mprotect(bottom, GUARD_SIZE, PROT_NONE);
		out->tasks[i].stack = top;
		out->tasks[i].arena = out;
		out->tasks[i].index = i;
	}
	return out;
}

static void unmap_arena(arena_t *arena) {
	void *top = arena;
	void *base = top - ARENA_STACK_MAPPING;
	munmap(base, ARENA_MAPPING);
}

static task_t *arena_get_task(arena_t *arena) {
	uintptr_t v = ~(arena->bits);
	runtime_assert_or(v, "alloc from full arena");
	int index = __builtin_ffsl(v)-1;
	task_t *out = &arena->tasks[index];
	runtime_assert_or(out->status == STATUS_EMPTY,
			  "fresh task isn't empty");
	runtime_assert_or(out->index == index, "inconsistent task index");
	/* set index bit  */
	arena->bits |= ((uintptr_t)1<<index);
	return out;
}

static void arena_put_task(task_t *task) {
	arena_t *arena = task->arena;
	uintptr_t old = arena->bits;
	arena->bits &= ~((uintptr_t)1<<(task->index));
	runtime_assert_or(arena->bits != old, "double-free");
}

/*
  The task heap.
  Allocation is more-or-less LIFO and first-fit.
 */
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

static task_t *new_task(void) {
	task_t *out = NULL;
	
	if (theap.partial) {
		out = arena_get_task(theap.partial);
		runtime_assert_or(out,
				  "failed alloc from partially-full arena");

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
			theap.empty = NULL;
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
		runtime_assert_or(out, "failed alloc from empty arena");
	}
	
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
	runtime_assert_or(task->status == STATUS_EMPTY,
			  "free of non-empty task");
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
			unmap_arena(old);
			theap.alloc -= ARENA_TASKS;
		}
	}
}

static noreturn _sbrt_exit(void);

static void add_stats_from(arena_t *arena, tsk_stats_t *stats) {
	int running = 0;
	for (arena_t *a = arena; a != NULL; a = a->next) {
		for (int i=0; i<ARENA_TASKS; ++i) {
			switch (a->tasks[i].status) {
			case STATUS_EMPTY:
				stats->free++;
				break;
			case STATUS_PARKED:
				stats->parked++;
				break;
			case STATUS_RUNNABLE:
				stats->runnable++;
				break;
			case STATUS_RUNNING:
				runtime_assert_or(running == 0,
						  "more than 1 running task");
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
	switch (runq.t0.status) {
	default:
		panic("bad t0 status");
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
}

static task_t *list_pop(tasklist_t *tl) {
	if (tl->top == NULL)
		return NULL;

	task_t *out = tl->top;
	tl->top = out->next;
	if (tl->top == NULL) {
		runtime_assert_or(out == tl->tail, "bad worklist state");
		tl->tail = NULL;
	}
	out->next = NULL;
	return out;
}

static void list_pushback(tasklist_t *tl, task_t *task) {
	if (tl->top == NULL) {
		runtime_assert_or(tl->tail == NULL, "bad worklist state");
		tl->top = task;
		tl->tail = task;
		return;
	}
	runtime_assert_or(tl->tail, "bad worklist state");
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
		        poll(-1); /* TODO: timers */
			work = list_pop(&runq.queue);
			runtime_assert_or(work, "deadlock");
		}
	}
	return work;
}

/* to de-schedule, set runq.running->status, then call swtch(find_work(1)) */
static void swtch(task_t *next) {
	runtime_assert_or(next != runq.running,
			  "tried to schedule onto self");
	runtime_assert_or(next->status == STATUS_RUNNABLE,
			  "tried to schedule unrunnable task");
	next->status = STATUS_RUNNING;
	task_t *me = runq.running;
	runq.running = next;
	_swapctx(&me->ctx, &next->ctx);
	clobber_mem();
	return;
}

static int yield(int block, tasklist_t *target) {
	task_t *next = find_work(block);
	if (next == NULL) return 0;
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
	runtime_assert_or(task->status == STATUS_PARKED,
			  "unpark of unparked task");
	--runq.parked;
	ready(task);
}

int wake(tasklist_t *tl) {
	runtime_assert_or(tl != &runq.queue,
			  "wake called on worklist");
	task_t *task = list_pop(tl);
	if (task)
		unpark(task);

	return (task) ? 1 : 0;
}

int wakeall(tasklist_t *tl) {
	int out = 0;
	while(wake(tl)) ++out;
	return out;
}

/* task entry point */
static noreturn _sbrt_entry(void) {
	runq.running->start(runq.running->udata);
	_sbrt_exit();
}

/* longjmp into a task (abandon the current one) */
static noreturn run(task_t *task) {
	runtime_assert_or(task->status == STATUS_RUNNABLE,
			  "run() called on unrunnable task");
	runq.running = task;
	task->status = STATUS_RUNNING;
	_loadctx(&task->ctx);
}

/* free running task; jump to next available work */
static noreturn _sbrt_exit(void) {
	/* free/clear old task state */
	task_t *old = runq.running;
	runtime_assert_or(old->status == STATUS_RUNNING,
			  "runq.running is not running");
	old->status = STATUS_EMPTY;
	old->start = NULL;
	old->udata = NULL;

	task_t *target;
	if (runq.begin.top) {
		target = task_handoff(old);
	} else {
		free_task(old);
		target = find_work(1);
	}
	run(target);
}

/*
  Create a new runnable task.

  (Usually this results in de-scheduling; allocators
  of new tasks are put into a lower-priority queue
  than other runnable tasks.)
 */
void spawn(void (start)(void*), void *data) {
	task_t *t;
	word_t stack;
	word_t retpc;
	
	if (runq.begin.top || runq.queue.top) {
		wait(&runq.begin);
		t = runq.running->next;
		runq.running->next = NULL;
	} else {
		t = new_task();
	}

	runtime_assert_or(t, "out of memory");
	t->udata = data;
	t->start = start;
	stack.ptr = t->stack;
	retpc.ptr = _sbrt_entry;
	setup(&t->ctx, stack, retpc);
	ready(t);
	return;
}

static void park_and_wait(task_t **addr) {
	*addr = runq.running;
	runq.running->status = STATUS_PARKED;
	++runq.parked;
	swtch(find_work(1));
	*addr = NULL;
	return;
}

ssize_t ioctx_write(ioctx_t *ctx, char *buf, size_t bytes) {
	ssize_t amt;
try:
	amt = write(ctx->fd, buf, bytes);
	if ((amt == -1) && (errno == EAGAIN)) {
		park_and_wait(&ctx->writer);
		goto try;
	}
	return amt;
}

ssize_t ioctx_read(ioctx_t *ctx, char *buf, size_t max) {
	ssize_t amt;
try:
	amt = read(ctx->fd, buf, max);
	if ((amt == -1) && (errno == EAGAIN)) {
		park_and_wait(&ctx->reader);
		goto try;
	}
	return amt;
}

ptrdiff_t stack_remaining(void) {
	int dummy;
	uintptr_t stack_top = (uintptr_t)runq.running->stack;
	uintptr_t stack_bottom = (uintptr_t)(&dummy);
	/* guess at system stack being at least 2MB */
	uintptr_t stack_size = (runq.running == &runq.t0) ? (1<<21) : STACK_SIZE;
	return stack_size - (ptrdiff_t)(stack_top - stack_bottom);
}

/* library bootstrap - set main()'s stack as t0 */
CHIP_INIT void chip_init(void) {
	runq.t0.status = STATUS_RUNNING;
	runq.running = &runq.t0;
	int dummy;
	runq.running->stack = &dummy;
	pollinit();
}
