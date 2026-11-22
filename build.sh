#!/bin/bash
set -e
clang-format -i src/*.c
clang-format -i tests/*.c
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure -V
