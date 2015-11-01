#include "runtime.h"
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/event.h>

static void unpark(task_t *t);

static int kqfd;

static struct kevent events[64];

void pollinit(void) {
	kqfd = kqueue();
	if (kqfd == -1) {
		perror("kqueue");
		_exit(1);
	}
}

static void handle_events(int off, int num);

int ioctx_init(int fd, ioctx_t *ctx) {
	events[0].ident = fd;
	events[0].filter = EVFILT_WRITE;
	events[0].udata = ctx;
	events[0].flags = EV_CLEAR|EV_ENABLE|EV_ADD;
	events[0].fflags = 0;

	events[1].ident = fd;
	events[1].filter = EVFILT_READ;
	events[1].udata = ctx;
	events[1].flags = EV_CLEAR|EV_ENABLE|EV_ADD;
	events[1].fflags = 0;

	struct timespec zero = { 0, 0 };
	int nev = kevent(kqfd, &events[0], 2, &events[2], 62, &zero);
	if (nev == -1) return -1;
	handle_events(2, 2+nev);

	ctx->fd = fd;
	ctx->writer = NULL;
	ctx->reader = NULL;
	return 0;
}

int ioctx_destroy(ioctx_t *ctx) {
	return close(ctx->fd);
}

void poll(int ms) {
	struct timespec *t = NULL;
	struct timespec ts;
	if (ms != -1) {
		ts.tv_nsec = ms * 1000000;
		ts.tv_sec = 0;
		t = &ts;
	}
	int nev = kevent(kqfd, NULL, 0, &events[0], 64, t);
	assert(nev != -1);
	handle_events(0, nev);
}

static void handle_events(int off, int num) {
	for (int i=off; i<num; ++i) {
		struct kevent *ev = &events[i];
		ioctx_t *ctx = (ioctx_t *)ev->udata;
		switch (ev->filter) {
		case EVFILT_WRITE:
			if (ctx->writer) unpark(ctx->writer);
			break;
		case EVFILT_READ:
			if (ctx->reader) unpark(ctx->reader);
			break;
		}
	}
}
