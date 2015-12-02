# NOTE: `make` is run from the build script, which detects host arch and OS

RUNTIME_HEADERS := runtime.h chip.h
RUNTIME_INCLUDES := runtime_$(POLLER).c runtime_$(ARCH).c
RUNTIME_SOURCE := runtime.c context_$(ARCH).s 

# trigger re-compilation when one of the headers changes
$(RUNTIME_SOURCE): $(RUNTIME_HEADERS) $(RUNTIME_INCLUDES)

# just compile/link tests in one go.
bin/%.test: tests/%_test.c $(RUNTIME_SOURCE)
	$(CC) $(CFLAGS) $^ -o $@

ARCHIVE := $(LIB_DIR)/libchip.a

# headers in /usr/lib/chip/*.h
CHIP_INCLUDE_DIR := $(INCLUDE_DIR)/chip

# make the archive depend upon the source
# directly so that we don't need to keep the
# archive around in this directory to prove
# that it is up-to-date.
$(ARCHIVE): $(RUNTIME_HEADERS) $(RUNTIME_SOURCE)
	$(CC) -c $(CFLAGS) $(RUNTIME_SOURCE)
	ar rcs libchip.a runtime.o context_$(ARCH).o
	mv libchip.a $@

$(CHIP_INCLUDE_DIR)/%.h: %.h
	mkdir -p $(CHIP_INCLUDE_DIR)
	cp $^ $@

INSTALLED_FILES := $(ARCHIVE) \
	$(CHIP_INCLUDE_DIR)/runtime.h \
	$(CHIP_INCLUDE_DIR)/chip.h \

.PHONY: install
install: $(INSTALLED_FILES) 

.PHONY: uninstall
uninstall:
	$(RM) $(INSTALLED_FILES)

# benchmarks link against the archive
bin/%.bench: tests/%_bench.c $(INSTALLED_FILES)
	$(CC) $< $(CFLAGS) -lchip -o $@

bin/%: tests/%.c $(INSTALLED_FILES)
	$(CC) $< $(CFLAGS) -lchip -o $@

.PHONY: clean
clean:
	$(RM) -r *.a *.o *.gch *.test *.bench *.out *.dSYM bin/*
