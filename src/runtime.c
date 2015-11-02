#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <sys/mman.h>
#include <limits.h>

#ifdef __gnu_linux__
#include "runtime_epoll.h"
#elif __APPLE__
#include "runtime_kqueue.h"
#else
#error "unsupported OS"
#endif

#define noreturn void __attribute__((noreturn))

#define clobber_mem() __asm__("" : : : "memory")

/* for type-punning register values */
typedef union {
	void *ptr;
	uintptr_t val;
} word_t;

typedef struct {
	word_t sp; /* current stack pointer */
} regctx_t;

static inline void setup(regctx_t *ctx, word_t stack, word_t retpc) {
#ifdef __x86_64__
	/* push retpc (w/ 16-byte stack alignment) */
	stack.val -= 16;
	*(uintptr_t *)stack.ptr = retpc.val;
	/* push callee-saves */
	stack.val -= 8*sizeof(word_t);

#elif __arm__
	/* lr is saved at the highest address */
	stack.val -= 4;
	*(uintptr_t *)stack.ptr = retpc.val;
	/* space for r5-r12 */
	stack.val -= 8*sizeof(word_t);
#endif

	ctx->sp = stack;
	return;
}

/* 
 * these need to be defined externally in
 * assembly so that caller-save regs
 * are actually saved.
 *
 * __savectx() returns 0 on entry,
 * and non-zero on 'return' 
 *
 * __loadctx() should never appear
 * to return (it corresponds to
 * __savectx() returning a non-zero
 * value somewhere else!)
 */
extern void _swapctx(regctx_t*, const regctx_t*);
extern noreturn _loadctx(const regctx_t*);

/* possible task statuses */
enum {
	STATUS_EMPTY,    /* uninitialized */
	STATUS_RUNNABLE, /* able to run (use swapctx()) */
	STATUS_RUNNING,  /* running now */
	STATUS_PARKED,   /* was running; waiting for event */
};

struct task_s {
	task_t 	   *next;
	int        status; /* STATUS_XXX */
	regctx_t   ctx;    /* saved register state, if not running */
	void       (*start)(void*); 
	void       *udata; /* passed to task->start() */
	void       *stack;
};

static void *stack_mapped;

/* the global run queue/state */
static struct{
	task_t     *running;
	tasklist_t queue;    /* runnable */
	int        parked;   /* count of parked tasks */
	tasklist_t begin;    /* blocking requests to newtask() */
	task_t     t0;       /* the root task (taskmain()) */
} runq;

/* a gross header hack for a bitmap slab allocator */
#define MAXTASKS 1024
#define BITSLAB_SIZE MAXTASKS
#define BITSLAB_TYPE task_t
#define BITSLAB_TYPE_NAME taskslab_t
#define BITSLAB_FN_PREFIX slab
#include "bitslab.h"
static taskslab_t tslab;
#undef BITSLAB_SIZE
#undef BITSLAB_TYPE
#undef BITSLAB_TYPE_NAME
#undef BITSLAB_FN_PREFIX

static noreturn _sbrt_exit(void);

void get_tsk_stats(tsk_stats_t *stats) {
	stats->free = 0;
	stats->parked = 0;
	stats->runnable = 0;
	int running = 0;
	switch (runq.t0.status) {
	case STATUS_PARKED:
		stats->parked++;
		break;
	case STATUS_RUNNABLE:
		stats->runnable++;
		break;
	case STATUS_RUNNING:
		running = 1;
		break;
	default:
		assert(0 && "bad t0 status");
	}
	for (int i=0; i<MAXTASKS; ++i) {
		switch (tslab.mem[i].status) {
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
			assert(running == 0);
			running = 1;
			break;
		default:
			assert(0 && "unknown task status!");
		}
	}
	assert(running == 1);
	assert(stats->parked == runq.parked);
}

task_t *list_pop(tasklist_t *tl) {
	if (tl->top == NULL) {
		return NULL;
	}
	task_t *out = tl->top;
	tl->top = out->next;
	if (tl->top == NULL) {
		assert(out == tl->tail);
		tl->tail = NULL;
	}
	out->next = NULL;
	return out;
}

static void list_pushback(tasklist_t *tl, task_t *task) {
	if (tl->top == NULL) {
		assert(tl->tail == NULL);
		tl->top = task;
		tl->tail = task;
		return;
	}
	assert(tl->tail);
	tl->tail->next = task;
	tl->tail = task;
	task->next = NULL;
	return;
}

/*
 * find_work() - the root of the scheduler.
 *
 * Every scheduling point ultimately calls find_work()
 * to figure out what to run next. Polling hooks should
 * be added here.
 */
static task_t *find_work(int must) {
	task_t *work = list_pop(&runq.queue);
	if (work == NULL && must) {
		poll(-1); /* TODO: timer */
		work = list_pop(&runq.queue);
	}
	if (must) {
		assert(work && "deadlock!");
	}
	return work;
}

