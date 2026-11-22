#!/bin/bash
set -e
clang-format -i src/*.c src/*.h
clang-format -i tests/*.c 
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure -V
ctest --test-dir build --output-on-failure --no-tests=error \
  -R '^descriptor_race_preload$' --repeat until-fail:100