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

#include "mongo/scripting/mozjs/object.h"

#include "mongo/scripting/mozjs/implscope.h"
#include "mongo/scripting/mozjs/objectwrapper.h"
#include "mongo/scripting/mozjs/valuereader.h"
#include "mongo/scripting/mozjs/valuewriter.h"

namespace mongo {
namespace mozjs {

constexpr JSFunctionSpec ObjectInfo::methods[];
constexpr char ObjectInfo::className[];

void ObjectInfo::Functions::bsonsize(JSContext* cx, JS::CallArgs args) {
    if (args.length() != 1)
        uasserted(ErrorCodes::BadValue, "bsonsize needs 1 argument");

    if (args.get(0).isNull()) {
        args.rval().setInt32(0);
        return;
    }

    if (!args.get(0).isObject())
        uasserted(ErrorCodes::BadValue, "argument to bsonsize has to be an object");

    args.rval().setInt32(ValueWriter(cx, args.get(0)).toBSON().objsize());
}

void ObjectInfo::Functions::invalidForStorage(JSContext* cx, JS::CallArgs args) {
    if (args.length() != 1)
        uasserted(ErrorCodes::BadValue, "invalidForStorage needs 1 argument");

    if (args.get(0).isNull()) {
        args.rval().setUndefined();
        return;
    }

    if (!args.get(0).isObject())
        uasserted(ErrorCodes::BadValue, "argument to invalidForStorage has to be an object");

    Status validForStorage = ValueWriter(cx, args.get(0)).toBSON().storageValid(true);
    if (validForStorage.isOK()) {
        args.rval().setNull();
        return;
    }

    std::string errmsg = str::stream() << validForStorage.codeString() << ": "
                                       << validForStorage.reason();

    ValueReader(cx, args.rval()).fromStringData(errmsg);
}

}  // namespace mozjs
}  // namespace mongo
