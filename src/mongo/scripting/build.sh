#!/bin/sh
emcc -g -O2 -std=c++17 -I. test.cpp \
    -s WASM=1 \
    -s ASSERTIONS=1 \
    -s STACK_OVERFLOW_CHECK=2 \
    -s TOTAL_MEMORY=65536 \
    -s TOTAL_STACK=4096 \
    -s LINKABLE=1 \
    -s SIDE_MODULE=1 \
    -s EXPORTED_FUNCTIONS='["_mysq", "_mytransform", "_myfilter"]' \
    -o test.wasm
xxd --include test.wasm > iwasm_wasm.h
echo -n 'var wasm = "' > wasm.js
base64 -w0 test.wasm >> wasm.js
echo '";' >> wasm.js
