from encoded_value import *

print(CLASS("QueryResultGenerated", [
    FIELD("long long", "cursorId"),
    FIELD("int", "startingFrom"),
    FIELD("int", "nReturned"),
], "MsgDataPrivate").cpp());
