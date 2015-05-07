/*    Copyright 2014 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kQuery

#include "mongo/platform/basic.h"

#include "mongo/scripting/engine_v8-sm.h"

#include <iostream>

#include "mongo/base/init.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/unordered_set.h"
#include "mongo/scripting/v8-sm_db.h"
#include "mongo/scripting/v8-sm_utils.h"
#include "mongo/util/base64.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"

using namespace mongoutils;

namespace mongo {

    class BSONHolder {
    MONGO_DISALLOW_COPYING(BSONHolder);
    public:
        BSONHolder(SMScope* scope, BSONObj obj) :
            _scope(scope),
            _obj(obj.getOwned()),
            _resolved(false) {
            invariant(scope);
        }

        SMScope* _scope;
        const BSONObj _obj;
        bool _resolved;
    };

    namespace {
        using namespace JS;

        static constexpr JSClass globalClass = {
            "global",
            JSCLASS_GLOBAL_FLAGS,
        };

        struct bsonMethods {
            static bool AddProperty(JSContext *cx, JS::HandleObject obj,
                                        JS::HandleId id, JS::MutableHandleValue v) {
                return false;
            };
            static bool DeleteProperty(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                                   bool *succeeded) {
                return false;
            }
            static bool GetProperty(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                           JS::MutableHandleValue vp) {
                return false;
            }
            static bool SetProperty(JSContext *cx, JS::HandleObject obj, JS::HandleId id,
                                   bool strict, JS::MutableHandleValue vp) {
                return false;
            }
            static bool Enumerate(JSContext* cx, JS::HandleObject obj) {
                auto holder = static_cast<BSONHolder*>(JS_GetPrivate(obj));

                if (holder->_resolved) {
                    return true;
                }

                BSONObjIterator i(holder->_obj);
                while (i.more()) {
                    BSONElement e = i.next();

                    JS::RootedValue vp(cx);

                    holder->_scope->mongoToSMElement(e, true, &vp);

                    JS_SetProperty(cx, obj, e.fieldName(), vp);
                }

                holder->_resolved = true;

                return true;
            }
            static bool Resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                            bool* resolvedp) {

                auto holder = static_cast<BSONHolder*>(JS_GetPrivate(obj));

                if (holder->_resolved) {
                    return true;
                }

                bool found;

                JS_HasPropertyById(cx, obj, id, &found);

                if (found) {
                    return true;
                }

                auto str = JSID_TO_STRING(id);

                char* cstr = JS_EncodeString(cx, str);

                auto elem = holder->_obj[cstr];

                JS::RootedValue vp(cx);

                holder->_scope->mongoToSMElement(elem, true, &vp);

                JS_SetPropertyById(cx, obj, id, vp);

                JS_free(cx, cstr);
                *resolvedp = true;

                return true;
            }
        };

        static constexpr JSClass bsonClass = {
            "bson",
            JSCLASS_HAS_PRIVATE,
            bsonMethods::AddProperty,
            bsonMethods::DeleteProperty,
            nullptr,
            bsonMethods::SetProperty,
            bsonMethods::Enumerate,
            bsonMethods::Resolve
        };
    }

    using std::cout;
    using std::endl;
    using std::map;
    using std::string;
    using std::stringstream;

    // Generated symbols for JS files
    namespace JSFiles {
        extern const JSFile types;
        extern const JSFile assert;
    }

     SMScriptEngine::SMScriptEngine()
     {
         JS_Init();
     }

     SMScriptEngine::~SMScriptEngine() {
         JS_ShutDown();
     }

     void ScriptEngine::setup() {
         if (!globalScriptEngine) {
             globalScriptEngine = new SMScriptEngine();

             if (hasGlobalServiceContext()) {
                 getGlobalServiceContext()->registerKillOpListener(globalScriptEngine);
             }
         }
     }

     std::string ScriptEngine::getInterpreterVersionString() {
         return "SM 37";
     }

     std::string SMScope::getError() {
         return ""; // TODO ...
     }

     void SMScriptEngine::interrupt(unsigned opId) {
         // TODO ...
     }

     void SMScriptEngine::interruptAll() {
         // TODO ...
     }

     void SMScope::registerOperation(OperationContext* txn) {
         // TODO ...
     }

     void SMScope::unregisterOperation() {
         // TODO ...
    }

    void SMScope::kill() {
        // TODO ...
    }

    /** check if there is a pending killOp request */
    bool SMScope::isKillPending() const {
        // TODO ...
        return false;
    }
    
    OperationContext* SMScope::getOpContext() const {
        // TODO ...
        return nullptr;
    }

    SMScope::SMScope(SMScriptEngine * engine) :
        _runtime(JS_NewRuntime(8L * 1024 * 1024)),
        _context(JS_NewContext(_runtime, 8192)),
        _global(_context),
        _funcs(_context)
    {
        JSAutoRequest ar(_context);
        _global.set(JS_NewGlobalObject(_context, &globalClass, nullptr, JS::DontFireOnNewGlobalHook));

        JSAutoCompartment ac(_context, _global);

        JS_InitStandardClasses(_context, _global);
    }

    SMScope::~SMScope() {
        unregisterOperation();
        JS_DestroyContext(_context);
        JS_DestroyRuntime(_runtime);
    }

    bool SMScope::hasOutOfMemoryException() {
        return false;
    }

    void SMScope::init(const BSONObj * data) {
        if (! data)
            return;

        BSONObjIterator i(*data);
        while (i.more()) {
            BSONElement e = i.next();
            setElement(e.fieldName(), e);
        }
    }

