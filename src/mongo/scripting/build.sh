#!/bin/sh
EMCC_FORCE_STDLIBS=1 \
EMCC_ONLY_FORCED_STDLIBS=1 \
em++ -O3 -std=c++17 -I. test.cpp \
    -s WASM=1 \
    -s ASSERTIONS=1 \
    -s STACK_OVERFLOW_CHECK=2 \
    -s TOTAL_MEMORY=524288 \
    -s TOTAL_STACK=4096 \
    -s SIDE_MODULE=1 \
    -s EXPORTED_FUNCTIONS='["_getNext"]' \
    -o test.wasm
xxd --include test.wasm > iwasm_wasm.h
echo -n 'var wasm = "' > cpp.js
base64 -w0 test.wasm >> cpp.js
echo '";' >> cpp.js
    #-s TOTAL_MEMORY=65536 \
    #-s LINKABLE=1 \
    #-s USE_PTHREADS=1 \
    #-s MAIN_MODULE=2 \
    #-s SIDE_MODULE=1 \
    #EMCC_FORCE_STDLIBS=libc++,libc++abi,libpthreads,libc-wasm \
