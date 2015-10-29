#include <time.h>
#include <stdio.h>
#include "../chip.h"

void pong(void* nothing) {
	for (;;) {
		sched();
	}
}

#define PONGS 10000000

int taskmain(void) {
	puts("starting ping-pong test...");

	spawn(pong, NULL);
	clock_t start = clock();
	for (int i=0; i<PONGS; ++i) {
		sched();
	}
	clock_t end = clock();
 	unsigned long cycles = end-start;
	double cpp = ((double)cycles)/((double)PONGS);
	printf("%d ping-pongs in %ld clocks\n", PONGS, cycles);
	printf("%f clocks per ping-pong\n", cpp);

	tsk_stats_t stats;
	get_tsk_stats(&stats);
	assert(stats.parked == 0);
	return 0;
}
