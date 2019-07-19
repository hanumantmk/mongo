db.test.drop();
db.test.insert({ name: "Younger Person", age: 25, email_address: "young@mongodb.com" });
db.test.insert({ name: "Older Person", age: 75, email_address: "old@mongodb.com" });
db.test.insert({ name: "Middle Person", age: 50, email_address: "middle@mongodb.com" });

load("src/mongo/scripting/stats-wasm.js");

c = db.test.aggregate([{ $wasm: { wasm: BinData(0, wasm) } }]);
while (c.hasNext()) {
    print(tojson(c.next()));
}
