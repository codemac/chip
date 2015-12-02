
static void io_unpark(task_t *t);
static int park_and_iowait(task_t **addr);

static int epfd;
static struct epoll_event events[128];

void pollinit(void) {
create:
	epfd = epoll_create1(EPOLL_CLOEXEC);
	if (epfd == -1) {
		if (errno == EINTR)
			goto create;
			
		perror("epoll_creat1");
		_exit(1);
	}
}

int ioctx_init(int fd, ioctx_t *ctx) {
	events[0].data.ptr = ctx;
	events[0].events = EPOLLERR|EPOLLET|EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLHUP;

again:
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &events[0]) < 0) {
		if (errno == EINTR)
			goto again;

		return -1;
	}


	ctx->fd = fd;
	ctx->writer = NULL;
	ctx->reader = NULL;
	return 0;
}

int ioctx_destroy(ioctx_t *ctx) {
epoll_del:
	if (epoll_ctl(epfd, EPOLL_CTL_DEL, ctx->fd, NULL) < 0) {
		if (errno == EINTR)
			goto epoll_del;
		
		return -1;
	}
fd_close:
	if (close(ctx->fd) == -1) {
		if (errno == EINTR)
			goto fd_close;
		
		return -1;
	}
	ctx->fd = -1;
	
	/* don't leak tasks parked on this ioctx */
	ioctx_cancel(ctx);
	return 0;
}

int ioctx_accept(ioctx_t *ctx, struct sockaddr *addr, socklen_t *addrlen) {
	int res;
try:
	res = accept4(ctx->fd, addr, addrlen, SOCK_CLOEXEC|SOCK_NONBLOCK);
	if (res == -1) {
		switch (errno) {
		case EAGAIN:
			if (unlikely(ctx->reader))
				panic("concurrent calls to accept()");

			if (unlikely(park_and_iowait(&ctx->reader) < 0))
				return -1;

		case EINTR:
			goto try;
		}
	}
	return res;
}

static void poll(int ms) {
	int nev;
entry:
	nev = epoll_wait(epfd, &events[0], 128, ms);
	if (nev == -1) {
		switch (errno) {
		case EINTR:
			goto entry;
		default:
			perror("epoll_wait");
			_exit(1);
		}
	}
	
	int woke = 0;
	for (int i=0; i<nev; ++i) {
		struct epoll_event *ev = &events[i];
		ioctx_t *ctx = (ioctx_t *)ev->data.ptr;

		if (ctx->reader && (ev->events&(EPOLLIN|EPOLLERR|EPOLLRDHUP|EPOLLHUP))) {
			io_unpark(ctx->reader);
			woke++;
		}
		
		if (ctx->writer && (ev->events&(EPOLLOUT|EPOLLERR))) {
			io_unpark(ctx->writer);
			woke++;
		}
	}
	/*
	  We have to handle the case in which the
	  user has initialized some (perhaps many)
	  different fds, but is not waiting on many 
	  of them, and has nonetheless managed to park
	  all of the tasks.
	 */
	if (woke == 0 && ms == -1)
		goto entry;
}
