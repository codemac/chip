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

void pipe_canceler(void *data) {
	ioctx_t *ctx = data;
	ioctx_cancel(ctx);
}

void pipe_writer(void *data) {
	ioctx_t *ctx = data;
	tsk_stats_t stats;
	ssize_t amt = 0;
	ssize_t this;
	ssize_t w;
	int zero;

	please(zero = open("/dev/zero", O_RDONLY));

	/* as soon as we block, the cancel-er will be spawned */
	spawn(pipe_canceler, ctx);
	
	do {
		please(this = read(zero, write_buf, 4096));
		get_tsk_stats(&stats);
	        w = ioctx_write(ctx, write_buf, this);
		if (w == -1) {
			if (errno == ECANCELED) {
				puts("write got ECANCELED");
				continue;
			} else {
				perror("write"); _exit(1);
			}
		}
		amt += w;
	} while (amt < NUM_BYTES);


	please(ioctx_destroy(ctx));
	puts("write ok.");
	return;
}

void pipe_reader(void *data) {
	ioctx_t *ctx = data;
	tsk_stats_t stats;
	ssize_t this;
	ssize_t amt = 0;
	
	spawn(pipe_canceler, ctx);

	
	for (;;) {
		/* have the runtime perform a sanity check */
		get_tsk_stats(&stats);
	rd:
		this = ioctx_read(ctx, read_buf, 4096);
		switch (this) {
		case 0:
			goto done;
		case -1:
			if (errno == ECANCELED) {
				puts("read got ECANCELED");
				goto rd;
			} else {
				perror("read"); _exit(1);
			}
		default:
			amt += this;
			break;
		}
	}
done:
	assert(amt == NUM_BYTES);
	please(ioctx_destroy(ctx));
	puts("read ok.");
	post(&rdsema);
	return;
}

int main(void) {
	puts("running pipe cancelation tests...");

	int pipefd[2];
#ifdef __gnu_linux__
	please(pipe2(pipefd, O_NONBLOCK|O_CLOEXEC));
#else
	please(pipe(pipefd));
	fcntl(pipefd[0], F_SETFL, O_NONBLOCK|(fcntl(pipefd[0], F_GETFL)));
	fcntl(pipefd[1], F_SETFL, O_NONBLOCK|(fcntl(pipefd[1], F_GETFL)));
#endif

	ioctx_t r_ctx;
	ioctx_t w_ctx;
	please(ioctx_init(pipefd[0], &r_ctx));
	please(ioctx_init(pipefd[1], &w_ctx));
	
	spawn(pipe_writer, &w_ctx);
	spawn(pipe_reader, &r_ctx);

	park(&rdsema); /* wait for reads to complete */
	puts(__FILE__ " passed.");
	return 0;
}
