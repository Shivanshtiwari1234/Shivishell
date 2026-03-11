#!/usr/bin/env bash
set -e
mkdir -p build
cc -O3 -Iinclude src/*.c -o build/shivishell
echo "Built: build/shivishell"
