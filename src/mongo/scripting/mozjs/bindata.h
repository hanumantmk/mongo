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
 * Wrapper for the BinData bson type
 *
 * It offers some simple methods and a handful of specialized constructors
 */
struct BinDataInfo {
    static void construct(JSContext* cx, JS::CallArgs args);
    static void finalize(JSFreeOp* fop, JSObject* obj);

    struct Functions {
        MONGO_DEFINE_JS_FUNCTION(base64);
        MONGO_DEFINE_JS_FUNCTION(hex);
        MONGO_DEFINE_JS_FUNCTION(toString);

        MONGO_DEFINE_JS_FUNCTION(HexData);
        MONGO_DEFINE_JS_FUNCTION(MD5);
        MONGO_DEFINE_JS_FUNCTION(UUID);
    };

    const JSFunctionSpec methods[4] = {
        MONGO_ATTACH_JS_FUNCTION(base64),
        MONGO_ATTACH_JS_FUNCTION(hex),
        MONGO_ATTACH_JS_FUNCTION(toString),
        JS_FS_END,
    };

    const JSFunctionSpec freeFunctions[4] = {
        MONGO_ATTACH_JS_FUNCTION_WITH_FLAGS(HexData, JSFUN_CONSTRUCTOR),
        MONGO_ATTACH_JS_FUNCTION_WITH_FLAGS(MD5, JSFUN_CONSTRUCTOR),
        MONGO_ATTACH_JS_FUNCTION_WITH_FLAGS(UUID, JSFUN_CONSTRUCTOR),
        JS_FS_END,
    };

    const char* const className = "BinData";
    static const unsigned classFlags = JSCLASS_HAS_PRIVATE;
};

}  // namespace mozjs
}  // namespace mongo
