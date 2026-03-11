CC ?= cc
CFLAGS ?= -O3 -Iinclude
SRC := $(wildcard src/*.c)
OUT := build/shivishell

.PHONY: all build test clean

all: build

build:
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $(OUT)
	@echo "Built: $(OUT)"

clean:
	@rm -rf build

test: build
	$(CC) $(CFLAGS) tests/test_main.c src/parse.c src/history.c -o build/tests
	@./build/tests
