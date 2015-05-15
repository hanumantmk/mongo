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

#include "mongo/client/dbclientcursor.h"
#include "mongo/scripting/engine_v8-sm.h"

#define MONGO_JS_FLAGS (JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT)

namespace mongo {

    namespace {
        JSFunctionSpec _oidMethods[] {
            JS_FS("toString", OIDClass::Methods::toString, 0, MONGO_JS_FLAGS),
            JS_FS_END,
        };
    }

    JSFunctionSpec* OIDClass::methods = _oidMethods;

    const char* OIDClass::className = "ObjectId";

    void OIDClass::finalize(JSFreeOp *fop, JSObject *obj) {
        auto oid = static_cast<OID*>(JS_GetPrivate(obj));

        if (oid) {
            delete oid;
        }
    }

    bool OIDClass::Methods::toString(JSContext *cx, unsigned argc, JS::Value *vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
        JS::RootedObject thisv(cx, args.thisv().toObjectOrNull());
        auto scope = static_cast<SMScope*>(JS_GetContextPrivate(cx));

        JS::RootedValue value(cx);

        if (! JS_GetProperty(cx, thisv, "str", &value)) return false;

        auto str = scope->toSTLString(value.toString());

        std::stringstream ss;

        ss << "ObjectId(\"" << str << "\")";

        std::string ret = ss.str();

        scope->fromStringData(ret, args.rval());

        return true;
    }


    bool OIDClass::construct(JSContext *cx, unsigned argc, JS::Value *vp) {
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
        JS::RootedObject thisv(cx, JS_NewObject(cx, &scope->_oid.jsclass, proto, parent));

        JS::RootedValue jsStr(cx);
        scope->fromStringData(oid->toString(), &jsStr);

        JS_SetProperty(cx, thisv, "str", jsStr);

        JS_SetPrivate(thisv, oid.release());

        args.rval().setObjectOrNull(thisv);
        
        return true;
    }

    bool OIDClass::call(JSContext *cx, unsigned argc, JS::Value *vp) {
        return construct(cx, argc, vp);
    }

    namespace {
        JSFunctionSpec _numberLongMethods[] {
            JS_FS("valueOf", NumberLongClass::Methods::valueOf, 0, MONGO_JS_FLAGS),
            JS_FS("toNumber", NumberLongClass::Methods::toNumber, 0, MONGO_JS_FLAGS),
            JS_FS("toString", NumberLongClass::Methods::toString, 0, MONGO_JS_FLAGS),
            JS_FS_END,
        };
    }

    JSFunctionSpec* NumberLongClass::methods = _numberLongMethods;

    const char* NumberLongClass::className = "NumberLong";

    long long NumberLongClass::Methods::numberLongVal(JSContext *cx, JS::HandleObject thisv) {
        JS::RootedValue floatApprox(cx);
        JS::RootedValue top(cx);
        JS::RootedValue bottom(cx);
        bool hasTop;

        if (! JS_HasProperty(cx, thisv, "top", &hasTop)) return false;

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

    bool NumberLongClass::Methods::valueOf(JSContext *cx, unsigned argc, JS::Value *vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
        JS::RootedObject thisv(cx, args.thisv().toObjectOrNull());

        args.rval().setDouble(numberLongVal(cx, thisv));

        return true;
    }

    bool NumberLongClass::Methods::toNumber(JSContext *cx, unsigned argc, JS::Value *vp) {
        return valueOf(cx, argc, vp);
    }

    bool NumberLongClass::Methods::toString(JSContext *cx, unsigned argc, JS::Value *vp) {
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

    bool NumberLongClass::construct(JSContext *cx, unsigned argc, JS::Value *vp) {
        JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
        auto scope = static_cast<SMScope*>(JS_GetContextPrivate(cx));

        JS::RootedObject parent(cx);
        JS::RootedObject proto(cx);
        JS::RootedObject thisv(cx, JS_NewObject(cx, &scope->_numberLong.jsclass, proto, parent));

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

    bool NumberLongClass::call(JSContext *cx, unsigned argc, JS::Value *vp) {
        return construct(cx, argc, vp);
    }
}