#define SMMAGIC_HEADER \
    JSAutoRequest ar(_context); \
    JSAutoCompartment ac(_context, _global)

    void SMScope::_setValue(const char * field, JS::HandleValue val) {
        JS_SetProperty(_context, _global, field, val);
    }

    void SMScope::setNumber(const char * field, double val) {
        SMMAGIC_HEADER;
        JS::RootedValue jsValue(_context, DOUBLE_TO_JSVAL(val));
        _setValue(field, jsValue);
    }

    void SMScope::setString(const char * field, StringData val) {
        SMMAGIC_HEADER;
        JS::RootedValue jsValue(_context, STRING_TO_JSVAL(JS_NewStringCopyN(_context, val.rawData(), val.size())));
        _setValue(field, jsValue);
    }

    void SMScope::setBoolean(const char * field, bool val) {
        SMMAGIC_HEADER;
        JS::RootedValue jsValue(_context, BOOLEAN_TO_JSVAL(val));
        _setValue(field, jsValue);
    }

    void SMScope::setElement(const char *field, const BSONElement& e) {
    }

    void SMScope::setObject(const char *field, const BSONObj& obj, bool readOnly) {
    }

    int SMScope::type(const char *field) {
        SMMAGIC_HEADER;
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        if (x.isNull())
            return jstNULL;
        if (x.isUndefined())
            return Undefined;
        if (x.isString())
            return String;
//        if (x.isFunction())
//            return Code;
//        if (x.isArray())
//            return Array;
        if (x.isBoolean())
            return Bool;
        // needs to be explicit NumberInt to use integer
//        if (v->IsInt32())
//            return NumberInt;
        if (x.isNumber())
            return NumberDouble;
        //if (x.isExternal()) {
        //    uassert(10230,  "can't handle external yet", 0);
        //    return -1;
        //}
        //if (v->IsDate())
        //    return Date;
        if (x.isObject())
            return Object;

        // uasserted(12509, str::stream() << "unable to get type of field " << field);
        abort();
    }

    double SMScope::getNumber(const char *field) {
        SMMAGIC_HEADER;
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        return x.toDouble();
    }

    int SMScope::getNumberInt(const char *field) {
        SMMAGIC_HEADER;
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        return x.toInt32();
    }

    long long SMScope::getNumberLongLong(const char *field) {
        SMMAGIC_HEADER;
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        return x.toDouble();
    }

    string SMScope::getString(const char *field) {
        SMMAGIC_HEADER;
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        char* cstr = JS_EncodeString(_context, x.toString());

        std::string out(cstr);

        JS_free(_context, cstr);

        return out;
    }

    bool SMScope::getBoolean(const char *field) {
        SMMAGIC_HEADER;
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        return x.toBoolean();
    }

    BSONObj SMScope::getObject(const char * field) {
        SMMAGIC_HEADER;
        // uassert(10231,  "not an object", v->IsObject());
        return BSONObj(); // TODO ...
    }

    void SMScope::smToMongoNumber(BSONObjBuilder& b,
                         StringData elementName,
                         JS::HandleValue value,
                         BSONObj* originalParent) {
        b.append(elementName, value.get().toNumber());
    }

    void SMScope::smToMongoObject(BSONObjBuilder& b,
                         StringData sname,
                         JS::HandleValue value,
                         int depth,
                         BSONObj* originalParent) {
        //TODO something
    }

    BSONObj SMScope::smToMongo(JS::HandleObject o, int depth) {
        BSONObj originalBSON;

        BSONObjBuilder b;

        // We special case the _id field in top-level objects and move it to the front.
        // This matches other drivers behavior and makes finding the _id field quicker in BSON.
        if (depth == 0) {
            bool found;
            JS_HasProperty(_context, o, "_id", &found);

            if (found) {
                JS::RootedValue x(_context);
                JS_GetProperty(_context, o, "_id", &x);

                smToMongoElement(b, "_id", x, 0, &originalBSON);
            }
        }

        JS::AutoIdArray names(_context, JS_Enumerate(_context, o));

        for (size_t i = 0; i < names.length(); ++i) {
            JS::RootedValue x(_context);

            JS_IdToValue(_context, names[i], &x);

            bool is_id;
            JS_StringEqualsAscii(_context, x.toString(), "_id", &is_id);

            if (depth == 0 && is_id)
                continue;

            JS::RootedValue value(_context);

            JS::RootedId id(_context, names[i]);

            JS_GetPropertyById(_context, o, id, &value);

            char* sname = JS_EncodeString(_context, x.toString());

            smToMongoElement(b, sname, value, depth + 1, &originalBSON);

            JS_free(_context, sname);
        }

        const int sizeWithEOO = b.len() + 1/*EOO*/ - 4/*BSONObj::Holder ref count*/;
        uassert(17260, str::stream() << "Converting from JavaScript to BSON failed: "
                                     << "Object size " << sizeWithEOO << " exceeds limit of "
                                     << BSONObjMaxInternalSize << " bytes.",
                sizeWithEOO <= BSONObjMaxInternalSize);

        return b.obj(); // Would give an uglier error than above for oversized objects.
    }

    void SMScope::smToMongoElement(BSONObjBuilder & b, StringData sname,
                                   JS::HandleValue value, int depth,
                                   BSONObj* originalParent) {
        uassert(17279,
                str::stream() << "Exceeded depth limit of " << 128 
                              << " when converting js object to BSON. Do you have a cycle?",
                depth < 128);

        // Null char should be at the end, not in the string
        uassert(16985,
                str::stream() << "JavaScript property (name) contains a null char "
                              << "which is not allowed in BSON. "
                              << originalParent->jsonString(),
                (string::npos == sname.find('\0')) );

        if (value.isString()) {
            char* str = JS_EncodeString(_context, value.toString());
            b.append(sname, StringData(str));
            return;
        }
        /*
        if (value->IsFunction()) {
            uassert(16716, "cannot convert native function to BSON",
                    !value->ToObject()->Has(strLitToV8("_v8_function")));
            b.appendCode(sname, V8String(value));
            return;
        }
        */
        if (value.isNumber()) {
            smToMongoNumber(b, sname, value, originalParent);
            return;
        }
        if (JS_IsArrayObject(_context, value)) {
            // Note: can't use BSONArrayBuilder because need to call recursively
            BSONObjBuilder arrBuilder(b.subarrayStart(sname));

            RootedObject array(_context, value.toObjectOrNull());

            uint32_t len;
            JS_GetArrayLength(_context, array, &len);

            for (uint32_t i = 0; i < len; i++) {
                const string name = BSONObjBuilder::numStr(i);

                RootedValue value(_context);

                JS_GetElement(_context, array, i, &value);
                smToMongoElement(arrBuilder, name, value, depth+1, originalParent);
            }
            return;
        }
        /*
        if (value->IsDate()) {
            long long dateval = (long long)(v8::Date::Cast(*value)->NumberValue());
            b.appendDate(sname, Date_t((unsigned long long) dateval));
            return;
        }
        if (value->IsExternal())
            return;
        */
        if (value.isObject()) {
            smToMongoObject(b, sname, value, depth, originalParent);
            return;
        }

        if (value.isBoolean()) {
            b.appendBool(sname, value.toBoolean());
            return;
        }
        else if (value.isUndefined()) {
            b.appendUndefined(sname);
            return;
        }
        else if (value.isNull()) {
            b.appendNull(sname);
            return;
        }
        uasserted(16662, str::stream() << "unable to convert JavaScript property to mongo element "
                                   << sname);
    }

    void SMScope::newFunction(StringData code, JS::MutableHandleValue out) {
        SMMAGIC_HEADER;
        std::string fn = str::stream() << "____MongoToV8_newFunction_temp = " << code;

        JS::CompileOptions co(_context);
        JS::RootedValue value(_context);
        JS::RootedScript script(_context);

        JS_CompileScript(_context, _global, fn.c_str(), fn.length(), co, &script);

        JS_ExecuteScript(_context, _global, script, &value);

        JS_GetProperty(_context, _global, fn.c_str(), out);
    }


    void SMScope::mongoToSMElement(const BSONElement &elem, bool readOnly, JS::MutableHandleValue out) {
        switch (elem.type()) {
        case mongo::Code:
            return newFunction(elem.valueStringData(), out);
        case CodeWScope:
            if (!elem.codeWScopeObject().isEmpty())
                warning() << "CodeWScope doesn't transfer to db.eval" << endl;
            return newFunction(StringData(elem.codeWScopeCode(), elem.codeWScopeCodeLen() - 1), out);
        case mongo::Symbol:
        case mongo::String:
        {
            auto str = elem.String();
            auto jsstr = JS_NewStringCopyN(_context, str.c_str(), str.length());

            out.set(STRING_TO_JSVAL(jsstr));
            return;
        }
        case mongo::jstOID:
            abort();
//            return newId(elem.__oid());
        case mongo::NumberDouble:
        case mongo::NumberInt:
            out.set(DOUBLE_TO_JSVAL(elem.number()));
            return;
        case mongo::Array:
        {
            JS::RootedObject array(_context, JS_NewArrayObject(_context, 0));
            int i = 0;
            BSONForEach(subElem, elem.embeddedObject()) {
                JS::RootedValue member(_context);
                mongoToSMElement(subElem, readOnly, &member);
                JS_SetElement(_context, array, i++, member); 
            }
            out.set(OBJECT_TO_JSVAL(array));
            return;
        }
        case mongo::Object:
            return mongoToLZSM(elem.embeddedObject(), readOnly, out);
//        case mongo::Date:
//            return v8::Date::New((double) ((long long)elem.date().millis));
        case mongo::Bool:
            return out.setBoolean(elem.Bool());
        case mongo::EOO:
        case mongo::jstNULL:
        case mongo::Undefined: // duplicate sm behavior
            return out.setNull();
//        case mongo::RegEx: {
//            // TODO parse into a custom type that can support any patterns and flags SERVER-9803
//            v8::TryCatch tryCatch;
//
//            v8::Handle<v8::Value> args[] = {
//                v8::String::New(elem.regex()),
//                v8::String::New(elem.regexFlags())
//            };
//
//            v8::Handle<v8::Value> ret = _jsRegExpConstructor->NewInstance(2, args);
//            uassert(16863, str::stream() << "Error converting " << elem.toString(false)
//                                         << " in field " << elem.fieldName()
//                                         << " to a JS RegExp object: "
//                                         << toSTLString(tryCatch.Exception()),
//                    !tryCatch.HasCaught());
//
//            return ret;
//        }
//        case mongo::BinData: {
//            int len;
//            const char *data = elem.binData(len);
//            stringstream ss;
//            base64::encode(ss, data, len);
//            argv[0] = v8::Number::New(elem.binDataType());
//            argv[1] = v8::String::New(ss.str().c_str());
//            return BinDataFT()->GetFunction()->NewInstance(2, argv);
//        }
//        case mongo::bsonTimestamp: {
//            v8::TryCatch tryCatch;
//
//            argv[0] = v8::Number::New(elem.timestampTime() / 1000);
//            argv[1] = v8::Number::New(elem.timestampInc());
//
//            v8::Handle<v8::Value> ret = TimestampFT()->GetFunction()->NewInstance(2,argv);
//            uassert(17355, str::stream() << "Error converting " << elem.toString(false)
//                                         << " in field " << elem.fieldName()
//                                         << " to a JS Timestamp object: "
//                                         << toSTLString(tryCatch.Exception()),
//                    !tryCatch.HasCaught());
//
//            return ret;
//        }
        case mongo::NumberLong:
            return out.setDouble(elem.numberLong());
//            nativeUnsignedLong = elem.numberLong();
//            // values above 2^53 are not accurately represented in JS
//            if ((long long)nativeUnsignedLong ==
//                (long long)(double)(long long)(nativeUnsignedLong) &&
//                    nativeUnsignedLong < 9007199254740992ULL) {
//                argv[0] = v8::Number::New((double)(long long)(nativeUnsignedLong));
//                return NumberLongFT()->GetFunction()->NewInstance(1, argv);
//            }
//            else {
//                argv[0] = v8::Number::New((double)(long long)(nativeUnsignedLong));
//                argv[1] = v8::Integer::New(nativeUnsignedLong >> 32);
//                argv[2] = v8::Integer::New((unsigned long)
//                                           (nativeUnsignedLong & 0x00000000ffffffff));
//                return NumberLongFT()->GetFunction()->NewInstance(3, argv);
//            }
//        case mongo::MinKey:
//            return MinKeyFT()->GetFunction()->NewInstance();
//        case mongo::MaxKey:
//            return MaxKeyFT()->GetFunction()->NewInstance();
//        case mongo::DBRef:
//            argv[0] = v8StringData(elem.dbrefNS());
//            argv[1] = newId(elem.dbrefOID());
//            return DBPointerFT()->GetFunction()->NewInstance(2, argv);
        default:
            massert(16661, str::stream() << "can't handle type: " << elem.type()
                                         << " " << elem.toString(), false);
            break;
        }
        return out.setUndefined();
    }

    void SMScope::mongoToLZSM(const BSONObj& m, bool readOnly, JS::MutableHandleValue out) {
        if (m.firstElementType() == String && str::equals(m.firstElementFieldName(), "$ref")) {
            abort();
#if 0
            BSONObjIterator it(m);
            const BSONElement ref = it.next();
            const BSONElement id = it.next();
            if (id.ok() && str::equals(id.fieldName(), "$id")) {
                v8::Handle<v8::Value> args[] = {
                    mongoToSMElement(ref, readOnly),
                    mongoToSMElement(id, readOnly)
                };
                v8::Local<v8::Object> dbRef = DBRefFT()->GetFunction()->NewInstance(2, args);
                while (it.more()) {
                    BSONElement elem = it.next();
                    dbRef->Set(v8StringData(elem.fieldName()), mongoToSMElement(elem, readOnly));
                }
                return dbRef;
            }
#endif
        }

        JS::RootedObject proto(_context);
        JS::RootedObject parent(_context);

        JS::RootedObject obj(_context, JS_NewObject(_context, &bsonClass, proto, parent));

        JS_SetPrivate(obj, new BSONHolder(this, m));

        out.setObject(*obj.get());

    }

    void SMScope::__createFunction(const char* raw, ScriptingFunction functionNumber, JS::MutableHandleScript script) {
        // uassert(10232, "not a function", ret->IsFunction());
        
        std::string fn = str::stream() << "_funcs" << functionNumber << " = " << raw;

        JS::CompileOptions co(_context);
        JS::RootedValue value(_context);

        JS_CompileScript(_context, _global, fn.c_str(), fn.length(), co, script);

        JS_ExecuteScript(_context, _global, script, &value);
    }

    ScriptingFunction SMScope::_createFunction(const char* raw, ScriptingFunction functionNumber) {
        SMMAGIC_HEADER;
        // uassert(10232, "not a function", ret->IsFunction());

        JS::RootedScript script(_context);

        __createFunction(raw, functionNumber, &script);

        _funcs.append(script);

        return functionNumber;
    }

    void SMScope::setFunction(const char* field, const char* code) {

    }

    void SMScope::rename(const char * from, const char * to) {
        // TODO ...
    }

    int SMScope::invoke(ScriptingFunction func, const BSONObj* argsObject, const BSONObj* recv,
                        int timeoutMs, bool ignoreReturn, bool readOnlyArgs, bool readOnlyRecv) {
        // TODO ...

        return 0;
    }

    bool SMScope::exec(StringData code, const string& name, bool printResult,
                       bool reportError, bool assertOnError, int timeoutMs) {
        // TODO ...

        return true;
    }

    void SMScope::injectNative(const char *field, NativeFunction func, void* data) {
        // TODO ...
    }

    void SMScope::gc() {
        // TODO ...
    }

    void SMScope::localConnectForDbEval(OperationContext* txn, const char * dbName) {
        // TODO ...
    }

    void SMScope::externalSetup() {
        // TODO ...
    }

    // ----- internal -----

    void SMScope::reset() {
        // TODO ...
    }

    // --- random utils ----

    static logger::MessageLogDomain* jsPrintLogDomain;

    MONGO_INITIALIZER(JavascriptPrintDomain)(InitializerContext*) {
        jsPrintLogDomain = logger::globalLogManager()->getNamedDomain("javascriptOutput");
        return Status::OK();
    }

} // namespace mongo
