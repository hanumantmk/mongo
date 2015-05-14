// v8_db.cpp

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

#include "mongo/scripting/v8-sm_db.h"

#include "mongo/stdx/memory.h"
#include "mongo/util/text.h"

namespace mongo {

    struct CursorImpl {
        static void finalize(JSFreeOp *fop, JSObject *obj) {
            auto cursor = static_cast<DBClientCursor*>(JS_GetPrivate(obj));

            delete cursor;
        }

        static bool construct(JSContext *cx, unsigned argc, JS::Value *vp) {
            return true;
        }
    };

    struct CursorMethods {
        static bool next(JSContext *cx, unsigned argc, JS::Value *vp) {
            JS::RootedValue rval(cx);
            JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
            auto cursor = static_cast<mongo::DBClientCursor*>(JS_GetPrivate(&args.callee()));
            auto scope = static_cast<SMScope*>(JS_GetContextPrivate(cx));

            if (! cursor) {
                // TODO something different?
                return false;
            }

            BSONObj o = cursor->next();
            bool ro = false;
            //if (args.This()->Has(v8::String::New("_ro")))
            //    ro = args.This()->Get(v8::String::New("_ro"))->BooleanValue();
            scope->mongoToLZSM(o, ro, &rval);

            args.rval().set(rval);

            return true;
        }

        static bool hasNext(JSContext *cx, unsigned argc, JS::Value *vp) {
            JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
            auto cursor = static_cast<mongo::DBClientCursor*>(JS_GetPrivate(&args.callee()));

            if (! cursor) {
                // TODO something different?
                return false;
            }

            args.rval().setBoolean(cursor->more());

            return true;
        }

        static bool objsLeftInBatch(JSContext *cx, unsigned argc, JS::Value *vp) {
            JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
            auto cursor = static_cast<mongo::DBClientCursor*>(JS_GetPrivate(&args.callee()));

            if (! cursor) {
                // TODO something different?
                return false;
            }

            args.rval().setInt32(cursor->objsLeftInBatch());

            return true;
        }

        static bool readOnly(JSContext *cx, unsigned argc, JS::Value *vp) {
            JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
            JS::RootedObject cursor(cx, args.thisv().toObjectOrNull());

            JS::RootedValue value(cx);
            value.setBoolean(true);

            JS_SetProperty(cx, cursor, "_ro", value);

            args.rval().setObjectOrNull(cursor);

            return true;
        }
    };

    static constexpr JSClass cursorClass = {
        "mongoCursor",
        JSCLASS_HAS_PRIVATE,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        CursorImpl::finalize,
    };

    static constexpr JSFunctionSpec cursorMethodSpec[] {
        JS_FS("next", CursorMethods::next, 0, 0),
        JS_FS("hasNext", CursorMethods::hasNext, 0, 0),
        JS_FS("objsLeftInBatch", CursorMethods::objsLeftInBatch, 0, 0),
        JS_FS("readOnly", CursorMethods::readOnly, 0, 0),
        JS_FS_END,
    };

    void SMScope::makeCursor(DBClientCursor* cursor, JS::MutableHandleValue out) {
        JS::RootedObject proto(_context);
        JS::RootedObject parent(_context);

        JS::RootedObject cursorObj(_context, JS_NewObject(
            _context,
            &cursorClass,
            proto,
            parent
        ));

        checkBool(JS_DefineFunctions(_context, cursorObj, cursorMethodSpec));

        JS_SetPrivate(cursorObj, cursor);
    }

    struct OIDImpl {
        static void finalize(JSFreeOp *fop, JSObject *obj) {
            auto oid = static_cast<OID*>(JS_GetPrivate(obj));

            if (oid) {
                delete oid;
            }
        }

        static bool construct(JSContext *cx, unsigned argc, JS::Value *vp);
    };

    static constexpr JSClass oidClass = {
        "mongoOID",
        JSCLASS_HAS_PRIVATE,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        OIDImpl::finalize,
        OIDImpl::construct,
        nullptr,
        OIDImpl::construct,
    };

    bool OIDImpl::construct(JSContext *cx, unsigned argc, JS::Value *vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
        auto scope = static_cast<SMScope*>(JS_GetContextPrivate(cx));

        auto oid = stdx::make_unique<OID>();

        if (args.length() == 0) {
            oid->init();
        } else {
            std::string s(scope->toSTLString(args.get(0).toString()));

            try {
                Scope::validateObjectIdString(s);
            } catch (const MsgAssertionException& m) {
                return false;
            }
            oid->init(s);
        }

        JS::RootedObject parent(cx);
        JS::RootedObject proto(cx);
        JS::RootedObject thisv(cx, JS_NewObject(cx, &oidClass, proto, parent));

        JS::RootedValue jsStr(cx);
        scope->fromStringData(oid->toString(), &jsStr);

        JS_SetProperty(cx, thisv, "str", jsStr);

        JS_SetPrivate(thisv, oid.release());

        args.rval().setObjectOrNull(thisv);
        
        return true;
    }

    void SMScope::installOIDProto() {
        JS::RootedObject proto(_context);
        JS::RootedObject parent(_context);

        _oidProto.set(JS_InitClass(
            _context,
            _global,
            proto,
            &oidClass,
            OIDImpl::construct,
            0,
            nullptr,
            nullptr,
            nullptr,
            nullptr
        ));

        JS::RootedValue value(_context);

        value.setObjectOrNull(_oidProto);

        _setValue("ObjectId", value);
    }

    void SMScope::makeOID(JS::MutableHandleValue out) {
        JS::AutoValueVector args(_context);

        out.setObjectOrNull(JS_New(_context, _oidProto, args));
    }

