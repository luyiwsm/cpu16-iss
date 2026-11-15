CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
ASAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer -g
CORE = src/isa.c src/cpu.c src/coe_loader.c src/debugger.c
HEADERS = $(wildcard include/*.h)
TESTS = test_isa test_coe test_lockstep test_regression
ASAN_TESTS = $(addsuffix _asan,$(TESTS))

.PHONY: all test asan clean
all: cpu16_sim $(TESTS)

build results:
	mkdir -p $@

cpu16_sim: src/main.c $(CORE) $(HEADERS) | build results
	$(CC) $(CFLAGS) -o $@ src/main.c $(CORE)

$(TESTS): test_%: tests/test_%.c $(CORE) $(HEADERS) | build results
	$(CC) $(CFLAGS) -o $@ $< $(CORE)

cpu16_sim_asan: src/main.c $(CORE) $(HEADERS) | build results
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ src/main.c $(CORE)

$(ASAN_TESTS): test_%_asan: tests/test_%.c $(CORE) $(HEADERS) | build results
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ $< $(CORE)

test: all
	@set -e; for test in $(TESTS); do ./$$test; done
	python3 tests/test_cli.py

asan: cpu16_sim_asan $(ASAN_TESTS)
	@set -e; for test in $(ASAN_TESTS); do ./$$test; done
	python3 tests/test_cli.py ./cpu16_sim_asan

# Keep user programs such as build/add.asm.
clean:
	rm -f cpu16_sim cpu16_sim_asan $(TESTS) $(ASAN_TESTS)
