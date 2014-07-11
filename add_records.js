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
            },
            lua: {
                $lua : ["x = 0\n" +
                        "function mget ()\n" +
                            "return x\n" +
                       "end\n" +
                       "function get ()\n" +
                            "return x\n" +
                       "end\n" +
                       "function mprocess (y)\n" +
                           "x = x + tonumber(y)\n" +
                       "end\n" +
                       "function process (y)\n" +
                           "x = x + tonumber(y)\n" +
                       "end\n" +
                       "function reset ()\n" +
                       "end\n", "$val"]
            }
        }
    },
    {
        $project : {
            lua_group : "$lua",
            add : { $add : [ "$sum", 2 ] },
            addAndMul2 : { $addAndMul2 : [ "$sum", 2 ] },
            tcc : { $tcc : [ "(int)x % 9", "$sum" ] },
            lua : { $lua : [ "function foo ( a, b, c )\nreturn a + b + c\nend\n", "$sum", 1000, 10000 ] },
            luastr : { $lua : [ "function foo ( s )\nreturn string.len(s)\nend\n", "$sum" ] },
            shell : { $shell : [ "figlet", "Welcome to the Mongo" ] }
        }
    }
]).forEach(function (doc) {
    shell = doc.shell;
    delete doc["shell"];

    print( "------------------------------------------" );
    print( "json: " + tojson(doc) )
    print( );
    print( "shell:\n" + shell );
    print( "------------------------------------------" );
    print( );
    print( );
});
