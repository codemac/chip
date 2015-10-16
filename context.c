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

/* 
 * set up a ctx such that the next time
 * it is jumped into, func(udata) will
 * be called.
 */
void setup(regctx_t *ctx, void (*func)(void*), void *udata, void *stack) {
	setstack(ctx, (uintptr_t)stack);
	setreturn(ctx, (uintptr_t)func);
	setarg0(ctx, (uintptr_t)udata);
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

/* the global run queue */
struct{
	tasklist_t queue;
} runq;

struct task_s {
	void      *stack;
	task_t 	  *next;
	task_t    *prev;
	regctx_t   ctx;
	int        status;
};

#define MAXTASKS 1024
#define MAXTASKWORDS MAXTASKS/(sizeof(uintptr_t)*8)
task_t all_tasks[MAXTASKS];

task_t *list_pop(tasklist_t *tl) {
	if (tl->top == NULL) {
		return NULL;
	}
	task_t *out = tl->top;
	tl->top = out->next;
	if (tl->top) {
		tl->top->prev = NULL;
	} else {
		assert(out == tl->tail);
		tl->tail = NULL;
	}
	out->next = NULL;
	out->prev = NULL;
	return out;
}

void list_pushback(tasklist_t *tl, task_t *task) {
	if (tl->top == NULL) {
		tl->top = task;
		tl->tail = task;
		return;
	}
	assert(tl->tail);
	tl->tail->next = task;
	task->prev = tl->tail;
	tl->tail = task;
	task->next = NULL;
	return;
}

int wake(tasklist_t *tl) {
	assert(tl != &runq.queue);
	task_t *task = list_pop(tl);
	if (task) {
		task->status = STATUS_RUNNABLE;
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
		swapctx(&task->ctx, &other->ctx);
		return 1;
	}
	/* todo: epoll */
	return 0;
}

/* park task on tasklist; deschedule */
void wait(tasklist_t *tl, task_t *task) {
	task->status = STATUS_PARKED;
	list_pushback(tl, task);
	if (yield(task) == 0) assert(0 && "deadlock");
	/* make sure we were woken by another yield() */
	assert(task->status == STATUS_RUNNING);
	return;
}
