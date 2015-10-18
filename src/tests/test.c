#include <stdio.h>
#include <time.h>
#include <assert.h>
#include "../chip.h"
#include "../chipsync.h"

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
		semrelease(&sema);
	}
	return;
}

int taskmain(void) {
	/* 
	 * so, this is unfortunate.
	 * on OSX, the first call to puts()
	 * needs to run on the system stack,
	 * because it is dynamically linked,
	 * and segfaults when run in a task.
	 */
	puts("running tests...");

	/* no tasks to run -- should return immediately */
	sched();

	/* spawn 100 tasks that run 'inc' */
	for (int i=0; i<INCS; ++i) {
		spawn(inc, NULL);
	}
	clock_t t = clock();
	semacquire(&sema);
	t = clock() - t;
	assert(sema.count == 0);
	assert(count == INCS);
	printf("%d scheduler hops in %ld clocks.\n", INCS, t);
	puts("tests OK.");
	return 0;
}
