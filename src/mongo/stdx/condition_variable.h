/**
 *    Copyright (C) 2015 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <condition_variable>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "mongo/stdx/chrono.h"
#include "mongo/stdx/mutex.h"

namespace mongo {
namespace stdx {

using cv_status = ::std::cv_status;  // NOLINT

namespace detail {

template <typename T>
class condition_variable_methods {
public:
    template <class Predicate>
    void wait(stdx::unique_lock<stdx::mutex>& lock, Predicate pred) {
        while (!pred()) {
            static_cast<T*>(this)->wait(lock);
        }
    }

    template <class Rep, class Period>
    stdx::cv_status wait_for(stdx::unique_lock<stdx::mutex>& lock,
                             const stdx::chrono::duration<Rep, Period>& rel_time) {
        return static_cast<T*>(this)->wait_until(lock,
                                                 stdx::chrono::system_clock::now() + rel_time);
    }

    template <class Rep, class Period, class Predicate>
    bool wait_for(stdx::unique_lock<stdx::mutex>& lock,
                  const stdx::chrono::duration<Rep, Period>& rel_time,
                  Predicate pred) {
        return static_cast<T*>(this)->wait_until(
            lock, stdx::chrono::system_clock::now() + rel_time, std::move(pred));
    }

    template <class Clock, class Duration, class Pred>
    bool wait_until(stdx::unique_lock<stdx::mutex>& lock,
                    const stdx::chrono::time_point<Clock, Duration>& timeout_time,
                    Pred pred) {
        while (!pred()) {
            if (static_cast<T*>(this)->wait_until(lock, timeout_time) == stdx::cv_status::timeout) {
                return pred();
            }
        }
        return true;
    }
};
}

#ifdef _WIN32

class condition_variable : public detail::condition_variable_methods<condition_variable> {
public:
    using native_handle_type = PCONDITION_VARIABLE;

    condition_variable() noexcept = default;

    condition_variable(const condition_variable&) = delete;
    condition_variable& operator=(const condition_variable&) = delete;

    void notify_one() noexcept {
        return WakeConditionVariable(&_impl);
    }

    void notify_all() noexcept {
        return WakeAllConditionVariable(&_impl);
    }

    void wait(stdx::unique_lock<stdx::mutex>& lock) noexcept {
        if (!SleepConditionVariableSRW(&_impl, lock.mutex()->native_handle(), INFINITE, 0)) {
            std::terminate();
        }
    }

    using detail::condition_variable_methods<condition_variable>::wait;

    template <class Clock, class Duration>
    stdx::cv_status wait_until(stdx::unique_lock<stdx::mutex>& lock,
                               const stdx::chrono::time_point<Clock, Duration>& timeout_time) {
        auto ms =
            stdx::chrono::duration_cast<stdx::chrono::milliseconds>(timeout_time - Clock::now());

        bool success = SleepConditionVariableSRW(&_impl, lock.mutex()->native_handle(), ms, 0);
        if (!success) {
            auto err = GetLastError();
            if (err != ERROR_TIMEOUT) {
                throw std::system_error(err, std::system_category(), "failed condvar timedwait");
            } else {
                return stdx::cv_status::timeout;
            }
        }

        return (Clock::now() < timeout_time ? stdx::cv_status::no_timeout
                                            : stdx::cv_status::timeout);
    }

    using detail::condition_variable_methods<condition_variable>::wait_until;

    native_handle_type native_handle() noexcept {
        return &this->_impl;
    }

private:
    CONDITION_VARIABLE _impl = CONDITION_VARIABLE_INIT;
};

#else

class condition_variable : public detail::condition_variable_methods<condition_variable> {
public:
    using native_handle_type = pthread_cond_t*;

    condition_variable() noexcept = default;

    condition_variable(const condition_variable&) = delete;
    condition_variable& operator=(const condition_variable&) = delete;

    ~condition_variable() {
        pthread_cond_destroy(&_impl);
    }

    void notify_one() noexcept {
        if (pthread_cond_signal(&_impl) != 0) {
            std::terminate();
        }
    }

    void notify_all() noexcept {
        if (pthread_cond_broadcast(&_impl) != 0) {
            std::terminate();
        }
    }

    void wait(stdx::unique_lock<stdx::mutex>& lock) noexcept {
        int err = pthread_cond_wait(&_impl, lock.mutex()->native_handle());
        if (err) {
            std::terminate();
        }
    }

    using detail::condition_variable_methods<condition_variable>::wait;

    template <class Clock, class Duration>
    stdx::cv_status wait_until(stdx::unique_lock<stdx::mutex>& lock,
                               const stdx::chrono::time_point<Clock, Duration>& timeout_time) {
        auto s = stdx::chrono::time_point_cast<stdx::chrono::seconds>(timeout_time);
        auto ns = stdx::chrono::duration_cast<stdx::chrono::nanoseconds>(timeout_time - s);

        struct timespec ts = {static_cast<std::time_t>(s.time_since_epoch().count()),
                              static_cast<long>(ns.count())};

        int err = pthread_cond_timedwait(&_impl, lock.mutex()->native_handle(), &ts);
        if (err) {
            if (err != ETIMEDOUT) {
                throw std::system_error(err, std::system_category(), "failed condvar timedwait");
            } else {
                return stdx::cv_status::timeout;
            }
        }

        return (Clock::now() < timeout_time ? stdx::cv_status::no_timeout
                                            : stdx::cv_status::timeout);
    }

    using detail::condition_variable_methods<condition_variable>::wait_until;

    native_handle_type native_handle() noexcept {
        return &this->_impl;
    }

private:
    pthread_cond_t _impl = PTHREAD_COND_INITIALIZER;
};

#endif

using condition_variable_any = ::std::condition_variable_any;  // NOLINT

}  // namespace stdx
}  // namespace mongo
