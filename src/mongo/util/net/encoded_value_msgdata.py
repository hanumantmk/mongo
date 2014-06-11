from encoded_value import *

print(CLASS("MsgData", [
    FIELD("int", "len"),
    FIELD("MSGID", "id"),
    FIELD("MSGID", "responseTo"),
    FIELD("short", "_operation"),
    FIELD("char", "_flags"),
    FIELD("char", "_version"),
    FIELD("char", "_data", 4),
    EXTRA_CONST('''

    int operation() const {
        return _operation();
    }

    int dataLen() const {
        return len() - (MsgData<>::_size - 4);
    }

    long long getCursor() const {
        verify( responseTo() > 0 );
        verify( _operation() == opReply );
        return encoded_value::CReference<long long, convertEndian>(_data().ptr() + 4);
    }

    bool valid() const {
        if ( len() <= 0 || len() > ( 4 * BSONObjMaxInternalSize ) )
            return false;
        if ( _operation() < 0 || _operation() > 30000 )
            return false;
        return true;
    }

    '''),
    EXTRA_MUTABLE('''

    void setOperation(int o) {
        _flags() = 0;
        _version() = 0;
        _operation() = o;
    }

    encoded_value::Reference<int, convertEndian> dataAsInt() {
        return _data().ptr();
    }

    ''')
]).cpp());
