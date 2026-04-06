#!/bin/sh
set -e

echo "Testing static linking with pkg-config --static..."

echo "CFLAGS:"
pkg-config --cflags --static lace

echo "LIBS:"
pkg-config --libs --static lace

echo "Compiling with static link..."
cc main.c $(pkg-config --cflags --static lace) $(pkg-config --libs --static lace) -o test

echo "Running binary..."
./test

echo "pkg-config consumer test PASSED"