#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#ifdef __x86_64__
#define NUM_SAVED_REGS 11
#elif __arm__
#define NUM_SAVED_REGS 10
#else
#error "unsupported arch!"
#endif

typedef struct {
/* 
 * we need all callee-saved
 * registers, plus the register
 * used for the 1st argument
 * (see setup())
 *
 * N.B. keep in sync with context_{arch}.s 
 */
#ifdef __x86_64__
	uintptr_t rax; /* return value */
	uintptr_t rdi; /* arg0 */
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
	uintptr_t r0; /* return val and arg0 */
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


inline void setstack(regctx_t *ctx, uintptr_t sp) {
#ifdef __x86_64__
	ctx->rsp = sp;
#elif __arm__
	ctx->r13 = sp;
#endif
	return;
}

inline void setreturn(regctx_t *ctx, uintptr_t pc) {
#ifdef __x86_64__
	ctx->rip = pc;
#elif __arm__
	ctx->r14 = pc; /* lr = pc */
#endif
	return;
}

inline void setarg0(regctx_t *ctx, uintptr_t arg) {
#ifdef __x86_64__
	ctx->rdi = arg;
#elif __arm__
	ctx->r0 = arg;
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
extern int __savectx(regctx_t*);
extern void __loadctx(const regctx_t*);

static inline void swapctx(regctx_t *save, const regctx_t *jmp) {
	/* belts and suspenders */
	assert(save != jmp);

	if (__savectx(save) == 0) {
		__loadctx(jmp);
		/* loadctx should never return! */
		assert(0 && "__loadctx returned!");
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

typedef struct task_s task_t;

typedef struct tasklist_s tasklist_t;

struct tasklist_s {
	task_t *top;
	task_t *tail;
};

/* the global run queue/state */
static struct{
	task_t *running;
	task_t t0;        /* the root task */
	tasklist_t queue; /* runnable */
	int parked;       /* count of parked tasks */
	tasklist_t begin; /* blocking requests to newtask() */
} runq;

struct task_s {
	void      *stack;  /* stack base */
	void      (start)(void*); 
	void      *udata;  /* passed to task->start() */
	task_t 	  *next;
	regctx_t   ctx;    /* saved register state, if not running */
	int        status; /* STATUS_XXX */
};

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


/* task start */
static void __entry(void) {
	running->start(running->udata);
	__exit();
}

/* task exit -- should never return */
static void __exit(void) {
	running->status = STATUS_EMPTY;
	slab_free(&tslab, running);

	/* if someone was waiting to
	 * create a new task, mark them
         * as runnable.
	 */
	if (runq.begin.top) {
		task_t *c = list_pop(&runq.begin);
		c->status = STATUS_RUNNABLE;
		list_pushback(&runq.runnable, c);
	}
	
	__jmpnext();
	assert(0 && "unreachable");
}

void taskexit(void) { __exit(); }

/* TODO: netpolling */
static void netpoll(block int) { return; }

/* jump to next free task */
static void __jmpnext(void) {
	task_t *next = list_pop(&runq.queue);
	if (next == NULL && runq.parked) {
		/* find work */
		netpoll(1);
		next = list_pop(&runq.queue);
	}
	/* there's no work left to do -- exit */
	if (!next) exit(0);
	
	assert(top->status == STATUS_RUNNABLE);
	running = top;
	top->status = STATUS_RUNNING;
	__loadctx(&top->ctx);
}

int yield();

void wait(tasklist_t *l, task_t *t);

void newtask(void (func)(void*), void *data) {
	task_t *t;

	/* 
	 * we may need to wait 
         * to allocate; there are
	 * a fixed number of tasks.
         */
	if (runq.begin.top) {
		wait(&runq.begin, running);	
	}

	t = slab_malloc(&tslab);
	assert(t);
	t->udata = data;
	t->start = func;
	t->status = STATUS_RUNNABLE;
	setup(&t->ctx, stack);
	list_pushback(&runq.runnable, t);
	return;
}

/* 
 * set up a ctx such that the next time
 * it is jumped into, func(udata) will
 * be called.
 */
void setup(regctx_t *ctx, void *stack) {
	setstack(ctx, (uintptr_t)stack);
	setreturn(ctx, (uintptr_t)__entry);
	return;
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

void list_pushback(tasklist_t *tl, task_t *task) {
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

int wake(tasklist_t *tl) {
	assert(tl != &runq.queue);
	task_t *task = list_pop(tl);
	if (task) {
		task->status = STATUS_RUNNABLE;
		--runq.parked;
		list_pushback(&runq.queue, task);
		return 1;
	}
	return 0;
}

/* mark everything on the
 * list as runnable; returns
 * the number of tasks woken.
 */
int wakeall(tasklist_t *tl) {
	assert(tl != &runq.queue);
	int out = 0;
	while(wake(tl)) ++out;
	return out;
}

/* find a runnable task; call swap 
 * returns 0 if no task is available
 */
static int yield(task_t* task) {
	task_t *other = list_pop(&runq.queue);
	if (other) {
		assert(other != task);
		assert(other->status == STATUS_RUNNABLE);
		other->status = STATUS_RUNNING;
		runq.running = other;
		swapctx(&task->ctx, &other->ctx);
		return 1;
	}
	/* todo: epoll */
	return 0;
}

/* park task on tasklist; deschedule */
void wait(tasklist_t *tl, task_t *task) {
	task->status = STATUS_PARKED;
	++runq.parked
	list_pushback(tl, task);
	if (yield(task) == 0) assert(0 && "deadlock");

	/* make sure we were woken appropriately */
	assert(task->status == STATUS_RUNNING);
	assert(runq.running == task)
	return;
}

/* main */
extern int taskmain();

int main(void) {
	/* 
	 * setup t0, which
	 * runs on the system stack.
	 */
	t0.status = STATUS_RUNNING;
	runq.running = &runq.t0;
	return taskmain();
}
