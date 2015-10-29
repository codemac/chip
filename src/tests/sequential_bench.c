#include <stdio.h>
#include <time.h>
#include <assert.h>
#include "../chip.h"

/* 
 * spawn more than MAXTASKS
 * tasks, just to get some
 * coverage of task exhaustion.
 */
#define INCS 1000000

static int count;
static sema_t sema;

static void stkchk(void *nothing) {
	assert(stack_remaining() > 8000);
}

static void inc(void *data) {
	if (++count == INCS) {
		post(&sema);
	}
	return;
}

int taskmain(void) {
	puts("running sequential stack switch test...");

	for (int i=0; i<1024; ++i) {
		spawn(stkchk, NULL);
	}
	sched();
	
	/* spawn tasks that run 'inc' */
	clock_t t = clock();
	for (int i=0; i<INCS; ++i) {
		spawn(inc, NULL);
	}
	park(&sema);
	t = clock() - t;
	assert(sema.count == 0);
	assert(count == INCS);
	double cpp = ((double)t)/((double)INCS);
	printf("%d stack switches in %ld clocks\n", INCS, t);
	printf("%f clocks per stack switch\n", cpp);
	return 0;
}
