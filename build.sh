#!/usr/bin/env bash
set -e
mkdir -p build
gcc -O3 -Iinclude src/*.c -o build/shivishell
echo "Built: build/shivishell"
