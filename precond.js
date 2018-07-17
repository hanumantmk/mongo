load("jstests/libs/parallelTester.js");

db.dropDatabase();
db.createCollection("test1");
db.createCollection("test2");
db.createCollection("test3");

function doBulk(retry) {
    var session = new Mongo().startSession();

    print(tojson(session.getDatabase("test").runCommand({
        bulkWrite: "admin",
        writeConcern: {w: "majority"},
        startTransaction: true,
        autocommit: true,
        txnNumber: NumberLong(1),
        preConditions: [
            {ns: "test.test1", q: {special: 1}, res: {special: 1}, retryable: retry},
        ],
        commands: [
            {insert: "test2", $db: "test", documents: [{a: 1}]},
            {insert: "test3", $db: "test", documents: [{b: 1}]}
        ]
    })));
};

var threadA = new Thread(doBulk, true);
var threadB = new Thread(doBulk, false);

var threadC = new Thread(function() {
    var session = new Mongo().startSession();

    sleep(3000);

    print(tojson(session.getDatabase("test").test1.insert({special: 1})));
});

threadA.start();
threadB.start();
threadC.start();

threadA.join();
threadB.join();
threadC.join();

print(tojson([db.test1.findOne(), db.test2.findOne(), db.test3.findOne()]));
