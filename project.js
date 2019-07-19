db.test.drop();
db.test.insert({ foo: 1 });
db.test.insert({ foo: 2 });
db.test.insert({ foo: 3 });

load("src/mongo/scripting/project-wasm.js");

c = db.test.aggregate([{ $wasm: { wasm: BinData(0, wasm) } }]);
while (c.hasNext()) {
    print(tojson(c.next()));
}
