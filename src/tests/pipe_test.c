#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include "../chip.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static sema_t rdsema;

static char write_buf[4096];

static char read_buf[4096];

#define NUM_BYTES (1<<22)

void pipe_writer(void *data) {
	int wfd = ((int *)data)[1];
	ioctx_t ctx;
	assert(ioctx_init(wfd, &ctx) == 0);
	tsk_stats_t stats;

	int rand = open("/dev/zero", O_RDONLY);
	if (rand == -1) {
		perror("open(\"/dev/urandom\")");
		_exit(1);
	}

	ssize_t amt = 0;
	while (amt < NUM_BYTES) {
		ssize_t this = read(rand, write_buf, 4096);
		if (this == -1) {
			perror("read(\"/dev/zero\")");
			_exit(1);
		}
		get_tsk_stats(&stats);
		ssize_t w = ioctx_write(&ctx, write_buf, this);
		if (w == -1) {
			perror("write(pipe)");
			_exit(1);
		}
		amt += w;
	}

	assert(ioctx_destroy(&ctx) == 0);
	puts("write ok.");
	return;
}

void pipe_reader(void *data) {
	int rfd = ((int *)data)[0];
	ioctx_t ctx;
	assert(ioctx_init(rfd, &ctx) == 0);
	tsk_stats_t stats;
	ssize_t amt = 0;
	for (;;) {
		/* have the runtime perform a sanity check */
		get_tsk_stats(&stats);

		ssize_t this = ioctx_read(&ctx, read_buf, 4096);
		if (this == -1) {
			perror("read(pipe)");
			_exit(1);
		}
		if (this == 0) {
			break;
		}
		amt += this;
	}
	assert(amt == NUM_BYTES);
	assert(ioctx_destroy(&ctx) == 0);
	puts("read ok.");
	post(&rdsema);
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
	puts("running pipe tests...");

	int pipefd[2];
	int ok;
#ifdef __gnu_linux__
	ok = pipe2(pipefd, O_NONBLOCK|O_CLOEXEC);
	if (ok == -1) {
		perror("pipe2");
		_exit(1);
	}
#else
	ok = pipe(pipefd);	
	if (ok == -1) {
		perror("pipe");
		_exit(1);
	}
	fcntl(pipefd[0], F_SETFL, O_NONBLOCK|(fcntl(pipefd[0], F_GETFL)));
	fcntl(pipefd[1], F_SETFL, O_NONBLOCK|(fcntl(pipefd[1], F_SETFL)));
#endif

	spawn(pipe_writer, pipefd);
	spawn(pipe_reader, pipefd);

	park(&rdsema); /* wait for reads to complete */
	puts("tests OK.");
	return 0;
}
