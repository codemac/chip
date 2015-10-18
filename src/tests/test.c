#include <stdio.h>
#include "../chip.h"

/* 
 * spawn more than MAXTASKS
 * tasks, just to get some
 * coverage of task exhaustion.
 */
#define INCS 2000

static int count;
static tasklist_t adds;

static void inc(void *data) {
	if (++count == INCS) {
		wake(&adds);
	}
	return;
}

int taskmain(void) {
	int failed = 0;
	/* 
	 * so, this is unfortunate.
	 * on OSX, the first call to puts()
	 * needs to run on the system stack,
	 * because it is dynamically linked,
	 * and segfaults when run in a task.
	 */
	puts("running tests...");

	/* spawn 100 tasks that run 'inc' */
	for (int i=0; i<INCS; ++i) {
		spawn(inc, NULL);
	}
	wait(&adds);
	if (count != INCS) {
		printf("expected %d incs; found %d...\n", INCS, count);
		failed = 1;
	}
	return failed;
}
