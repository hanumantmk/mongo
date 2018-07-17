load("jstests/libs/parallelTester.js");

db.dropDatabase();
db.createCollection("test");
db.test.insert({key:1, val:1});

var threadA = new Thread(function(){
    var session = new Mongo().startSession();
    sleep(1000);

    print(tojson(session.getDatabase("test").runCommand({
        bulkWrite: "admin",
        writeConcern: {w: "majority"},
        startTransaction: true,
        autocommit: true,
        txnNumber: NumberLong(1),
        commands: [
            {
                update: "test",
                $db: "test",
                updates: [{q: {key: 1}, u: {key: 1, val: 3}, multi: false}]
            },
        ],
    })));
});

var threadB = new Thread(function(){
    var session = new Mongo().startSession();
    session.startTransaction();

    session.getDatabase("test").test.update({key:1}, {key:1, val:2});
    sleep(3000);
    print(tojson(session.commitTransaction()));
});

threadA.start();
threadB.start();

var state = 0;
while (state != 3) {
    var result = db.test.findOne({key:1});
    print(tojson(result));
    state = result.val;

    sleep(1000);
}

threadA.join();
threadB.join();

