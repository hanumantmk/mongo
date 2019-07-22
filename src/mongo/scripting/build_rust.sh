#!/bin/sh
cd ~/Git/mongo-wasm/examples/limit
cargo build --target wasm32-unknown-unknown --release

cd ~/Git/mongo-wasm/examples/passthrough
cargo build --target wasm32-unknown-unknown --release

cd ~/Git/mongo-wasm/examples/project
cargo build --target wasm32-unknown-unknown --release

cd ~/Git/mongo-wasm/examples/stats
cargo build --target wasm32-unknown-unknown --release

cd ~/Git/mongo/src/mongo/scripting

echo -n 'var wasm = "' > passthrough-wasm.js
wasm-gc --no-demangle ~/Git/mongo-wasm/target/wasm32-unknown-unknown/release/mongo_wasm_example_passthrough.wasm passthrough.wasm
base64 -w0 passthrough.wasm >> passthrough-wasm.js
echo '";' >> passthrough-wasm.js

echo -n 'var wasm = "' > limit-wasm.js
wasm-gc --no-demangle ~/Git/mongo-wasm/target/wasm32-unknown-unknown/release/mongo_wasm_example_limit.wasm limit.wasm
base64 -w0 limit.wasm >> limit-wasm.js
echo '";' >> limit-wasm.js

echo -n 'var wasm = "' > project-wasm.js
wasm-gc --no-demangle ~/Git/mongo-wasm/target/wasm32-unknown-unknown/release/mongo_wasm_example_project.wasm project.wasm
base64 -w0 project.wasm >> project-wasm.js
echo '";' >> project-wasm.js

echo -n 'var wasm = "' > stats-wasm.js
wasm-gc --no-demangle ~/Git/mongo-wasm/target/wasm32-unknown-unknown/release/mongo_wasm_example_stats.wasm stats.wasm
base64 -w0 stats.wasm >> stats-wasm.js
echo '";' >> stats-wasm.js
