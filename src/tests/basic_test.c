#include <stdio.h>
#include <assert.h>
#include "../chip.h"

/* 
 * spawn more than MAXTASKS
 * tasks, just to get some
 * coverage of task exhaustion.
 */
#define INCS 3000

static int count;
static sema_t sema;

static void inc(void *data) {
	if (++count == INCS) {
		tsk_stats_t stats;
		get_tsk_stats(&stats);
		assert(stats.parked == 1);
		post(&sema);
		assert(stack_remaining() > 8000);
	}
	return;
}

int main(void) {
	/* 
	 * so, this is unfortunate.
	 * on OSX, the first call to puts()
	 * needs to run on the system stack,
	 * because it is dynamically linked,
	 * and segfaults when run in a task.
	 */
	puts("running basic tests...");

	tsk_stats_t stats;

	get_tsk_stats(&stats);

	assert(stats.parked == 0);
	assert(stats.runnable == 0);
	
	/* no tasks to run -- should return immediately */
	sched();

	assert(stack_remaining() > 8192);
	
	/* spawn tasks that run 'inc' */
	for (int i=0; i<INCS; ++i) {
		spawn(inc, NULL);
	}
	get_tsk_stats(&stats);
	printf("after %d spawns, %d parked, %d free, %d runnable\n", INCS, stats.parked, stats.free, stats.runnable);
	
	park(&sema);

	/* we waited for everything to end -- make sure this is the case. */
	get_tsk_stats(&stats);
	assert(stats.runnable == 0);
	assert(stats.parked == 0);
	
	assert(sema.count == 0);
	assert(count == INCS);
	puts(__FILE__ " passed.");
	return 0;
}
