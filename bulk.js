db.dropDatabase();
db.createCollection("test");

var session = new Mongo().startSession();

print(tojson(session.getDatabase("test").runCommand({
    bulkWrite: "admin",
    commands: [
        {
            insert: "test",
            $db: "test",
            documents: [{ _id: 1}, {_id: 1}, {_id:1}],
            ordered:false,
        },
        {
            insert: "test",
            $db: "test",
            documents: [{ _id: 1}, {_id: 1}, {_id:1}],
            ordered:true,
        },
    ],
    ordered: false,
})));
