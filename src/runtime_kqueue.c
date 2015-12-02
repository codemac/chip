
static void io_unpark(task_t *t);
static int park_and_iowait(task_t **addr);

static int kqfd;
static struct kevent events[128];

static void pollinit(void) {
	kqfd = kqueue();
	if (kqfd == -1) {
		perror("kqueue");
		_exit(1);
	}
}

static int handle_events(int off, int num);

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
	int nev;
get_events:
	nev = kevent(kqfd, &events[0], 2, &events[2], 126, &zero);
	if (nev == -1) {
		if (errno == EINTR)
			goto get_events;

		return -1;
	}
	handle_events(2, 2+nev);

	ctx->fd = fd;
	ctx->writer = NULL;
	ctx->reader = NULL;
	return 0;
}

int ioctx_accept(ioctx_t *ctx, struct sockaddr *addr, socklen_t *addrlen) {
	int res;
try:
	res = accept(ctx->fd, addr, addrlen);
	if (res == -1) {
		switch (errno) {
		case EAGAIN:
			if (unlikely(ctx->reader))
				panic("concurrent calls to accept()");

			if (unlikely(park_and_iowait(&ctx->reader) < 0))
				return -1;
			
		case EINTR:
			goto try;
		default:
			return -1;
		}
	}
	
	int fl;
set_flags:
	fl = fcntl(res, F_SETFL, O_CLOEXEC|O_NONBLOCK|(fcntl(res, F_GETFL)));
	if (unlikely(fl == -1)) {
		if (errno == EINTR)
			goto set_flags;

		int saved = errno;
		do {
			fl = close(res);
		} while (fl == -1 && errno == EINTR);
		errno = saved;
		return -1;
	}
	return res;
}

int ioctx_destroy(ioctx_t *ctx) {
do_close:
	if (close(ctx->fd) == -1) {
		if (errno == EINTR)
			goto do_close;

		return -1;
	}

	ctx->fd = -1;
	ioctx_cancel(ctx);
	return 0;
}

static void poll(int ms) {
	struct timespec *t = NULL;
	struct timespec ts;
	if (ms != -1) {
		ts.tv_nsec = ms * 1000000;
		ts.tv_sec = 0;
		t = &ts;
	}

	int nev;
kevent_wait:
	nev = kevent(kqfd, NULL, 0, &events[0], 128, t);
	if (nev == -1) {
		switch (errno) {			
		case EINTR:
			goto kevent_wait;
		default:
			perror("kevent");
			_exit(1);
		}
	}
	if (handle_events(0, nev) == 0 && ms == -1)
		goto kevent_wait;
}

static int handle_events(int off, int num) {
	int woke = 0;
	for (int i=off; i<num; ++i) {
		struct kevent *ev = &events[i];
		ioctx_t *ctx = (ioctx_t *)ev->udata;
		switch (ev->filter) {
		case EVFILT_WRITE:
			if (ctx->writer) {
				io_unpark(ctx->writer);
				++woke;
			}
			break;
		case EVFILT_READ:
			if (ctx->reader) {
				io_unpark(ctx->reader);
				++woke;
			}
			break;
		}
	}
	return woke;
}
