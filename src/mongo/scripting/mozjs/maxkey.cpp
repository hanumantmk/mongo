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

#include "mongo/scripting/mozjs/maxkey.h"

#include "mongo/scripting/mozjs/implscope.h"
#include "mongo/scripting/mozjs/objectwrapper.h"
#include "mongo/scripting/mozjs/valuereader.h"

namespace mongo {
namespace mozjs {

namespace {
const char* const kSingleton = "singleton";
}  // namespace

void MaxKeyInfo::construct(JSContext* cx, JS::CallArgs args) {
    call(cx, args);
}

/**
 * The idea here is that MinKey and MaxKey are singleton callable objects that
 * return the singleton when called. This enables all instances to compare
 * == and === to MinKey even if created by "new MinKey()" in JS.
 */
void MaxKeyInfo::call(JSContext* cx, JS::CallArgs args) {
    auto scope = getScope(cx);

    ObjectWrapper o(cx, scope->getMaxKeyProto().getProto());

    JS::RootedValue val(cx);

    if (!o.hasField(kSingleton)) {
        JS::RootedObject thisv(cx);
        scope->getMaxKeyProto().newObject(&thisv);

        val.setObjectOrNull(thisv);
        o.setValue(kSingleton, val);
    } else {
        o.getValue(kSingleton, &val);
    }

    args.rval().setObjectOrNull(val.toObjectOrNull());
}

void MaxKeyInfo::Functions::tojson(JSContext* cx, JS::CallArgs args) {
    ValueReader(cx, args.rval()).fromStringData("{ \"$maxKey\" : 1 }");
}

void MaxKeyInfo::postInstall(JSContext* cx, JS::HandleObject global, JS::HandleObject proto) {
    ObjectWrapper protoWrapper(cx, proto);

    JS::RootedValue value(cx);
    getScope(cx)->getMaxKeyProto().newObject(&value);

    ObjectWrapper(cx, global).setValue("MaxKey", value);
    protoWrapper.setValue(kSingleton, value);
}

}  // namespace mozjs
}  // namespace mongo
