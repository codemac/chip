#ifndef __CHIP_SYNC_H_
#define __CHIP_SYNC_H_
#include "chip.h"

/*
 * sema_t is a semaphore.
 */
typedef struct {
	tasklist_t list;
	int count;
} sema_t;

/* 
 * semacquire attempts to decrement
 * the semaphore counter but blocks
 * if decrementing the counter would
 * go below zero.
 */
void semacquire(sema_t *sema) {
	if (sema->count == 0) {
		wait(&sema->list);
	} else {
		--sema->count;
	}
}

/*
 * semrelease increments the semaphore
 * counter, or wakes someone waiting to
 * decrement the semaphore.
 */
void semrelease(sema_t *sema) {
	if (wake(&sema->list) == 0)
		++sema->count;
}

typedef struct {
	tasklist_t waiting;
	int locked;
} mutex_t;

void lock(mutex_t *mutex) {
	if (mutex->locked) {
		wait(&mutex->waiting);
	} else {
		mutex->locked = 1;
	}
}

void unlock(mutex_t *mutex) {
	mutex->locked = wake(&mutex->waiting);
}

#endif