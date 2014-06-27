from encoded_value import *

print(CLASS("MSGHEADERGenerated", [
    FIELD("int", "messageLength"),
    FIELD("int", "requestID"),
    FIELD("int", "responseTo"),
    FIELD("int", "opCode"),
]).cpp());

print("typedef MSGHEADERGenerated<> MSGHEADER;\n");
