conn = new Mongo();

db = conn.getDB("test");

db.test.drop();

for (i = 0; i < 100; i++) {
    db.test.insert({ "key" : i % 5, "val" : i + (i % 5)});
}

db.test.aggregate([
    {
        $group: {
            _id: "$key",
            sum: {
                $sum : "$val"
            },
            negativeSum: {
                $negativeSum : "$val"
            }
        }
    }
]).forEach(printjson);
