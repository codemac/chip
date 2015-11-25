#ifndef __CHIP_RUNTIME_H_
#define __CHIP_RUNTIME_H_
#include <stddef.h>
#include <sys/types.h>

/* 
 * spawn() a new coroutine
 */
void spawn(void (start)(void*), void *data);

/* yield to the scheduler; may return immediately */
void sched(void);

typedef struct {
	int runnable; /* number of currently-runnable tasks */
	int parked;   /* number of parked tasks */
	int iowait;   /* number of tasks waiting for i/o */
	int free;     /* number of free (unused) tasks */
} tsk_stats_t;

/*
 * get_tsk_stats() traverses the entire task
 * heap and returns information about the number
 * of tasks in different scheduling states. (Also,
 * some sanity checking is performed internally.)
 * This should probably only ever be used in test code.
 */
void get_tsk_stats(tsk_stats_t *);

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

typedef struct {
	int fd;
	task_t *writer;
	task_t *reader;
} ioctx_t;

/*
 * ioctx_init() initializes the given ioctx_t
 * to refer to the provided file descriptor.
 * The file descriptor must already have O_NONBLOCK
 * set. On success, 0 is returned. On error,
 * -1 is returned, and errno will be set.
 */
int ioctx_init(int fd, ioctx_t *ctx);


/*
 * ioctx_destroy() invalidates the given
 * ioctx_t and closes the associated file
 * descriptor. On success, 0 is returned.
 * On error, -1 is returned, and errno
 * will be set. Additionally, on success,
 * ctx->fd will be -1, and any tasks parked
 * on this ioctx will be unparked with 
 * errno set to ECANCELED (see ioctx_cancel()).
 */
int ioctx_destroy(ioctx_t *ctx);

/*
 * ioctx_write() writes the buffer starting
 * at 'buf' up to 'bytes' bytes, and returns
 * the number of bytes written. On error, -1
 * will be returned, and errno will be set.
 * If the file descriptor in question is not
 * available for writing (EAGAIN), then the
 * scheduler will be invoked, and the write
 * will be re-tried when the operating system
 * says the fd is writeable.
 *
 * Two tasks trying to perform writes on the
 * same fd at the same time will have undefined 
 * behavior, but the program may abort if the runtime
 * notices.
 */
ssize_t ioctx_write(ioctx_t *ctx, char *buf, size_t bytes);

/*
 * ioctx_read() reads into the buffer starting
 * at 'buf' up to 'bytes' bytes, and returns
 * the number of bytes read. On error, -1
 * will be returned, and errno will be set.
 * If the file descriptor in question is not
 * available for reading (EAGAIN), then the
 * scheduler will be invoked, and the read
 * will be re-tried when the operating system
 * says the fd is readable.
 *
 * Two tasks trying to perform reads on the same
 * fd at the same time will have undefined behavior,
 * but may abort the program if the runtime notices.
 */
ssize_t ioctx_read(ioctx_t *ctx, char *buf, size_t bytes);

/*
 * ioctx_cancel() causes any tasks blocked on I/O on the
 * given ioctx to be woken up with errno set to ECANCELED.
 * The call does not return until the blocked tasks have
 * had an opportunity to run. Note that the file descriptor 
 * is not closed, and tasks may immediately re-park themselves
 * on the fd. To permanently invalidate the ioctx, use 
 * ioctx_destroy().
 *
 */
void ioctx_cancel(ioctx_t *ctx);

#endif /* __CHIP_RUNTIME_H_ */
