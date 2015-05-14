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

    void reportError(JSContext *cx, const char *message,
                     JSErrorReport *report) {
      fprintf(stderr, "%s:%u:%s\n",
              report->filename ? report->filename : "[no filename]",
              (unsigned int)report->lineno, message);
    }

    static logger::MessageLogDomain* jsPrintLogDomain;
    static bool Print(JSContext *cx, unsigned argc, JS::Value *vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
        auto scope = static_cast<SMScope*>(JS_GetContextPrivate(cx));
        LogstreamBuilder builder(jsPrintLogDomain, getThreadName(), logger::LogSeverity::Log());
//        std::ostream& ss = builder.stream();
        std::ostream& ss = std::cerr;

        bool first = true;
        for (size_t i = 0; i < args.length(); i++) {
            if (first)
                first = false;
            else
                ss << " ";

            if (args.get(i).isNullOrUndefined()) {
                // failed to get object to convert
                ss << "[unknown type]";
                continue;
            }
//            if (args[i]->IsExternal()) {
//                // object is External
//                ss << "[mongo internal]";
//                continue;
//            }

            auto str = scope->toSTLString(JS::ToString(cx, args.get(i)));
            ss << str;
        }
        ss << "\n";

        return true;
    }

    static bool Version(JSContext *cx, unsigned argc, JS::Value *vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
        auto scope = static_cast<SMScope*>(JS_GetContextPrivate(cx));

        scope->fromStringData(JS_VersionToString(JS_GetVersion(cx)), args.rval());

        return true;
    }

    static bool SMGC(JSContext *cx, unsigned argc, JS::Value *vp) {
        auto scope = static_cast<SMScope*>(JS_GetContextPrivate(cx));

        scope->gc();

        return true;
    }

    class BSONHolder {
    MONGO_DISALLOW_COPYING(BSONHolder);
    public:
        BSONHolder(SMScope* scope, BSONObj obj) :
            _scope(scope),
            _obj(obj.getOwned()),
            _resolved(false)
        {
            invariant(scope);
        }

        SMScope* _scope;
        const BSONObj _obj;
        bool _resolved;
    };

    class NativeHolder {
    MONGO_DISALLOW_COPYING(NativeHolder);
    public:
        NativeHolder(SMScope* scope, NativeFunction func, void* ctx) :
            _scope(scope),
            _func(func),
            _ctx(ctx) {
            invariant(scope);
        }

        SMScope* _scope;
        NativeFunction _func;
        void* _ctx;
    };

    namespace {
        using namespace JS;

        static constexpr JSClass globalClass = {
            "global",
            JSCLASS_GLOBAL_FLAGS,
        };

        struct nativeMethods {
            static bool Call(JSContext *cx, unsigned argc, JS::Value *vp) {
                JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

                void* ctx = JS_GetPrivate(&args.callee());

                auto holder = static_cast<NativeHolder*>(ctx);

                BSONObjBuilder bob;

                for (unsigned i = 0; i < args.length(); i++) {
                    char buf[20];
                    std::snprintf(buf, sizeof(buf), "%i", i);

                    holder->_scope->smToMongoElement(bob, buf, args.get(i), 0, nullptr);
                }

                BSONObj out;

                try {
                    out = holder->_func(bob.obj(), holder->_ctx);
                } catch (...) {
                    Status s = exceptionToStatus();

                    std::cerr << "Failure in native function: " << s << std::endl;

                    return false;
                }

                JS::RootedValue rval(cx);

                holder->_scope->mongoToLZSM(out, false, &rval);

                args.rval().set(rval);

                return true;
            }
            static void Finalize(JSFreeOp *fop, JSObject *obj) {
                auto holder = static_cast<NativeHolder*>(JS_GetPrivate(obj));

                delete holder;
            }
        };

        struct failMethods {
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
                return false;
            }

            static bool Resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                            bool* resolvedp) {
                return false;
            }
            static bool Convert(JSContext *cx, JS::HandleObject obj, JSType type,
                            JS::MutableHandleValue vp) {
                return false;
            }
        };
        struct bsonMethods {
            static void Finalize(JSFreeOp *fop, JSObject *obj) {
                auto holder = static_cast<BSONHolder*>(JS_GetPrivate(obj));

                delete holder;
            }

            static bool Enumerate(JSContext* cx, JS::HandleObject obj) {
                auto holder = static_cast<BSONHolder*>(JS_GetPrivate(obj));

                if (holder->_resolved) {
                    return true;
                }

                BSONObjIterator i(holder->_obj);
                while (i.more()) {
                    BSONElement e = i.next();

                    auto field = e.fieldName();

                    JS::RootedValue vp(cx);
                    holder->_scope->checkBool(JS_GetProperty(cx, obj, field, &vp));
                }

                holder->_resolved = true;

                return true;
            }
            static bool Resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                            bool* resolvedp) {

                auto holder = static_cast<BSONHolder*>(JS_GetPrivate(obj));

                char* cstr;
                char buf[20];

                if (JSID_IS_STRING(id)) {
                    auto str = JSID_TO_STRING(id);

                    cstr = JS_EncodeString(cx, str);
                } else {
                    auto idx = JSID_TO_INT(id);

                    snprintf(buf, sizeof(buf), "%i", idx);

                    cstr = buf;
                }

                auto elem = holder->_obj[cstr];

                JS::RootedValue vp(cx);

                holder->_scope->mongoToSMElement(elem, true, &vp);

                holder->_scope->checkBool(JS_SetPropertyById(cx, obj, id, vp));

                if (JSID_IS_STRING(id)) {
                    JS_free(cx, cstr);
                }

                *resolvedp = true;

                return true;
            }
        };

        static constexpr JSClass bsonClass = {
            "bson",
            JSCLASS_HAS_PRIVATE,
            nullptr, //bsonMethods::AddProperty,
            nullptr, //bsonMethods::DeleteProperty,
            nullptr,
            nullptr, //bsonMethods::SetProperty,
            bsonMethods::Enumerate,
            bsonMethods::Resolve,
            nullptr,
            bsonMethods::Finalize
        };

        static constexpr JSClass nativeClass = {
            "native",
            JSCLASS_HAS_PRIVATE,
            failMethods::AddProperty,
            failMethods::DeleteProperty,
            failMethods::GetProperty,
            failMethods::SetProperty,
            failMethods::Enumerate,
            failMethods::Resolve,
            failMethods::Convert,
            nativeMethods::Finalize,
            nativeMethods::Call
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
        std::cerr << "in interrupt()" << std::endl;
         boost::lock_guard<boost::mutex> intLock(_globalInterruptLock);
         OpIdToScopeMap::iterator iScope = _opToScopeMap.find(opId);
         if (iScope == _opToScopeMap.end()) {
             // got interrupt request for a scope that no longer exists
//             LOG(1) << "received interrupt request for unknown op: " << opId
//                    << printKnownOps_inlock() << endl;
             return;
         }
//         LOG(1) << "interrupting op: " << opId << printKnownOps_inlock() << endl;
         iScope->second->kill();
     }

     void SMScriptEngine::interruptAll() {
        std::cerr << "in interruptAll()" << std::endl;
         boost::lock_guard<boost::mutex> interruptLock(_globalInterruptLock);
         for (OpIdToScopeMap::iterator iScope = _opToScopeMap.begin();
              iScope != _opToScopeMap.end(); ++iScope) {
             iScope->second->kill();
         }
     }

     void SMScope::registerOperation(OperationContext* txn) {
        std::cerr << "in registerOperation()" << std::endl;
         boost::lock_guard<boost::mutex> giLock(_engine->_globalInterruptLock);
         invariant(_opId == 0);
         _opId = txn->getOpID();
         _engine->_opToScopeMap[_opId] = this;
         LOG(2) << "MSScope " << static_cast<const void*>(this) << " registered for op " << _opId;
         Status status = txn->checkForInterruptNoAssert();
         if (!status.isOK()) {
             kill();
         }
     }

     void SMScope::unregisterOperation() {
        std::cerr << "in unregisterOperation()" << std::endl;
         boost::lock_guard<boost::mutex> giLock(_engine->_globalInterruptLock);
         LOG(2) << "SMScope " << static_cast<const void*>(this) << " unregistered for op " << _opId << endl;
        if (_opId != 0) {
            // scope is currently associated with an operation id
            SMScriptEngine::OpIdToScopeMap::iterator it = _engine->_opToScopeMap.find(_opId);
            if (it != _engine->_opToScopeMap.end())
                _engine->_opToScopeMap.erase(it);
            _opId = 0;
        }
     }

    void SMScope::kill() {
        std::cerr << "in kill()" << std::endl;
        _pendingKill.store(true);
        JS_RequestInterruptCallback(_runtime);
    }

    /** check if there is a pending killOp request */
    bool SMScope::isKillPending() const {
        return _pendingKill.load();
    }

    void SMScope::checkBool(bool x) {
        if (! x) {
            std::cerr << "we done g00fed" << std::endl;
            uassertStatusOK(Status(ErrorCodes::InternalError, "sm failure"));
        }
    }

    
    OperationContext* SMScope::getOpContext() const {
        return _opCtx;
    }

    bool InterruptCallback(JSContext* cx) {
        SMScope* scope = static_cast<SMScope*>(JS_GetContextPrivate(cx));

        if (scope->_pendingGC.load()) {
            JS_GC(scope->_runtime);
        }

        bool kill = scope->isKillPending();

        std::cerr << "in interrupt callback" <<
            " js_total_bytes(" << mongo::sm::get_total_bytes() << ")"
            " js_max_bytes(" << mongo::sm::get_max_bytes() << ")"
            " interrupt(" << scope->isKillPending() << ")"
            " kill?(" << kill << ")"
        << std::endl;

        if (kill) {
            scope->_engine->getDeadlineMonitor()->stopDeadline(scope);
            scope->unregisterOperation();
        }

        return !kill;
    }

    SMScope::SMScope(SMScriptEngine * engine) :
        _ts(),
        _engine(engine),
        _runtime(JS_NewRuntime(1L * 1024 * 1024)),
        _context(JS_NewContext(_runtime, 8192)),
        _global(_context),
        _funcs(_context),
        _pendingKill(false),
        _opId(0),
        _opCtx(nullptr),
        _pendingGC(false),
        _connectState(NOT),
        _oidProto(_context),
        _numberLongProto(_context)
    {
        JS_SetInterruptCallback(_runtime, InterruptCallback);
        JS_SetContextPrivate(_context, this);
        JSAutoRequest ar(_context);
        _global.set(JS_NewGlobalObject(_context, &globalClass, nullptr, JS::DontFireOnNewGlobalHook));

        JS_SetErrorReporter(_runtime, reportError);

        JSAutoCompartment ac(_context, _global);

        checkBool(JS_InitStandardClasses(_context, _global));

        injectSMFunction("print", Print);
        injectSMFunction("version", Version);  // TODO: remove
        injectSMFunction("gc", SMGC);

        installBSONTypes();
//        execSetup(JSFiles::types);
    }

    SMScope::~SMScope() {
        unregisterOperation();
//        JS_DestroyContext(_context);
//        JS_DestroyRuntime(_runtime);
    }

    bool SMScope::hasOutOfMemoryException() {
        return false;
    }

    void SMScope::injectSMFunction(StringData sd, JSNative fun) {
        auto ok = JS_DefineFunction(_context, _global, sd.rawData(), fun, 0, 0);

        checkBool(ok);
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

    std::string SMScope::toSTLString(JSString* str) {
        auto deleter= [&](char* ptr){ JS_free(_context, ptr); };

        std::unique_ptr<char, decltype(deleter)> cstr(JS_EncodeString(_context, str), deleter);

        if (!cstr) {
            uassertStatusOK(Status(ErrorCodes::InternalError, "sm couldn't encode string"));
        }

        return std::string(cstr.get());
    }

    void SMScope::fromStringData(StringData sd, JS::MutableHandleValue out) {
        auto jsStr = JS_NewStringCopyN(_context, sd.rawData(), sd.size());

        if (! jsStr) {
            uassertStatusOK(Status(ErrorCodes::InternalError, "sm couldn't copy string"));
        }

        out.setString(jsStr);
    }

#define SMMAGIC_HEADER \
    JSAutoRequest ar(_context); \
    JSAutoCompartment ac(_context, _global)

    void SMScope::_setValue(const char * field, JS::HandleValue val) {
        checkBool(JS_SetProperty(_context, _global, field, val));
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
        SMMAGIC_HEADER;

        JS::RootedValue value(_context);
        mongoToSMElement(e, false, &value);
        _setValue(field, value);
    }

    void SMScope::setObject(const char *field, const BSONObj& obj, bool readOnly) {
        SMMAGIC_HEADER;

        JS::RootedValue value(_context);
        mongoToLZSM(obj, readOnly, &value);
        _setValue(field, value);
    }

    int SMScope::type(const char *field) {
        SMMAGIC_HEADER;
        JS::RootedValue x(_context);

        checkBool(JS_GetProperty(_context, _global, field, &x));

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

         uasserted(12509, str::stream() << "unable to get type of field " << field);
    }

    double SMScope::getNumber(const char *field) {
        SMMAGIC_HEADER;
        JS::RootedValue x(_context);

        checkBool(JS_GetProperty(_context, _global, field, &x));

        return x.toNumber();
    }

    int SMScope::getNumberInt(const char *field) {
        SMMAGIC_HEADER;
        JS::RootedValue x(_context);

        checkBool(JS_GetProperty(_context, _global, field, &x));

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

        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        JS::RootedObject obj(_context, x.toObjectOrNull());

        return smToMongo(obj);
    }

    void SMScope::smToMongoNumber(BSONObjBuilder& b,
                         StringData elementName,
                         JS::HandleValue value,
                         BSONObj* originalParent) {
        b.append(elementName, value.get().toNumber());
    }

    long long numberLongVal(JSContext *cx, JS::HandleObject thisv) {
        JS::RootedValue floatApprox(cx);
        JS::RootedValue top(cx);
        JS::RootedValue bottom(cx);

        if (!JS_GetProperty(cx, thisv, "top", &top)) {
            if (! JS_GetProperty(cx, thisv, "floatApprox", &floatApprox)) {
                // TODO what to do here...
                return 0;
            }

            std::cerr << "returning floatApprox: " << floatApprox.toDouble() << std::endl;

            return (long long)(floatApprox.toDouble());
        }

        if (!JS_GetProperty(cx, thisv, "bottom", &bottom)) {
            // TODO what to do here...
            return 0;
        }

        std::cerr << "returning complex: " << 
            (long long)
            ((unsigned long long)((long long)top.toPrivateUint32() << 32) +
            (unsigned)(bottom.toPrivateUint32())) << std::endl;

        return
            (long long)
            ((unsigned long long)((long long)top.toPrivateUint32() << 32) +
            (unsigned)(bottom.toPrivateUint32()));
    }

    OID SMScope::smToMongoObjectID(JS::HandleValue value) {
        JS::RootedObject obj(_context, value.toObjectOrNull());

        auto oid = static_cast<OID*>(JS_GetPrivate(obj));

        return *oid;
    }

    void SMScope::smToMongoObject(BSONObjBuilder& b,
                         StringData elementName,
                         JS::HandleValue value,
                         int depth,
                         BSONObj* originalParent) {
        JS::RootedObject obj(_context, value.toObjectOrNull());

        bool check;

        if (false) {
//        if (value->IsRegExp()) {
//            v8ToMongoRegex(b, elementName, obj.As<v8::RegExp>());
        } else if (JS_HasInstance(_context, _oidProto, value, &check) && check) {
            b.append(elementName, smToMongoObjectID(value));
        } else if (JS_HasInstance(_context, _numberLongProto, value, &check) && check) {
            b.append(elementName, numberLongVal(_context, obj));
//        } else if (NumberIntFT()->HasInstance(value)) {
//            b.append(elementName, numberIntVal(this, obj));
//        } else if (DBPointerFT()->HasInstance(value)) {
//            v8ToMongoDBRef(b, elementName, obj);
//        } else if (BinDataFT()->HasInstance(value)) {
//            v8ToMongoBinData(b, elementName, obj);
//        } else if (TimestampFT()->HasInstance(value)) {
//            Timestamp ot (obj->Get(strLitToV8("t"))->Uint32Value(),
//                          obj->Get(strLitToV8("i"))->Uint32Value());
//            b.append(elementName, ot);
//        } else if (MinKeyFT()->HasInstance(value)) {
//            b.appendMinKey(elementName);
//        } else if (MaxKeyFT()->HasInstance(value)) {
//            b.appendMaxKey(elementName);
        } else {
            // nested object or array
            BSONObj sub = smToMongo(obj, depth);
            b.append(elementName, sub);
        }
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

            bool is_id = false;

            if (JSID_IS_STRING(names[i])) {
                JS_IdToValue(_context, names[i], &x);

                JS_StringEqualsAscii(_context, x.toString(), "_id", &is_id);
            }

            if (depth == 0 && is_id)
                continue;

            JS::RootedValue value(_context);

            JS::RootedId id(_context, names[i]);

            JS_GetPropertyById(_context, o, id, &value);

            char* sname;
            char buf[20];

            if (JSID_IS_STRING(names[i])) {
                sname = JS_EncodeString(_context, x.toString());
            } else {
                snprintf(buf, sizeof(buf), "%i", JSID_TO_INT(names[i]));
                sname = buf;
            }

            smToMongoElement(b, sname, value, depth + 1, &originalBSON);

            if (JSID_IS_STRING(names[i])) {
                JS_free(_context, sname);
            }
        }

        const int sizeWithEOO = b.len() + 1/*EOO*/ - 4/*BSONObj::Holder ref count*/;
        uassert(17260, str::stream() << "Converting from JavaScript to BSON failed: "
                                     << "Object size " << sizeWithEOO << " exceeds limit of "
                                     << BSONObjMaxInternalSize << " bytes.",
                sizeWithEOO <= BSONObjMaxInternalSize);

        return b.obj();
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

            arrBuilder.done();
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
            smToMongoObject(b, sname, value, depth + 1, originalParent);
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

    void SMScope::newFunction(StringData raw, JS::MutableHandleValue out) {
        SMMAGIC_HEADER;

        std::string code = str::stream() << "____MongoToSM_newFunction_temp = " << raw;

        JS::CompileOptions co(_context);

        checkBool(JS::Evaluate(_context, _global, co, code.c_str(), code.length(), out));
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
            fromStringData(elem.valueStringData(), out);
            return;
        }
        case mongo::jstOID:
            makeOID(out);
            return;
        case mongo::NumberDouble:
            out.setDouble(elem.Number());
            return;
        case mongo::NumberInt:
            out.setInt32(elem.Int());
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

    bool hasFunctionIdentifier(StringData code) {
        if (code.size() < 9 || code.find("function") != 0 )
            return false;

        return code[8] == ' ' || code[8] == '(';
    }

    void SMScope::__createFunction(const char* raw, ScriptingFunction functionNumber, JS::MutableHandleValue fun) {
        // uassert(10232, "not a function", ret->IsFunction());
        raw = jsSkipWhiteSpace(raw);
        string code = raw;
        if (!hasFunctionIdentifier(code)) {
            if (code.find('\n') == string::npos &&
                    ! hasJSReturn(code) &&
                    (code.find(';') == string::npos || code.find(';') == code.size() - 1)) {
                code = "return " + code;
            }
            code = "function(){ " + code + "}";
        }
        
        std::string fn = str::stream() << "_funcs" << functionNumber;
        code = str::stream() << "_funcs" << functionNumber << " = " << code;

        JS::CompileOptions co(_context);

        checkBool(JS::Evaluate(_context, _global, co, code.c_str(), code.length(), fun));
    }

    ScriptingFunction SMScope::_createFunction(const char* raw, ScriptingFunction functionNumber) {
        SMMAGIC_HEADER;
        // uassert(10232, "not a function", ret->IsFunction());

        JS::RootedValue fun(_context);

        __createFunction(raw, functionNumber, &fun);

        _funcs.append(fun);

        return functionNumber;
    }

    void SMScope::setFunction(const char* field, const char* code) {
        SMMAGIC_HEADER;

        JS::RootedValue fun(_context);

        __createFunction(code, getFunctionCache().size() + 1, &fun);

        _setValue(field, fun);
    }

    void SMScope::rename(const char * from, const char * to) {
        SMMAGIC_HEADER;

        JS::RootedValue value(_context);
        JS::RootedValue undefValue(_context);

        checkBool(JS_GetProperty(_context, _global, from, &value));

        undefValue.setUndefined();

        _setValue(to, value);
        _setValue(from, undefValue);
    }

    int SMScope::invoke(ScriptingFunction func, const BSONObj* argsObject, const BSONObj* recv,
                        int timeoutMs, bool ignoreReturn, bool readOnlyArgs, bool readOnlyRecv) {
        SMMAGIC_HEADER;

        auto funcValue = _funcs[func-1];
        JS::RootedValue result(_context);

        const int nargs = argsObject ? argsObject->nFields() : 0;

        JS::AutoValueVector args(_context);

        if (nargs) {
            BSONObjIterator it(*argsObject);
            for (int i=0; i<nargs; i++) {
                BSONElement next = it.next();

                JS::RootedValue value(_context);
                mongoToSMElement(next, readOnlyArgs, &value);

                args.append(value);
            }
        }

        JS::RootedValue smrecv(_context);
        if (recv != 0)
            mongoToLZSM(*recv, readOnlyRecv, &smrecv);
        else
            smrecv.setObjectOrNull(_global);

        //if (!nativeEpilogue()) {
        //    _error = "JavaScript execution terminated";
        //    error() << _error << endl;
        //    uasserted(16711, _error);
        //}

        if (timeoutMs)
            _engine->getDeadlineMonitor()->startDeadline(this, timeoutMs);

        JS::RootedValue out(_context);
        JS::RootedObject obj(_context, smrecv.toObjectOrNull());

        checkBool(JS::Call(_context, obj, funcValue, args, &out));

        if (timeoutMs)
            _engine->getDeadlineMonitor()->stopDeadline(this);

        //if (!nativePrologue()) {
        //    _error = "JavaScript execution terminated";
        //    error() << _error << endl;
        //    uasserted(16712, _error);
        //}

        // throw on error
        //checkV8ErrorState(result, try_catch);

        if (!ignoreReturn) {
            // must validate the handle because TerminateExecution may have
            // been thrown after the above checks
            //if (!resultObject.IsEmpty() && resultObject->Has(strLitToV8("_v8_function"))) {
            //    log() << "storing native function as return value" << endl;
            //    _lastRetIsNativeCode = true;
            //}
            //else {
                _lastRetIsNativeCode = false;
            //}
            _setValue("__returnValue", out);
        }

        return 0;
    }

    bool SMScope::exec(StringData code, const string& name, bool printResult,
                       bool reportError, bool assertOnError, int timeoutMs) {
        SMMAGIC_HEADER;

        JS::CompileOptions co(_context);
        JS::RootedScript script(_context);

        checkBool(JS::Compile(_context, _global, co, code.rawData(), code.size(), &script));

        //if (!nativeEpilogue()) {
        //    _error = "JavaScript execution terminated";
        //    if (reportError)
        //        error() << _error << endl;
        //    if (assertOnError)
        //        uasserted(13475, _error);
        //    return false;
        //}

        if (timeoutMs)
            _engine->getDeadlineMonitor()->startDeadline(this, timeoutMs);

        JS::RootedValue out(_context);

        checkBool(JS_ExecuteScript(_context, _global, script, &out));

        if (timeoutMs)
            _engine->getDeadlineMonitor()->stopDeadline(this);

        //if (!nativePrologue()) {
        //    _error = "JavaScript execution terminated";
        //    if (reportError)
        //        error() << _error << endl;
        //    if (assertOnError)
        //        uasserted(16721, _error);
        //    return false;
        //}

        //if (checkV8ErrorState(result, try_catch, reportError, assertOnError))
        //    return false;

        _setValue("__lastres__", out);

        //if (printResult && !result->IsUndefined()) {
        //    // appears to only be used by shell
        //    cout << V8String(result) << endl;
        //}

        return true;
    }

    void SMScope::injectNative(const char *field, NativeFunction func, void* data) {
        SMMAGIC_HEADER;

        JS::RootedObject proto(_context);
        JS::RootedObject parent(_context);

        JS::RootedObject obj(_context, JS_NewObject(_context, &nativeClass, proto, parent));

        JS_SetPrivate(obj, new NativeHolder(this, func, data));

        JS::RootedValue value(_context);
        value.setObjectOrNull(obj);

        _setValue(field, value);
    }

    void SMScope::gc() {
        _pendingGC.store(true);
        JS_RequestInterruptCallback(_runtime);
    }

    void SMScope::localConnectForDbEval(OperationContext* txn, const char * dbName) {
        //SMMAGIC_HEADER;

        //invariant(_opCtx == NULL);
        //_opCtx = txn;

        //if (_connectState == EXTERNAL)
        //    uasserted(12510, "externalSetup already called, can't call localConnect");
        //if (_connectState ==  LOCAL) {
        //    if (_localDBName == dbName)
        //        return;
        //    uasserted(12511,
        //              str::stream() << "localConnect previously called with name "
        //                            << _localDBName);
        //}

        //// NOTE: order is important here.  the following methods must be called after
        ////       the above conditional statements.

        //// install db access functions in the global object
        //installDBAccess();

        //// install the Mongo function object and instantiate the 'db' global
        //_MongoFT = FTPtr::New(getMongoFunctionTemplate(this, true));
        //injectV8Function("Mongo", MongoFT(), _global);
        //execCoreFiles();
        //exec("_mongo = new Mongo();", "local connect 2", false, true, true, 0);
        //exec((string)"db = _mongo.getDB(\"" + dbName + "\");", "local connect 3",
        //     false, true, true, 0);
        //_connectState = LOCAL;
        //_localDBName = dbName;
        //
        //loadStored(txn);
    }

    void SMScope::externalSetup() {
        // TODO ...
    }

    // ----- internal -----

    void SMScope::reset() {
    }

    void SMScope::installBSONTypes() {
        installOIDProto();
        installNLProto();
    }

    void SMScope::installDBAccess() {
    //    typedef v8::Persistent<v8::FunctionTemplate> FTPtr;
    //    _DBFT             = FTPtr::New(createV8Function(dbInit));
    //    _DBQueryFT        = FTPtr::New(createV8Function(dbQueryInit));
    //    _DBCollectionFT   = FTPtr::New(createV8Function(collectionInit));

    //    // These must be done before calling injectV8Function
    //    DBFT()->InstanceTemplate()->SetNamedPropertyHandler(collectionGetter, collectionSetter);
    //    DBQueryFT()->InstanceTemplate()->SetIndexedPropertyHandler(dbQueryIndexAccess);
    //    DBCollectionFT()->InstanceTemplate()->SetNamedPropertyHandler(collectionGetter,
    //                                                                  collectionSetter);

    //    injectV8Function("DB", DBFT(), _global);
    //    injectV8Function("DBQuery", DBQueryFT(), _global);
    //    injectV8Function("DBCollection", DBCollectionFT(), _global);

    //    // The internal cursor type isn't exposed to the users at all
    //    _InternalCursorFT = FTPtr::New(getInternalCursorFunctionTemplate(this));
    }
    // --- random utils ----

    MONGO_INITIALIZER(JavascriptPrintDomain)(InitializerContext*) {
        jsPrintLogDomain = logger::globalLogManager()->getNamedDomain("javascriptOutput");
        return Status::OK();
    }

} // namespace mongo
