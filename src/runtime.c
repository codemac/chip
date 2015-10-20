#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <sys/mman.h>
#include "runtime.h"

#define noreturn void __attribute__((noreturn))

typedef struct {
/* 
 * we need all callee-saved registers
 * N.B. keep in sync with context_{arch}.s 
 */
#ifdef __x86_64__
	uintptr_t rbx;
	uintptr_t rbp;
	uintptr_t r10;
	uintptr_t r11;
	uintptr_t r12;
	uintptr_t r13;
	uintptr_t r14;
	uintptr_t r15;
	uintptr_t rip;
	uintptr_t rsp;
#elif __arm__
	uintptr_t r5;
	uintptr_t r6;
	uintptr_t r7;
	uintptr_t r8;
	uintptr_t r9;
	uintptr_t r10;
	uintptr_t r11;
	uintptr_t r12;
	uintptr_t r13;
	uintptr_t r14;
#endif
} regctx_t;

static inline void setup(regctx_t *ctx, uintptr_t stack, uintptr_t retpc) {
#ifdef __x86_64__
	ctx->rsp = stack;
	ctx->rip = retpc;
#elif __arm__
	ctx->r13 = stack;
	ctx->r14 = retpc;
#endif
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
extern int _savectx(regctx_t*);
extern noreturn _loadctx(const regctx_t*);

static inline void swapctx(regctx_t *save, const regctx_t *jmp) {
	/* belts and suspenders */
	assert(save != jmp);

	if (_savectx(save) == 0) {
		_loadctx(jmp);
	}
	return;
}

/* possible task statuses */
enum {
	STATUS_EMPTY,    /* uninitialized */
	STATUS_RUNNABLE, /* able to run (use swapctx()) */
	STATUS_RUNNING,  /* running now */
	STATUS_PARKED,   /* was running; waiting for event */
};

struct task_s {
	void       (*start)(void*); 
	void       *udata;  /* passed to task->start() */
	task_t 	   *next;
	regctx_t   ctx;    /* saved register state, if not running */
	int        status; /* STATUS_XXX */
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

static noreturn taskexit(void);

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
 * find a runnable task; call swap 
 * returns 0 if no task is available
 */
static int yield(int block, tasklist_t *target) {
	task_t *other = list_pop(&runq.queue);
	if (other == NULL && onpoll != NULL) {
		onpoll(block);
		other = list_pop(&runq.queue);
	}
	if (other == NULL) {
		if (block) assert(0 && "deadlock!");
		return 0;
	}

	/* pushback currently-running task */
	assert(other != runq.running);
	assert(other->status == STATUS_RUNNABLE);

	other->status = STATUS_RUNNING;
	task_t *this = runq.running;
	runq.running = other;
	list_pushback(target, this);
	swapctx(&this->ctx, &other->ctx);

	/* we were woken */
	assert(runq.running == this);
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
	return;
}

static void ready(task_t *task) {
	task->status = STATUS_RUNNABLE;
	list_pushback(&runq.queue, task);
}

int wake(tasklist_t *tl) {
	assert(tl != &runq.queue);
	task_t *task = list_pop(tl);
	if (task) {
		assert(task->status == STATUS_PARKED);
		--runq.parked;
		ready(task);
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

/* task start - we get SIGSEGV if we return */
static noreturn __entry(void) {
	runq.running->start(runq.running->udata);
	taskexit();
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
	assert(0 && "unreachable");
}

/*
 * exit point for all tasks except t0
 * longjmps into another task, or aborts
 */
static noreturn taskexit(void) {
	task_t *old = runq.running;
	assert(old->status == STATUS_RUNNING);
	old->status = STATUS_EMPTY;
	old->start = NULL;
	old->udata = NULL;
	slab_free(&tslab, old);

	/* 
	 * if someone was waiting to
	 * create a new task, mark them
     * as runnable.
	 */
	wake(&runq.begin);
	
	task_t *next = list_pop(&runq.queue);
	if (next == NULL && runq.parked) {
		if (onpoll) {
			onpoll(1);
			next = list_pop(&runq.queue);
		}
	}
	assert(next && "deadlock!");
	run(next);
}

void spawn(void (start)(void*), void *data) {
	task_t *t = NULL;
	int waited = 0;
	assert(start);

	/* 
	 * We may need to wait 
     * to allocate; there are
	 * a fixed number of tasks.
	 * If there other tasks in the
	 * queue, then enqueue rather than
	 * attempting to malloc in order
	 * to preserve fairness.
     */
	if (runq.begin.top) {
	dowait:
		wait(&runq.begin);
	}
	t = slab_malloc(&tslab);
	if (t == NULL) {
		if (waited == 0) {
			waited = 1;
			goto dowait;
		}
		assert(0 && "deadlock!");
	}

	t->udata = data;
	t->start = start;
	setup(&t->ctx, (uintptr_t)t->stack, (uintptr_t)__entry);
	ready(t);
	return;
}

extern int taskmain();

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define STACK_PAGES 2 /* 8k stacks are reasonably generous */
#define GUARD_PAGES 1
#define TSTKSZ (STACK_PAGES*PAGE_SIZE)
#define GSTKSZ ((STACK_PAGES+GUARD_PAGES)*PAGE_SIZE)
#define STACK_ARENA_SIZE (GSTKSZ*MAXTASKS)

int main(void) {
	/* 
	 * setup t0, which
	 * runs on the system stack.
	 */
	runq.t0.status = STATUS_RUNNING;
	runq.running = &runq.t0;

	/* map all stacks in one mapping */
	stack_mapped = mmap(NULL, STACK_ARENA_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, -1, 0);
	assert(stack_mapped);

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

	int ret = taskmain();
	munmap(stack_mapped, STACK_ARENA_SIZE);
	return ret;
}
