CC := clang
CFLAGS := -O3 -Wall -Werror -mpopcnt
TSTFLAGS := -g -fno-omit-frame-pointer
ARCH := amd64

%.test: tests/%.c runtime.c context_$(ARCH).s
	$(CC) $(CFLAGS) $(TSTFLAGS) $^ -o $@

%.bench: tests/%.c runtime.c context_$(ARCH).s
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: test
test: test.test
	./test.test

.PHONY: bench
bench: pingpong.bench
	./pingpong.bench

.PHONY: clean
clean:
	$(RM) -r *.o *.gch *.test *.bench *.out *.dSYM
