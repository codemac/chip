#ifndef __CHIP_RUNTIME_H_
#define __CHIP_RUNTIME_H_

/* 
	Some notes on task scheduling:
	Tasks are FIFO-scheduled, so calls to spawn()
	put tasks at the end of the list of runnable
	tasks. Similarly, a call to sched() puts the
	currently-running task at the end of the 
	list.
 */

/* 
 * spawn() a new coroutine
 */
void spawn(void (start)(void*), void *data);

/* yield to the scheduler */
void sched(void);

/* chip defines main() */
int main(void);

/*
 * The following primitives can be used
 * to build higher-level synchronization 
 * abstractions like mutexes and semaphores.
*/

/* task_t is an opaque type representing a task */
typedef struct task_s task_t;

/* 
 * A tasklist is a FIFO queue of tasks
 * that are waiting to be scheduled.
 *
 * A tasklist should be initialized with
 * both fields set to NULL. Freeing a tasklist
 * without first releasing any parked tasks
 * (i.e. with wakeall()) will result in leaked
 * tasks.
 */
typedef struct {
	task_t *top;
	task_t *tail;
} tasklist_t;

/* 
 * wait() blocks the currently-running
 * task until a call to wake() or wakeall()
 * on this tasklist unblocks it.
 */
void wait(tasklist_t *list);

/* 
 * wake() unblocks a task parked with wait() if
 * there is one. The number of tasks unblocked is returned
 * (either 0 or 1).
 */
int wake(tasklist_t *list);

/* 
 * like wait(), wakeall() unblocks all
 * tasks waiting on the tasklist,
 * and returns the number of tasks unblocked.
 */
int wakeall(tasklist_t *list);

/*
 * if onpoll is non-NULL,
 * it is called whenever the
 * scheduler is looking for work.
 * 
 * (this is used by netpollers)
 */
void (*onpoll)(int block);

#endif /* __CHIP_RUNTIME_H_ */