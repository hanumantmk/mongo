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

#include "mongo/scripting/mozjs/numberlong.h"

#include "mongo/scripting/mozjs/implscope.h"
#include "mongo/scripting/mozjs/objectwrapper.h"
#include "mongo/scripting/mozjs/valuereader.h"
#include "mongo/scripting/mozjs/valuewriter.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/text.h"

namespace mongo {
namespace mozjs {

long long NumberLongInfo::ToNumberLong(JSContext* cx, JS::HandleValue thisv) {
    JS::RootedObject obj(cx, thisv.toObjectOrNull());
    return ToNumberLong(cx, obj);
}

long long NumberLongInfo::ToNumberLong(JSContext* cx, JS::HandleObject thisv) {
    ObjectWrapper o(cx, thisv);

    if (!o.hasField("top")) {
        if (!o.hasField("floatApprox"))
            uasserted(ErrorCodes::InternalError, "No top and no floatApprox fields");

        return o.getNumber("floatApprox");
    }

    if (!o.hasField("bottom"))
        uasserted(ErrorCodes::InternalError, "top but no bottom field");

    return ((unsigned long long)((long long)o.getNumber("top") << 32) +
            (unsigned)(o.getNumber("bottom")));
}

void NumberLongInfo::Functions::valueOf(JSContext* cx, JS::CallArgs args) {
    long long out = NumberLongInfo::ToNumberLong(cx, args.thisv());

    args.rval().setDouble(out);
}

void NumberLongInfo::Functions::toNumber(JSContext* cx, JS::CallArgs args) {
    valueOf(cx, args);
}

void NumberLongInfo::Functions::toString(JSContext* cx, JS::CallArgs args) {
    str::stream ss;

    long long val = NumberLongInfo::ToNumberLong(cx, args.thisv());

    const long long limit = 2LL << 30;

    if (val <= -limit || limit <= val)
        ss << "NumberLong(\"" << val << "\")";
    else
        ss << "NumberLong(" << val << ")";

    ValueReader(cx, args.rval()).fromStringData(ss.operator std::string());
}

void NumberLongInfo::construct(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    JS::RootedObject thisv(cx);

    scope->getNumberLongProto().newObject(&thisv);
    ObjectWrapper o(cx, thisv);

    JS::RootedValue floatApprox(cx);
    JS::RootedValue top(cx);
    JS::RootedValue bottom(cx);

    if (args.length() == 0) {
        o.setNumber("floatApprox", 0);
    } else if (args.length() == 1) {
        if (args.get(0).isNumber()) {
            o.setValue("floatApprox", args.get(0));
        } else {
            std::string str = ValueWriter(cx, args.get(0)).toString();

            unsigned long long val = parseLL(str.c_str());

            // values above 2^53 are not accurately represented in JS
            if ((long long)val == (long long)(double)(long long)(val) &&
                val < 9007199254740992ULL) {
                o.setNumber("floatApprox", val);
            } else {
                o.setNumber("floatApprox", val);
                o.setNumber("top", val >> 32);
                o.setNumber("bottom", val & 0x00000000ffffffff);
            }
        }
    } else {
        if (!args.get(0).isNumber())
            uasserted(ErrorCodes::BadValue, "arguments must be numbers");
        if (!args.get(1).isNumber())
            uasserted(ErrorCodes::BadValue, "arguments must be numbers");
        if (!args.get(2).isNumber())
            uasserted(ErrorCodes::BadValue, "arguments must be numbers");

        o.setValue("floatApprox", args.get(0));
        o.setValue("top", args.get(1));
        o.setValue("bottom", args.get(2));
    }

    args.rval().setObjectOrNull(thisv);
}

}  // namespace mozjs
}  // namespace mongo
