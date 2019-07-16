db.test.drop();
db.test.insert({x:1});

load("src/mongo/scripting/wasm.js");

print(tojson(db.test.aggregate([{$wasmt: {wasm: BinData(0, wasm), func: "_mytransform"}}]).next()));
