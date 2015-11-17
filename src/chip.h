#ifndef __CHIP_H_
#define __CHIP_H_
#include "runtime.h"
#include <assert.h>

/*
 * sema_t is a semaphore.
 */
typedef struct {
	tasklist_t list;
	int        count;
} sema_t;

/* 
 * park attempts to decrement
 * the semaphore counter but blocks
 * if decrementing the counter would
 * go below zero.
 */
void park(sema_t *sema) {
	if (sema->count == 0) {
		wait(&sema->list);
	} else {
		--sema->count;
	}
}

/*
 * post increments the semaphore
 * counter, or wakes someone waiting to
 * decrement the semaphore.
 */
void post(sema_t *sema) {
	if (!wake(&sema->list))
		++sema->count;
}

typedef struct {
	tasklist_t waiting;
	int        locked;
} mutex_t;

void lock(mutex_t *mutex) {
	if (mutex->locked) {
		wait(&mutex->waiting);
	} else {
		mutex->locked = 1;
	}
}

void unlock(mutex_t *mutex) {
	assert(mutex->locked);
	mutex->locked = wake(&mutex->waiting);
}

#endif
