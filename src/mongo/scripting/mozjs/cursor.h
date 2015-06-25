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

#pragma once

#include "mongo/scripting/mozjs/wraptype.h"

namespace mongo {
namespace mozjs {

/**
 * Wraps a DBClientCursor in javascript
 *
 * Note that the install is private, so this class should only be constructible
 * from C++. Current callers are all via the Mongo object.
 */
struct CursorInfo {
    static void construct(JSContext* cx, JS::CallArgs args);
    static void finalize(JSFreeOp* fop, JSObject* obj);

    struct Functions {
        MONGO_DEFINE_JS_FUNCTION(hasNext);
        MONGO_DEFINE_JS_FUNCTION(next);
        MONGO_DEFINE_JS_FUNCTION(objsLeftInBatch);
        MONGO_DEFINE_JS_FUNCTION(readOnly);
    };

    const JSFunctionSpec methods[5] = {
        MONGO_ATTACH_JS_FUNCTION(hasNext),
        MONGO_ATTACH_JS_FUNCTION(next),
        MONGO_ATTACH_JS_FUNCTION(objsLeftInBatch),
        MONGO_ATTACH_JS_FUNCTION(readOnly),
        JS_FS_END,
    };

    const char* const className = "Cursor";
    static const unsigned classFlags = JSCLASS_HAS_PRIVATE;
    static const InstallType installType = InstallType::Private;
};

}  // namespace mozjs
}  // namespace mongo
