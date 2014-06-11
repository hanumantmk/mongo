from encoded_value import *

print(CLASS("MSGHEADER", [
    FIELD("int", "messageLength"),
    FIELD("int", "requestID"),
    FIELD("int", "responseTo"),
    FIELD("int", "opCode"),
]).cpp());
