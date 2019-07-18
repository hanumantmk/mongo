db.test.drop();
db.test.insert({x:1});

load("src/mongo/scripting/passthrough-wasm.js");

print(tojson(db.test.aggregate([{$wasm: {wasm: BinData(0, wasm)} }]).next()));
