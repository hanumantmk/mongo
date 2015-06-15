/*    Copyright 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
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

#pragma once

#include <tuple>

#include "mongo/bson/bsonobj.h"
#include "mongo/scripting/mozjs/wraptype.h"

namespace mongo {
namespace mozjs {

struct BSONInfo {
    static void construct(JSContext* cx, JS::CallArgs args);
    static void delProperty(JSContext* cx, JS::HandleObject obj, JS::HandleId id, bool* succeeded);
    static void enumerate(JSContext* cx, JS::HandleObject obj, JS::AutoIdVector& properties);
    static void finalize(JSFreeOp* fop, JSObject* obj);
    static void resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId id, bool* resolvedp);
    static void setProperty(JSContext* cx,
                            JS::HandleObject obj,
                            JS::HandleId id,
                            bool strict,
                            JS::MutableHandleValue vp);

    static constexpr char className[] = "BSON";
    static const int classFlags = JSCLASS_HAS_PRIVATE;
    static constexpr InstallType installType = InstallType::Private;

    struct Functions {
        MONGO_DEFINE_JS_FUNCTION(bsonWoCompare);
    };

    static constexpr JSFunctionSpec freeFunctions[] = {
        MONGO_ATTACH_JS_FUNCTION(bsonWoCompare), JS_FS_END,
    };

    static std::tuple<BSONObj*, bool> originalBSON(JSContext* cx, JS::HandleObject obj);
    static void make(JSContext* cx, JS::MutableHandleObject obj, BSONObj bson, bool ro);

    static void postInstall(JSContext* cx, JS::HandleObject global, JS::HandleObject proto);
};

}  // namespace mozjs
}  // namespace mongo
