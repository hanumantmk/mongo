from encoded_value import *

print(CLASS("_QueryResult", [
    FIELD("long long", "cursorId"),
    FIELD("int", "startingFrom"),
    FIELD("int", "nReturned"),
    EXTRA_CONST('''
        const char *data() const {
            return (&(nReturned()) + 1).ptr();
        }

        int resultFlags() const {
            return encoded_value::CReference<int, convertEndian>(this->_data().ptr());
        }
    '''),
    EXTRA_MUTABLE('''
        encoded_value::Reference<int, convertEndian> _resultFlags() {
            return this->dataAsInt();
        }
        void setResultFlagsToOk() {
            _resultFlags() = ResultFlag_AwaitCapable;
        }
        void initializeResultFlags() {
            _resultFlags() = 0;
        }
    ''')
], "_MsgData").cpp());

print("typedef _QueryResult<> QueryResult;\n");