    struct NumberLongMethods {
        static long long numberLongVal(JSContext *cx, JS::HandleObject thisv) {
            JS::RootedValue floatApprox(cx);
            JS::RootedValue top(cx);
            JS::RootedValue bottom(cx);
            bool hasTop;

            JS_HasProperty(cx, thisv, "top", &hasTop);

            if (! hasTop) {
                JS_GetProperty(cx, thisv, "floatApprox", &floatApprox);

                return (long long)(floatApprox.toNumber());
            }

            JS_GetProperty(cx, thisv, "top", &top);
            JS_GetProperty(cx, thisv, "bottom", &bottom);

            return
                (long long)
                ((unsigned long long)((long long)top.toInt32() << 32) +
                (unsigned)(bottom.toInt32()));
        }

        static bool construct(JSContext *cx, unsigned argc, JS::Value *vp);

        static bool valueOf(JSContext *cx, unsigned argc, JS::Value *vp) {
            JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
            JS::RootedObject thisv(cx, args.thisv().toObjectOrNull());

            args.rval().setDouble(numberLongVal(cx, thisv));

            return true;
        }

        static bool toNumber(JSContext *cx, unsigned argc, JS::Value *vp) {
            return valueOf(cx, argc, vp);
        }

        static bool toString(JSContext *cx, unsigned argc, JS::Value *vp) {
            JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
            JS::RootedObject thisv(cx, args.thisv().toObjectOrNull());
            auto scope = static_cast<SMScope*>(JS_GetContextPrivate(cx));

            std::stringstream ss;
            long long val = numberLongVal(cx, thisv);
            const long long limit = 2LL << 30;

            if (val <= -limit || limit <= val)
                ss << "NumberLong(\"" << val << "\")";
            else
                ss << "NumberLong(" << val << ")";

            std::string ret = ss.str();

            scope->fromStringData(ret, args.rval());

            return true;
        }
    };

#define MONGO_JS_FLAGS \
  (JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT)

    static constexpr JSFunctionSpec numberLongMethods[] {
        JS_FS("valueOf", NumberLongMethods::valueOf, 0, MONGO_JS_FLAGS),
        JS_FS("toNumber", NumberLongMethods::toNumber, 0, MONGO_JS_FLAGS),
        JS_FS("toString", NumberLongMethods::toString, 0, MONGO_JS_FLAGS),
        JS_FS_END,
    };

    static constexpr JSClass NumberLongClass = {
        "mongoNumberLong",
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        NumberLongMethods::construct,
        nullptr,
        NumberLongMethods::construct,
    };

    bool NumberLongMethods::construct(JSContext *cx, unsigned argc, JS::Value *vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
        auto scope = static_cast<SMScope*>(JS_GetContextPrivate(cx));

        JS::RootedObject parent(cx);
        JS::RootedObject proto(cx);
        JS::RootedObject thisv(cx, JS_NewObject(cx, &NumberLongClass, proto, parent));

        JS::RootedValue floatApprox(cx);
        JS::RootedValue top(cx);
        JS::RootedValue bottom(cx);

        if (args.length() == 0) {
            floatApprox.setDouble(0);
            JS_SetProperty(cx, thisv, "floatApprox", floatApprox);
        } else if (args.length() == 1) {
            if (args.get(0).isNumber()) {
                floatApprox.set(args.get(0));
                JS_SetProperty(cx, thisv, "floatApprox", floatApprox);
            } else {
                auto num = scope->toSTLString(args.get(0).toString());
                const char *numStr = num.c_str();
                long long n;
                try {
                    n = parseLL(numStr);
                }
                catch (const AssertionException&) {
                    return false;
//                        return v8AssertionException(string("could not convert \"") +
//                                                    num +
//                                                    "\" to NumberLong");
                }
                unsigned long long val = n;
                // values above 2^53 are not accurately represented in JS
                if ((long long)val ==
                    (long long)(double)(long long)(val) && val < 9007199254740992ULL) {

                    floatApprox.setDouble((double)(long long)(val));
                    JS_SetProperty(cx, thisv, "floatApprox", floatApprox);
                } else {
                    floatApprox.setDouble((double)(long long)(val));
                    JS_SetProperty(cx, thisv, "floatApprox", floatApprox);

                    top.setNumber(uint32_t(val >> 32));
                    bottom.setNumber(uint32_t((unsigned long)(val & 0x00000000ffffffff)));

                    JS_SetProperty(cx, thisv, "top", top);
                    JS_SetProperty(cx, thisv, "bottom", bottom);
                }
            }
        }
        else {
            floatApprox.set(args.get(0));
            top.setNumber(uint32_t(args.get(1).toInt32()));
            bottom.setNumber(uint32_t(args.get(2).toInt32()));

            JS_SetProperty(cx, thisv, "floatApprox", floatApprox);
            JS_SetProperty(cx, thisv, "top", top);
            JS_SetProperty(cx, thisv, "bottom", bottom);
        }

        args.rval().setObjectOrNull(thisv);

        return true;
    }

    void SMScope::installNLProto() {
        JS::RootedObject proto(_context);
        JS::RootedObject parent(_context);

        _numberLongProto.set(JS_InitClass(
            _context,
            _global,
            proto,
            &NumberLongClass,
            NumberLongMethods::construct,
            0,
            nullptr,
            numberLongMethods,
            nullptr,
            nullptr
        ));

        JS::RootedValue value(_context);

        value.setObjectOrNull(_numberLongProto);

        _setValue("NumberLong", value);
    }

    void SMScope::makeNL(JS::MutableHandleValue out) {
        JS::AutoValueVector args(_context);

        out.setObjectOrNull(JS_New(_context, _numberLongProto, args));
    }
}
