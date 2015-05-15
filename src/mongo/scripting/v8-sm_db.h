// v8_db.h

/*    Copyright 2014 MongoDB Inc.
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

#include <initializer_list>

#define JS_USE_CUSTOM_ALLOCATOR

#include "jsapi.h"
#include "jscustomallocator.h"

#include "mongo/scripting/sm_class.hpp"

namespace mongo {
    struct NumberLongClass {
        static bool construct(JSContext *cx, unsigned argc, JS::Value *vp);
        static bool call(JSContext *cx, unsigned argc, JS::Value *vp);

        struct Methods {
            static long long numberLongVal(JSContext *cx, JS::HandleObject thisv);

            static bool valueOf(JSContext *cx, unsigned argc, JS::Value *vp);
            static bool toNumber(JSContext *cx, unsigned argc, JS::Value *vp);
            static bool toString(JSContext *cx, unsigned argc, JS::Value *vp);
        };

        static constexpr JSFunctionSpec methods[] = {
            MONGO_SM_FS(valueOf),
            MONGO_SM_FS(toNumber),
            MONGO_SM_FS(toString),
            JS_FS_END,
        };

        static constexpr char className[] = "NumberLong";
        static const int classFlags = 0;
    };

    struct OIDClass {
        static bool construct(JSContext *cx, unsigned argc, JS::Value *vp);
        static bool call(JSContext *cx, unsigned argc, JS::Value *vp);
        static void finalize(JSFreeOp *fop, JSObject *obj);

        struct Methods {
            static bool toString(JSContext *cx, unsigned argc, JS::Value *vp);
        };

        static constexpr JSFunctionSpec methods[] = {
            MONGO_SM_FS(toString),
            JS_FS_END,
        };

        static constexpr char className[] = "ObjectId";
        static const int classFlags = JSCLASS_HAS_PRIVATE;
    };
}
