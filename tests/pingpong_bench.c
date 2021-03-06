#include <time.h>
#include <stdio.h>
#include <chip/chip.h>

void pong(word_t arg) {
	for (;;) {
		sched();
	}
}

#define PONGS 10000000

int main(void) {
	puts("starting ping-pong test...");

	word_t arg;
	arg.val = 0;
	spawn(pong, arg);
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
