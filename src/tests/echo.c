#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef __linux__
#include <fcntl.h>
#endif
#include "chip/chip.h"

#define please(expr) if ((expr) == -1) { perror(#expr); _exit(1); }

/* we have 12kB stacks, so this should fit fine. */
#define STACK_BUF_SIZE 8192

void echo(word_t arg0) {
	ioctx_t ctx;
	char buf[STACK_BUF_SIZE];

	please(ioctx_init(arg0.fd, &ctx));

	for (;;) {
		ssize_t ret;
		ssize_t res = ioctx_read(&ctx, buf, STACK_BUF_SIZE);
		switch (res) {
		case -1:
			perror("read");
		case 0:
			goto done;
		default:
			ret = 0;
			while (ret < res) {
				ssize_t w = ioctx_write(&ctx, buf + ret, res - ret);
				switch (w) {
				case -1:
					perror("write");
				case 0:
					goto done;
				default:
					ret += w;
				}
			}
		}
	}

done:
	please(ioctx_destroy(&ctx));
}

#define PORT 7070

int main(void) {
	int lfd;
	ioctx_t lctx;
	struct sockaddr addr;
	socklen_t addrlen;

#ifdef __linux__
	please(lfd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0));
#else
	please(lfd = socket(AF_INET, SOCK_STREAM, 0));
	fcntl(lfd, F_SETFL, O_NONBLOCK|O_CLOEXEC|fcntl(lfd, F_GETFL));
#endif

	struct sockaddr_in laddr;
	memset(&laddr, 0, sizeof(laddr));
	laddr.sin_family = AF_INET;
	laddr.sin_port = htons(PORT);
	laddr.sin_addr.s_addr = INADDR_ANY;

	please(bind(lfd, (const struct sockaddr *)&laddr, sizeof(laddr)));
	please(ioctx_init(lfd, &lctx));

	please(listen(lfd, 128));
	printf("listening on :%d...\n", PORT);
	for (;;) {
		word_t arg;
		please(arg.fd = ioctx_accept(&lctx, &addr, &addrlen));
		spawn(echo, arg);
	}
}
