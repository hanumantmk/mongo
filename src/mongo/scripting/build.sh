#!/bin/sh
emcc -g -O3 test.c -s WASM=1 -s SIDE_MODULE=1 -s ASSERTIONS=1 -s STACK_OVERFLOW_CHECK=2 -s TOTAL_MEMORY=65536 -s TOTAL_STACK=4096 -s LINKABLE=1 -s EXPORT_ALL=1 -o test.wasm
xxd --include test.wasm > iwasm_wasm.h
