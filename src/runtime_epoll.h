#include "runtime.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

static void unpark(task_t *t);

static int epfd;

static struct epoll_event events[64];

void pollinit(void) {
	epfd = epoll_create1(EPOLL_CLOEXEC);
	if (epfd == -1) {
		perror("epoll_creat1");
		_exit(1);
	}
}

int ioctx_init(int fd, ioctx_t *ctx) {
	events[0].data.ptr = ctx;
	events[0].events = EPOLLERR|EPOLLET|EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLHUP;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &events[0]) < 0)
		return -1;

	ctx->fd = fd;
	ctx->writer = NULL;
	ctx->reader = NULL;
	return 0;
}

int ioctx_destroy(ioctx_t *ctx) {
	if (epoll_ctl(epfd, EPOLL_CTL_DEL, ctx->fd, NULL) < 0) 
		return -1;
	
	return close(ctx->fd);
}

void poll(int ms) {
	int nev = epoll_wait(epfd, &events[0], 64, ms);
	if (nev == -1) {
		perror("epoll_wait");
		_exit(1);
	}
	
	for (int i=0; i<nev; ++i) {
		struct epoll_event *ev = &events[i];
		ioctx_t *ctx = (ioctx_t *)ev->data.ptr;

		if (ctx->reader && (ev->events&(EPOLLIN|EPOLLERR|EPOLLRDHUP|EPOLLHUP)))
			unpark(ctx->reader);
		
		if (ctx->writer && (ev->events&(EPOLLOUT|EPOLLERR)))
			unpark(ctx->writer);
		
	}
}
