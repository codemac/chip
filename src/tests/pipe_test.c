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

#define please(expr) if ((expr) == -1) { perror(#expr); _exit(1); }

#define NUM_BYTES (1<<22)

void pipe_writer(word_t data) {
	ioctx_t ctx;
	tsk_stats_t stats;
	int wfd = ((int *)data.ptr)[1];
	ssize_t amt = 0;
	ssize_t this;
	ssize_t w;
	int zero;

	please(ioctx_init(wfd, &ctx));
	please(zero = open("/dev/zero", O_RDONLY));

	while (amt < NUM_BYTES) {
		please(this = read(zero, write_buf, 4096));
		get_tsk_stats(&stats);
		please(w = ioctx_write(&ctx, write_buf, this));
		amt += w;
	}

	please(ioctx_destroy(&ctx));
	puts("write ok.");
	return;
}

void pipe_reader(word_t data) {
	ioctx_t ctx;
	tsk_stats_t stats;
	ssize_t this;
	ssize_t amt = 0;
	int rfd = ((int *)data.ptr)[0];
	
	please(ioctx_init(rfd, &ctx));
	
	for (;;) {
		/* have the runtime perform a sanity check */
		get_tsk_stats(&stats);
		please(this = ioctx_read(&ctx, read_buf, 4096));
		if (this == 0) {
			break;
		}
		amt += this;
	}
	
	assert(amt == NUM_BYTES);
	please(ioctx_destroy(&ctx));
	puts("read ok.");
	post(&rdsema);
	return;
}

int main(void) {
	puts("running pipe tests...");

	int pipefd[2];
	word_t arg0;
	arg0.ptr = pipefd;
#ifdef __gnu_linux__
	please(pipe2(pipefd, O_NONBLOCK|O_CLOEXEC));
#else
	please(pipe(pipefd));
	fcntl(pipefd[0], F_SETFL, O_NONBLOCK|(fcntl(pipefd[0], F_GETFL)));
	fcntl(pipefd[1], F_SETFL, O_NONBLOCK|(fcntl(pipefd[1], F_GETFL)));
#endif

	spawn(pipe_writer, arg0);
	spawn(pipe_reader, arg0);

	park(&rdsema); /* wait for reads to complete */
	puts(__FILE__ " passed.");
	return 0;
}