/* to de-schedule, set runq.running->status, then call swtch(find_work(1)) */
static void swtch(task_t *next) {
	assert(next != runq.running);
	assert(next->status == STATUS_RUNNABLE);
	next->status = STATUS_RUNNING;
	task_t *me = runq.running;
	runq.running = next;
	_swapctx(&me->ctx, &next->ctx);
	clobber_mem();
	return;
}

/* 
 * find a runnable task; call swap 
 * returns 0 if no task is available
 */
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
	return;
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
	assert(task->status == STATUS_PARKED);
	--runq.parked;
	ready(task);
}

int wake(tasklist_t *tl) {
	assert(tl != &runq.queue);
	task_t *task = list_pop(tl);
	if (task) {
		unpark(task);
		return 1;
	}
	return 0;
}

int wakeall(tasklist_t *tl) {
	assert(tl != &runq.queue);
	int out = 0;
	while(wake(tl)) ++out;
	return out;
}

/* task entry point */
static noreturn _sbrt_entry(void) {
	runq.running->start(runq.running->udata);
	_sbrt_exit();
}

/* 
 * longjmp into a task, which must be runnable
 * (sets/clobbers runq.running)
 */
static noreturn run(task_t *task) {
	assert(task->status == STATUS_RUNNABLE);
	runq.running = task;
	task->status = STATUS_RUNNING;
	_loadctx(&task->ctx);
}

/* free running task; jump to next available work */
static noreturn _sbrt_exit(void) {
	/* free/clear old task state */
	task_t *old = runq.running;
	assert(old->status == STATUS_RUNNING);
	old->status = STATUS_EMPTY;
	old->start = NULL;
	old->udata = NULL;

	task_t *target;
	/* 
	 * micro-optimization: if someone is blocked in
	 * spawn(), jump directly into that stack with 'next' set,
	 * which avoids the call to slab_free here and the call
	 * to slab_malloc on the other side. (see the corresponding
	 * code in spawn())
	 */
	if (runq.begin.top) {
		target = list_pop(&runq.begin);
		--runq.parked;
		target->status = STATUS_RUNNABLE;
		target->next = old;
	} else {
		slab_free(&tslab, old);
		target = find_work(1);
	}
	run(target);
}

void spawn(void (start)(void*), void *data) {
	task_t *t;
	word_t stack;
	word_t retpc;

	/* 
	 * TODO:
	 * if every alloc'd task is parked on i/o,
	 * then mmap a new task (or arena of tasks).
	 */
	
	/* 
	 * We may need to wait 
	 * to allocate; there are
	 * a fixed number of tasks.
	 * If there other tasks in the
	 * queue, then enqueue rather than
	 * attempting to malloc in order
	 * to preserve fairness.
	 */
	if (runq.begin.top || (NULL == (t = slab_malloc(&tslab)))) {
		/* we should only be woken when ->next is set */
		wait(&runq.begin);
		assert(runq.running->next);
		t = runq.running->next;
		runq.running->next = NULL;
	}
	
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

extern int taskmain();

/* use either PAGE_SIZE or PAGESIZE before defaulting to 4096 */
#ifndef PAGE_SIZE
#ifdef PAGESIZE
#define PAGE_SIZE PAGESIZE
#else
#define PAGE_SIZE 4096
#endif
#endif

#define STACK_PAGES 2 /* 8k stacks are reasonably generous */
#define GUARD_PAGES 1
#define TSTKSZ (STACK_PAGES*PAGE_SIZE)
#define GSTKSZ ((STACK_PAGES+GUARD_PAGES)*PAGE_SIZE)
#define STACK_ARENA_SIZE (GSTKSZ*MAXTASKS)

ptrdiff_t stack_remaining(void) {
	if (runq.running == &runq.t0) return -1;
	int dummy;
	uintptr_t stack_top = (uintptr_t)runq.running->stack;
	uintptr_t stack_bottom = (uintptr_t)(&dummy);
	return TSTKSZ - (ptrdiff_t)(stack_top - stack_bottom);
}

int main(void) {
	/* 
	 * setup t0, which
	 * runs on the system stack.
	 */
	runq.t0.status = STATUS_RUNNING;
	runq.running = &runq.t0;

	/* map all stacks in one mapping */
	stack_mapped = mmap(NULL, STACK_ARENA_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
	assert(stack_mapped != MAP_FAILED);

	/* 
	 * set up stacks in ascending order with a guard page
	 * at the bottom. the hope is that the bitslab
	 * allocator will do a decent job w.r.t. locality.
	 */
	void *stkp = stack_mapped;
	for (int i=0; i<MAXTASKS; ++i) {
		tslab.mem[i].stack = stkp + GSTKSZ;
		mprotect(stkp, PAGE_SIZE, PROT_NONE); /* mark the low page as unwriteable */
		stkp += GSTKSZ;
	}

	pollinit();
	
	int ret = taskmain();
	munmap(stack_mapped, STACK_ARENA_SIZE);
	return ret;
}
