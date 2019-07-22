#!/bin/sh
emcc -O3 -I. duk.c json2bson.cpp \
    -s WASM=1 \
    -s ASSERTIONS=1 \
    -s STACK_OVERFLOW_CHECK=2 \
    -s TOTAL_MEMORY=1073741824 \
    -s TOTAL_STACK=524288 \
    -s SIDE_MODULE=1 \
    -s LINKABLE=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS='["_getNext"]' \
    -o duk.wasm
xxd --include duk.wasm > duk_wasm.h
echo -n 'var wasm = "' > duk_wasm.js
base64 -w0 duk.wasm >> duk_wasm.js
echo '";' >> duk_wasm.js
    #-s TOTAL_MEMORY=65536 \
    #-s TOTAL_MEMORY=524288 \
    #-s TOTAL_STACK=4096 \
    #-s LINKABLE=1 \
    #-s USE_PTHREADS=1 \
    #-s MAIN_MODULE=2 \
    #-s SIDE_MODULE=1 \
    #EMCC_FORCE_STDLIBS=libc++,libc++abi,libpthreads,libc-wasm \
