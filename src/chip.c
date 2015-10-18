#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "chip.h"

#ifdef __x86_64__
#define NUM_SAVED_REGS 11
#elif __arm__
#define NUM_SAVED_REGS 10
#else
#error "unsupported arch!"
#endif

#define noreturn void __attribute__((noreturn))

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

typedef struct task_s task_t;

typedef struct tasklist_s tasklist_t;

struct tasklist_s {
	task_t *top;
	task_t *tail;
};

struct task_s {
	void      (*start)(void*); 
	void      *udata;  /* passed to task->start() */
	task_t 	  *next;
	regctx_t   ctx;    /* saved register state, if not running */
	int        status; /* STATUS_XXX */
	char       pad[16];
	char       stack[4096];
	char       pad2[16];
};

/* the global run queue/state */
static struct{
	task_t *running;
	task_t t0;        /* the root task */
	tasklist_t queue; /* runnable */
	int parked;       /* count of parked tasks */
	tasklist_t begin; /* blocking requests to newtask() */
} runq;

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

static void wait(tasklist_t *l);

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

static int wake(tasklist_t *tl) {
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

/* TODO: netpolling */
static void netpoll(int block) { return; }

/* 
 * find a runnable task; call swap 
 * returns 0 if no task is available
 */
static int yield(int block) {
	task_t *other = list_pop(&runq.queue);
	if (other == NULL) {
		netpoll(block);
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
	list_pushback(&runq.queue, this);
	swapctx(&this->ctx, &other->ctx);

	/* we were woken */
	assert(runq.running == this);
	return 1;
}

void sched(void) {
	runq.running->status = STATUS_RUNNABLE;
	yield(0);
}

/* task start */
static noreturn __entry(void) {
	runq.running->start(runq.running->udata);
	taskexit();
}

static noreturn run(task_t *task) {
	assert(task->status == STATUS_RUNNABLE);
	runq.running = task;
	task->status = STATUS_RUNNING;
	_loadctx(&task->ctx);
	assert(0 && "unreachable");
}

/* task exit -- should never return */
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
	if (runq.begin.top) {
		task_t *c = list_pop(&runq.begin);
		c->status = STATUS_RUNNABLE;
		list_pushback(&runq.queue, c);
	}
	
	task_t *next = list_pop(&runq.queue);
	if (next == NULL && runq.parked) {
		/* find work */
		netpoll(1);
		assert(next = list_pop(&runq.queue));
	}

	run(next);
}

/*
 * spawn starts a new coroutine that begins
 * executing the given function.
 */
void spawn(void (start)(void*), void *data) {
	task_t *t;
	assert(start);

	/* 
	 * we may need to wait 
     * to allocate; there are
	 * a fixed number of tasks.
     */
	if (runq.begin.top) {
		wait(&runq.begin);	
	}
	t = slab_malloc(&tslab);
	assert(t);
	t->udata = data;
	t->start = start;
	t->status = STATUS_RUNNABLE;
	setstack(&t->ctx, (uintptr_t)(&t->pad2));
	setreturn(&t->ctx, (uintptr_t)(__entry));
	list_pushback(&runq.queue, t);
	return;
}

/* park task on tasklist; deschedule */
static void wait(tasklist_t *tl) {
	runq.running->status = STATUS_PARKED;
	++runq.parked;
	list_pushback(tl, runq.running);
	yield(1);
	return;
}

/* main */
extern int taskmain();

int main(void) {
	/* 
	 * setup t0, which
	 * runs on the system stack.
	 */
	runq.t0.status = STATUS_RUNNING;
	runq.running = &runq.t0;
	return taskmain();
}
