# CI-003: Nava simulator harness build system.
# Produces one test binary per tests/test_*.c, all linked against:
#   libsimavr.a + hd44780.o + harness/*.o + fixtures/*.o + test_runner.o
#
# Usage:
#   make          — build all test binaries
#   make test     — build and run all tests (TAP output)
#   make clean    — remove build/

REPO_ROOT := $(realpath ..)
SIMAVR    := $(CURDIR)/simavr
BUILD     := $(CURDIR)/build
ELF       := $(REPO_ROOT)/.pio/build/nava_sysex/firmware.elf

# libelf is keg-only; resolve its prefix at make time so -I/-L are explicit.
LIBELF_PREFIX := $(shell brew --prefix libelf 2>/dev/null)

CC        := clang
CFLAGS    := -arch arm64 -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
             -I$(SIMAVR)/simavr/sim \
             -I$(SIMAVR)/examples/parts \
             -I$(CURDIR)/harness \
             -I$(CURDIR)/fixtures \
             -I$(LIBELF_PREFIX)/include \
             -I$(LIBELF_PREFIX)/include/libelf \
             -DNAVA_ELF_PATH=\"$(ELF)\"
# Opt-in diagnostics, e.g. make -C sim CFLAGS_EXTRA=-DNAVA_TWI_DEBUG
CFLAGS    += $(CFLAGS_EXTRA)
LDFLAGS   := -arch arm64 \
             -L$(LIBELF_PREFIX)/lib \
             $(SIMAVR)/lib/libsimavr.a \
             $(BUILD)/hd44780.o \
             -lelf

HARNESS_SRCS := $(wildcard harness/*.c)
HARNESS_OBJS := $(patsubst harness/%.c,$(BUILD)/harness_%.o,$(HARNESS_SRCS))

FIXTURE_SRCS := $(wildcard fixtures/*.c)
FIXTURE_OBJS := $(patsubst fixtures/%.c,$(BUILD)/fixture_%.o,$(FIXTURE_SRCS))

RUNNER_OBJ   := $(BUILD)/test_runner.o

# test_runner.c is the framework (no main()) — it links INTO each binary as
# $(RUNNER_OBJ), so it must not itself be treated as a test binary source.
TEST_SRCS    := $(filter-out tests/test_runner.c,$(wildcard tests/test_*.c))
TEST_BINS    := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRCS))

.PHONY: all test clean

all: $(TEST_BINS)

$(BUILD):
	mkdir -p $@

# Harness objects
$(BUILD)/harness_%.o: harness/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Fixture objects
$(BUILD)/fixture_%.o: fixtures/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Test runner object
$(RUNNER_OBJ): tests/test_runner.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# HD44780 example part, built from the vendored simavr checkout.
# `make clean` removes build/, so this must be rebuildable WITHOUT re-running
# setup_simavr.sh — otherwise a plain `make` after `clean` fails on a missing
# hd44780.o.  -Dprintf silences the part's per-byte stdout chatter, which would
# otherwise interleave with and corrupt the tests' TAP output.
$(BUILD)/hd44780.o: $(SIMAVR)/examples/parts/hd44780.c | $(BUILD)
	$(CC) -arch arm64 -O2 -Dprintf=nava_hd44780_quiet \
	    -I$(LIBELF_PREFIX)/include -I$(LIBELF_PREFIX)/include/libelf \
	    -I$(SIMAVR)/simavr/sim -I$(SIMAVR)/examples/parts \
	    -c $< -o $@

# Per-test binaries
$(BUILD)/test_%: tests/test_%.c $(HARNESS_OBJS) $(FIXTURE_OBJS) $(RUNNER_OBJ) \
                 $(BUILD)/hd44780.o | $(BUILD)
	$(CC) $(CFLAGS) $< $(HARNESS_OBJS) $(FIXTURE_OBJS) $(RUNNER_OBJ) -o $@ $(LDFLAGS)

# Run each test binary; each emits TAP
test: $(TEST_BINS)
	@total=0; failed=0; \
	for bin in $(TEST_BINS); do \
	    echo "# Running $$bin"; \
	    $$bin; rc=$$?; total=$$((total+1)); \
	    if [ $$rc -ne 0 ]; then failed=$$((failed+1)); fi; \
	done; \
	echo ""; \
	echo "# $$total test binary/binaries run, $$failed failed"; \
	exit $$failed

clean:
	rm -rf $(BUILD)
