db.test.drop();
db.test.insert({x:1});

load("src/mongo/scripting/cpp.js");

c = db.test.aggregate([{ $wasm: { wasm: BinData(0, wasm) } }]);
while (c.hasNext()) {
    print(tojson(c.next()));
}
