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

#include "mongo/scripting/mozjs/countdownlatch.h"

#include <unordered_map>

#include "mongo/scripting/mozjs/implscope.h"
#include "mongo/scripting/mozjs/objectwrapper.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/mutex.h"

namespace mongo {
namespace mozjs {

class CountDownLatchHolder {
private:
    struct Latch {
        Latch(int32_t count) : count(count) {}

        stdx::condition_variable cv;
        stdx::mutex mutex;
        int32_t count;
    };

    std::shared_ptr<Latch> get(int32_t desc) {
        stdx::lock_guard<stdx::mutex> lock(_mutex);
        Map::iterator iter = _latches.find(desc);
        uassert(ErrorCodes::JSInterpreterFailure,
                "not a valid CountDownLatch descriptor",
                iter != _latches.end());
        return iter->second;
    }

    typedef std::unordered_map<int32_t, std::shared_ptr<Latch>> Map;
    Map _latches;
    stdx::mutex _mutex;
    int32_t _counter;

public:
    CountDownLatchHolder() : _counter(0) {}
    int32_t make(int32_t count) {
        uassert(ErrorCodes::JSInterpreterFailure, "argument must be >= 0", count >= 0);
        stdx::lock_guard<stdx::mutex> lock(_mutex);
        int32_t desc = ++_counter;
        _latches.insert(std::make_pair(desc, std::make_shared<Latch>(count)));
        return desc;
    }
    void await(int32_t desc) {
        std::shared_ptr<Latch> latch = get(desc);
        stdx::unique_lock<stdx::mutex> lock(latch->mutex);
        while (latch->count != 0) {
            latch->cv.wait(lock);
        }
    }
    void countDown(int32_t desc) {
        std::shared_ptr<Latch> latch = get(desc);
        stdx::unique_lock<stdx::mutex> lock(latch->mutex);
        if (latch->count > 0) {
            latch->count--;
        }
        if (latch->count == 0) {
            latch->cv.notify_all();
        }
    }
    int32_t getCount(int32_t desc) {
        std::shared_ptr<Latch> latch = get(desc);
        stdx::unique_lock<stdx::mutex> lock(latch->mutex);
        return latch->count;
    }
};

namespace {
CountDownLatchHolder globalCountDownLatchHolder;
}  // namespace

void CountDownLatchInfo::Functions::_new(JSContext* cx, JS::CallArgs args) {
    uassert(ErrorCodes::JSInterpreterFailure, "need exactly one argument", args.length() == 1);
    uassert(ErrorCodes::JSInterpreterFailure, "argument must be an integer", args.get(0).isInt32());

    args.rval().setInt32(globalCountDownLatchHolder.make(args.get(0).toInt32()));
}

void CountDownLatchInfo::Functions::_await(JSContext* cx, JS::CallArgs args) {
    uassert(ErrorCodes::JSInterpreterFailure, "need exactly one argument", args.length() == 1);
    uassert(ErrorCodes::JSInterpreterFailure, "argument must be an integer", args.get(0).isInt32());

    globalCountDownLatchHolder.await(args.get(0).toInt32());

    args.rval().setUndefined();
}

void CountDownLatchInfo::Functions::_countDown(JSContext* cx, JS::CallArgs args) {
    uassert(ErrorCodes::JSInterpreterFailure, "need exactly one argument", args.length() == 1);
    uassert(ErrorCodes::JSInterpreterFailure, "argument must be an integer", args.get(0).isInt32());

    globalCountDownLatchHolder.countDown(args.get(0).toInt32());

    args.rval().setUndefined();
}

void CountDownLatchInfo::Functions::_getCount(JSContext* cx, JS::CallArgs args) {
    uassert(ErrorCodes::JSInterpreterFailure, "need exactly one argument", args.length() == 1);
    uassert(ErrorCodes::JSInterpreterFailure, "argument must be an integer", args.get(0).isInt32());

    args.rval().setInt32(globalCountDownLatchHolder.getCount(args.get(0).toInt32()));
}

void CountDownLatchInfo::postInstall(JSContext* cx, JS::HandleObject global, JS::HandleObject proto) {
    auto scope = getScope(cx);

    JS::RootedValue val(cx);
    scope->getCountDownLatchProto().newObject(&val);

    ObjectWrapper(cx, global).setValue("CountDownLatch", val);
}

}  // namespace mozjs
}  // namespace mongo
