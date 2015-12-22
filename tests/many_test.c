#include <stdio.h>
#include <assert.h>
#include <chip/chip.h>

/* 
 * spawn() this many tasks that all
 * contend on a lock, which exercises
 * task heap allocation/deallocation.
 * This is essentially worst-case behavior
 * in terms of pressure to allocate many
 * tasks.
 */
#define INCS 3000

static int count;
static sema_t sema;   /* 'done' semaphore */
static mutex_t ilock; /* lock to force many tasks to exist */

static void inc(word_t data) {
	lock(&ilock);
	if (++count == INCS) {
		post(&sema);
	}
        unlock(&ilock);
	return;
}

int main(void) {
	puts("running "__FILE__);
	lock(&ilock);
	
        /* 
	   with the lock held, start a bunch of tasks 
	   that contend on the lock.
	 */
	for (int i=0; i<INCS; ++i) {
		spawn(inc, NULL_ARG);
	}

	tsk_stats_t stats;
	get_tsk_stats(&stats);
	printf("after %d spawns, %d parked, %d free, %d queued\n", INCS, stats.parked, stats.free, stats.runnable);

	/* there should be INCS many pending tasks */
	assert(stats.parked+stats.runnable == INCS);
	
        unlock(&ilock);

	park(&sema);

	/* we waited for everything to end -- make sure this is the case. */
	get_tsk_stats(&stats);
	assert(stats.runnable == 0);
	assert(stats.parked == 0);

	printf("after unlock, %d parked, %d free, %d queued\n", stats.parked, stats.free, stats.runnable);
	
	assert(sema.count == 0);
	assert(count == INCS);
	puts(__FILE__ " passed.");
	return 0;
}
