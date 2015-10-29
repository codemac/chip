CC := gcc

bin/%.test: tests/%_test.c runtime.c context_$(ARCH).s
	$(CC) $(CFLAGS) $^ -o $@

bin/%.bench: tests/%_bench.c runtime.c context_$(ARCH).s
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean
clean:
	$(RM) -r *.o *.gch *.test *.bench *.out *.dSYM bin/*
