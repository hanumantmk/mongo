from encoded_value import *

print(CLASS("MsgDataGenerated", [
    FIELD("int", "len"),
    FIELD("MSGID", "id"),
    FIELD("MSGID", "responseTo"),
    FIELD("short", "_operation"),
    FIELD("char", "_flags"),
    FIELD("char", "_version"),
    FIELD("char", "_data", 4),
]).cpp());
