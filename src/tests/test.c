#include <stdio.h>
#include "../chip.h"

static void sayhi(void *data) {
	puts("in sayhi(str)");
	puts((char *)data);
}

int taskmain(void) {
	/* 
	 * so, this is unfortunate.
	 * on OSX, the first call to puts()
	 * needs to run on the system stack,
	 * because it is dynamically linked,
	 * and segfaults when run in a task.
	 */
	puts("in taskmain()");
	char *str = "hello, world!";
	spawn(sayhi, str);
	sched(); /* should jump to sayhi */
	puts("back in taskmain()");
	return 0;
}
