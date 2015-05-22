/*    Copyright 2015 MongoDB Inc.
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

#include "mongo/platform/basic.h"

#include "mongo/scripting/mozjs/idwrapper.h"

#include "mongo/base/error_codes.h"
#include "mongo/scripting/mozjs/jsstringwrapper.h"
#include "mongo/util/assert_util.h"

namespace mongo {
namespace mozjs {

IdWrapper::IdWrapper(JSContext* cx, JS::HandleId value) : _context(cx), _value(cx, value) {}

std::string IdWrapper::toString() {
    std::string out;

    if (JSID_IS_STRING(_value)) {
        return JSStringWrapper(_context, JSID_TO_STRING(_value)).toString();
    } else {
        char buf[20];
        snprintf(buf, sizeof(buf), "%i", JSID_TO_INT(_value));
        return buf;
    }
}

uint32_t IdWrapper::toInt32() {
    if (JSID_IS_STRING(_value))
        uasserted(ErrorCodes::BadValue, "id is not an int");

    return JSID_TO_INT(_value);
}

bool IdWrapper::equals(StringData sd) {
    JS::RootedValue x(_context);
    JS_IdToValue(_context, _value, &x);

    bool out;
    if (!JS_StringEqualsAscii(_context, x.toString(), sd.rawData(), &out)) {
        uasserted(ErrorCodes::InternalError, "Failed to stringEqualsAscii");
    }

    return out;
}

bool IdWrapper::isInt() {
    return !JSID_IS_STRING(_value);
}

bool IdWrapper::isString() {
    return JSID_IS_STRING(_value);
}

}  // namespace mozjs
}  // namespace mongo
