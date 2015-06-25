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
 * The JSThread javascript object
 *
 * We do an odd dance where we inject this in via _threadInject, then replace
 * this...
 */
struct JSThreadInfo {
    static void finalize(JSFreeOp* fop, JSObject* obj);

    struct Functions {
        MONGO_DEFINE_JS_FUNCTION(init);
        MONGO_DEFINE_JS_FUNCTION(start);
        MONGO_DEFINE_JS_FUNCTION(join);
        MONGO_DEFINE_JS_FUNCTION(hasFailed);
        MONGO_DEFINE_JS_FUNCTION(returnData);

        MONGO_DEFINE_JS_FUNCTION(_threadInject);
        MONGO_DEFINE_JS_FUNCTION(_scopedThreadInject);
    };

    const JSFunctionSpec threadMethods[6] = {
        MONGO_ATTACH_JS_FUNCTION(init),
        MONGO_ATTACH_JS_FUNCTION(start),
        MONGO_ATTACH_JS_FUNCTION(join),
        MONGO_ATTACH_JS_FUNCTION(hasFailed),
        MONGO_ATTACH_JS_FUNCTION(returnData),
        JS_FS_END,
    };

    const JSFunctionSpec freeFunctions[3] = {
        MONGO_ATTACH_JS_FUNCTION(_threadInject),
        MONGO_ATTACH_JS_FUNCTION(_scopedThreadInject),
        JS_FS_END,
    };

    const char* const className = "JSThread";
    static const unsigned classFlags = JSCLASS_HAS_PRIVATE;
    static const InstallType installType = InstallType::Private;
};

}  // namespace mozjs
}  // namespace mongo
