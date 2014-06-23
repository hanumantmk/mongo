from encoded_value import *

print(CLASS("_MSGHEADER", [
    FIELD("int", "messageLength"),
    FIELD("int", "requestID"),
    FIELD("int", "responseTo"),
    FIELD("int", "opCode"),
]).cpp());

print("typedef _MSGHEADER<> MSGHEADER;\n");
