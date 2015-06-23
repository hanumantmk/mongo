/**
 * Copyright (C) 2015 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/scripting/mozjs/oid.h"

#include "mongo/scripting/mozjs/implscope.h"
#include "mongo/scripting/mozjs/objectwrapper.h"
#include "mongo/scripting/mozjs/valuereader.h"
#include "mongo/scripting/mozjs/valuewriter.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {
namespace mozjs {

void OIDInfo::finalize(JSFreeOp* fop, JSObject* obj) {
    auto oid = static_cast<OID*>(JS_GetPrivate(obj));

    if (oid)
        delete oid;
}

void OIDInfo::Functions::toString(JSContext* cx, JS::CallArgs args) {
    ObjectWrapper o(cx, args.thisv());

    if (!o.hasField("str"))
        uasserted(ErrorCodes::BadValue, "Must have \"str\" field");

    JS::RootedValue value(cx);
    o.getValue("str", &value);

    auto str = ValueWriter(cx, value).toString();

    str::stream ss;

    ss << "ObjectId(\"" << str << "\")";
    ValueReader(cx, args.rval()).fromStringData(ss.operator std::string());
}

void OIDInfo::construct(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    auto oid = stdx::make_unique<OID>();

    if (args.length() == 0) {
        oid->init();
    } else {
        auto str = ValueWriter(cx, args.get(0)).toString();

        Scope::validateObjectIdString(str);
        oid->init(str);
    }

    JS::RootedObject thisv(cx);
    scope->getOidProto().newObject(&thisv);
    ObjectWrapper o(cx, thisv);

    o.setString("str", oid->toString());

    JS_SetPrivate(thisv, oid.release());

    args.rval().setObjectOrNull(thisv);
}

}  // namespace mozjs
}  // namespace mongo
