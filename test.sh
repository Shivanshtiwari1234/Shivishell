#!/usr/bin/env bash
set -e
mkdir -p build
cc -O2 -Iinclude tests/test_main.c src/parse.c src/history.c -o build/tests
./build/tests
