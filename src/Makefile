CC := clang
CFLAGS := -O3 -Wall -Werror
ARCH := amd64

%.test: tests/%.c chip.c context_$(ARCH).s
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: test
test: test.test
	./test.test

.PHONY: clean
	$(RM) *.o *.gch *.test *.out
