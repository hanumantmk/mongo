/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <experimental/coroutine>

#include "mongo/util/if_constexpr.h"
#include "mongo/util/future.h"

namespace mongo {
namespace coroutine {

template <typename T>
class FuturePromise {
public:
    auto initial_suspend() { return std::experimental::suspend_always(); }
    auto final_suspend () { return std::experimental::suspend_always(); }
    auto get_return_object() {
        return std::move(_future);
    }

    void return_value(T val) {
        _future = std::move(val);
    }

    void unhandled_exception() {
        _future = exceptionToStatus();
    }

private:
    Future<T> _future;
};

template <>
class FuturePromise<void> {
public:
    auto initial_suspend() { return std::experimental::suspend_always(); }
    auto final_suspend () { return std::experimental::suspend_always(); }
    auto get_return_object() {
        return std::move(_future);
    }

    void return_void() {
        _future = Future<void>::makeReady();
    }

    void unhandled_exception() {
        _future = exceptionToStatus();
    }

private:
    Future<void> _future;
};

template <typename T>
class FutureAwaitable {
    const static inline Status kUninitialized = Status(ErrorCodes::BadValue, "uninitialized");

public:
    explicit FutureAwaitable(Future<T>&& future) noexcept : _future(std::move(future)) {}

    bool await_ready() {
        if (_future.isReady()) {
            _storage = std::move(_future.getNoThrow());

            return true;
        }

        return false;
    }

    T await_resume() {
        return uassertStatusOK(std::move(_storage));
    }

    void await_suspend(std::experimental::coroutine_handle<> h) {
        std::move(_future).getAsync([this, h](StatusOrStatusWith<T> swT) mutable {
            _storage = std::move(swT);
            h.resume();
        });
    }

private:

    Future<T> _future;
    StatusOrStatusWith<T> _storage = kUninitialized;
};
}  // namespace coroutine

template <typename T>
inline coroutine::FutureAwaitable<T>
operator co_await(Future<T>&& future) noexcept {
    return coroutine::FutureAwaitable<T>(std::move(future));
}

template <typename T>
struct Future<T>::promise_type : public coroutine::FuturePromise<T> {
};

}  // namespace mongo
