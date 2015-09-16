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

#include <boost/optional.hpp>
#include <jsapi.h>
#include <string>

#include "mongo/base/alloc_only_pool_allocator.h"
#include "mongo/base/contiguous_stack.h"
#include "mongo/bson/bsonobjbuilder.h"

namespace mongo {
namespace mozjs {

/**
 * The state need to write a single level of a nested javascript object as a
 * bson object.
 *
 * We use this between ObjectWrapper and ValueWriter to avoid recursion in
 * translating js to bson.
 */
struct WriteThisFrame {
    WriteThisFrame(JSContext* cx, JSObject* obj, BSONObjBuilder* parent, StringData sd);

    BSONObjBuilder* subbob_or(BSONObjBuilder* option) {
        return subbob ? &subbob.get() : option;
    }

    JS::RootedObject thisv;
    JS::AutoIdArray ids;
    std::size_t idx = 0;
    boost::optional<BSONObjBuilder> subbob;
    BSONObj* originalBSON = nullptr;
    bool altered = true;
};

/**
 * Synthetic stack of variables for writeThis
 *
 * We use a ContiguousStack here because we have SpiderMonkey Rooting types
 * which are non-copyable and non-movable. They also have to actually be on the
 * stack, so we use an alloc only pool allocator with our stack so we can back
 * the stack with stack allocated memory.
 */
using WriteThisFrames = ContiguousStack<WriteThisFrame, AllocOnlyPoolAllocator<WriteThisFrame>>;

}  // namespace mozjs
}  // namespace mongo
