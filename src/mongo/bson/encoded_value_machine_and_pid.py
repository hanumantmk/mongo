from encoded_value import *

print(CLASS("MachineAndPidGenerated", [
    SHORTINTFIELD("unsigned ", "_machineNumber", 3, None, "Big"),
    FIELD("unsigned short", "_pid")
]).cpp());
