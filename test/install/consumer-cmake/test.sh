#!/bin/sh
set -e

echo "Testing CMake find_package(lace)..."

cmake -S . -B build
cmake --build build

echo "Running binary..."
./build/test-lace

echo "CMake consumer test PASSED"