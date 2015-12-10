#include <stdio.h>
#include <time.h>
#include <assert.h>
#include "chip/chip.h"

/* 
 * spawn more than MAXTASKS
 * tasks, just to get some
 * coverage of task exhaustion.
 */
#define INCS 1000000

static int count;
static sema_t sema;

/* 'recursive' iteration through spawn() */
static void inc(word_t ignored) {
	if (++count != INCS) {
		spawn(inc, NULL_ARG);
	} else {
		post(&sema);
	}
	return;
}

int main(void) {
	puts("running sequential stack switch test...");
	
	/* spawn tasks that run 'inc' */
	clock_t t = clock();
	spawn(inc, NULL_ARG);
	park(&sema);
	t = clock() - t;
	assert(sema.count == 0);
	assert(count == INCS);
	double cpp = ((double)t)/((double)INCS);
	printf("%d recursive spawns in %ld clocks\n", INCS, t);
	printf("%f clocks per spawn\n", cpp);
	return 0;
}
