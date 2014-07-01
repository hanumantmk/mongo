from encoded_value import *

print(CLASS("OIDGenerated", [
    UNION([
        STRUCT([
            FIELD("unsigned", "_time", None, "Big"),
            EVSTRUCT("MachineAndPidPrivate", "_machineAndPid"),
            SHORTINTFIELD("unsigned", "_inc", 3, None, "Big")
        ]),
        STRUCT([
            FIELD("long long", "a"),
            FIELD("unsigned", "b")
        ]),
        STRUCT([
            FIELD("int", "x"),
            FIELD("int", "y"),
            FIELD("int", "z")
        ]),
        FIELD("unsigned char", "data", 12),
    ])
]).cpp());
