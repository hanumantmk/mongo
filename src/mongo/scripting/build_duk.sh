#!/bin/sh
emcc -O3 -I. duk.c \
    -s WASM=1 \
    -s ASSERTIONS=1 \
    -s STACK_OVERFLOW_CHECK=2 \
    -s TOTAL_MEMORY=1073741824 \
    -s TOTAL_STACK=524288 \
    -s SIDE_MODULE=1 \
    -s LINKABLE=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS='["_mysq", "_mytransform", "_myfilter"]' \
    -o test.wasm
xxd --include test.wasm > iwasm_wasm.h
echo -n 'var wasm = "' > wasm.js
base64 -w0 test.wasm >> wasm.js
echo '";' >> wasm.js
    #-s TOTAL_MEMORY=65536 \
    #-s TOTAL_MEMORY=524288 \
    #-s TOTAL_STACK=4096 \
    #-s LINKABLE=1 \
    #-s USE_PTHREADS=1 \
    #-s MAIN_MODULE=2 \
    #-s SIDE_MODULE=1 \
    #EMCC_FORCE_STDLIBS=libc++,libc++abi,libpthreads,libc-wasm \
