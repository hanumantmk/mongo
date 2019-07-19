#!/bin/sh
asc test.ts -b test.wasm -O3 --runtime=none
xxd --include test.wasm > iwasm_wasm.h
echo -n 'var wasm = "' > assembly_script.js
base64 -w0 test.wasm >> assembly_script.js
echo '";' >> assembly_script.js
