/**
 * Copyright (C) 2017 MongoDB Inc.
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

#include "mongo/stdx/chrono.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/list.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/concurrency/with_lock.h"

namespace mongo {

class FifoSemaphore {
public:
    FifoSemaphore(size_t value) : _value(value) {}

    void lock() {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        invariant(_tryLockWithWaiter(lk, [&](stdx::condition_variable* cv, auto&& pred) {
            cv->wait(lk, pred);
            return true;
        }));
    }

    bool try_lock() {
        stdx::lock_guard<stdx::mutex> lk(_mutex);
        return _tryLock(lk);
    }

    template <typename Rep, typename Period>
    bool try_lock_for(const stdx::chrono::duration<Rep, Period>& timeoutDuration) {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        return _tryLockWithWaiter(lk, [&](stdx::condition_variable* cv, auto&& pred) {
            return cv->wait_for(lk, timeoutDuration, pred);
        });
    }

    template <typename Clock, typename Duration>
    bool try_lock_until(const stdx::chrono::time_point<Clock, Duration>& timeoutTime) {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        return _tryLockWithWaiter(lk, [&](stdx::condition_variable* cv, auto&& pred) {
            return cv->wait_until(lk, timeoutTime, pred);
        });
    }


    void unlock() {
        stdx::lock_guard<stdx::mutex> lk(_mutex);
        _value++;

        _notifyHead(lk);
    }

    size_t value() const {
        stdx::lock_guard<stdx::mutex> lk(_mutex);
        return _value;
    }

    size_t waiters() const {
        stdx::lock_guard<stdx::mutex> lk(_mutex);
        return _waiters.size();
    }

private:
    bool _tryLock(WithLock) {
        if (_value && _waiters.empty()) {
            _value--;
            return true;
        }

        return false;
    }

    template <typename Waiter>
    bool _tryLockWithWaiter(WithLock withLock, Waiter&& waiter) {
        if (_tryLock(withLock)) {
            return true;
        }

        _waiters.emplace_back();
        auto* cv = &_waiters.back();

        if (!waiter(cv, [&] { return _value && cv == &(_waiters.front()); })) {
            return false;
        }

        _waiters.pop_front();

        _value--;

        _notifyHead(withLock);

        return true;
    }

    void _notifyHead(WithLock) {
        if (_value && _waiters.size()) {
            _waiters.front().notify_one();
        }
    }

    size_t _value;
    size_t _counter = 0;
    mutable stdx::mutex _mutex;
    stdx::list<stdx::condition_variable> _waiters;
};

}  // namespace mongo
