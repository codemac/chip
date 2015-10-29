# NOTE: `make` is run from the build script, which detects host arch and OS

CC := gcc
RUNTIME_HEADERS := runtime.h runtime_$(POLLER).h
RUNTIME_SOURCE := runtime.c context_$(ARCH).s

# trigger re-compilation when one of the headers changes
$(RUNTIME_SOURCE): $(RUNTIME_HEADERS)

# just compile/link tests in one go.
bin/%.test: tests/%_test.c $(RUNTIME_SOURCE)
	$(CC) $(CFLAGS) $^ -o $@

LIB_DIR := /usr/local/lib
INCLUDE_DIR := /usr/local/include/chip

ARCHIVE := $(LIB_DIR)/libchip.a

# make the archive depend upon the source
# directly so that we don't need to keep the
# archive around in this directory to prove
# that it is up-to-date.
$(ARCHIVE): $(RUNTIME_HEADERS) $(RUNTIME_SOURCE)
	$(CC) -c $(CFLAGS) $(RUNTIME_SOURCE)
	ar rcs libchip.a runtime.o context_$(ARCH).o
	mv libchip.a $@

$(INCLUDE_DIR)/%.h: %.h
	mkdir -p $(INCLUDE_DIR)
	cp $^ $@

INSTALLED_FILES := $(ARCHIVE) \
	$(INCLUDE_DIR)/runtime.h \
	$(INCLUDE_DIR)/chip.h \
	$(INCLUDE_DIR)/runtime_$(POLLER).h

.PHONY: install
install: $(INSTALLED_FILES) 

.PHONY: uninstall
uninstall:
	$(RM) $(INSTALLED_FILES)

# benchmarks use the library in /usr/lib, so
# they depend upon installation.
bin/%.bench: tests/%_bench.c $(INSTALLED_FILES)
	$(CC) -lchip $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	$(RM) -r *.a *.o *.gch *.test *.bench *.out *.dSYM bin/*
