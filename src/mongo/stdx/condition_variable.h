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
    cv_status wait_for(stdx::unique_lock<stdx::mutex>& lock,
                       const stdx::chrono::duration<Rep, Period>& rel_time) {
        using namespace chrono;

        using SystemClockTimePointFloat = time_point<system_clock, duration<long double, std::nano>>;
        using SystemClockTimePointInteger = time_point<system_clock, nanoseconds>;

        if (rel_time <= rel_time.zero())
            return cv_status::timeout;

        SystemClockTimePointFloat maxTimePoint = SystemClockTimePointInteger::max();
        steady_clock::time_point steadyClockBegin = steady_clock::now();
        system_clock::time_point systemClockBegin = system_clock::now();

        time_point<system_clock, nanoseconds> waitDuration = [&] {
            if (maxTimePoint - rel_time > systemClockBegin) {
                auto rel_time_ns = chrono::duration_cast<nanoseconds>(rel_time);

                if (rel_time_ns < rel_time) {
                    rel_time_ns++;
                }

                return systemClockBegin + rel_time_ns;
            } else {
                return SystemClockTimePointInteger::max();
            }
        }();

        nanoseconds nsSinceEpoch =
            std::min(waitDuration.time_since_epoch(), nanoseconds(0x59682F000000E941));

        timespec ts = [&]()-> timespec {
            seconds s = chrono::duration_cast<seconds>(nsSinceEpoch);
            using ts_sec = decltype(std::declval<timespec>().tv_sec);

            constexpr ts_sec ts_sec_max = std::numeric_limits<ts_sec>::max();
            if (s.count() < ts_sec_max) {
                return {static_cast<ts_sec>(s.count()),
                        static_cast<decltype(ts.tv_nsec)>((nsSinceEpoch - s).count())};
            } else {
                return {ts_sec_max, std::giga::num - 1};
            }
        }();
        int ec = static_cast<T*>(this)->_timedWait(lock.mutex()->native_handle(), &ts);
        if (ec != 0 && ec != ETIMEDOUT) {
            throw std::system_error(ec, std::system_category(), "failed condvar timedwait");
        }

        return steady_clock::now() - steadyClockBegin < rel_time ? cv_status::no_timeout
                                                               : cv_status::timeout;
    }

    template <class Rep, class Period, class Predicate>
    bool wait_for(stdx::unique_lock<stdx::mutex>& lock,
                  const stdx::chrono::duration<Rep, Period>& rel_time,
                  Predicate pred) {
        return static_cast<T*>(this)->wait_until(
            lock, stdx::chrono::system_clock::now() + rel_time, std::move(pred));
    }

    template <class Clock, class Duration>
    cv_status wait_until(stdx::unique_lock<stdx::mutex>& lock,
                         const stdx::chrono::time_point<Clock, Duration>& timeout_time) {
        static_cast<T*>(this)->wait_for(lock, timeout_time - Clock::now());
        return Clock::now() < timeout_time ? cv_status::no_timeout : cv_status::timeout;
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

class condition_variable : private detail::condition_variable_methods<condition_variable> {
    friend class detail::condition_variable_methods<condition_variable>;

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
    using detail::condition_variable_methods<condition_variable>::wait_for;
    using detail::condition_variable_methods<condition_variable>::wait_until;

    native_handle_type native_handle() noexcept {
        return &this->_impl;
    }

private:
    int _timedWait(mutex::native_handle_type mutex, timespec* ts) {
        using namespace chrono;

        auto duration = seconds(ts->tv_sec) + nanoseconds(ts->tv_nsec);
        auto abstime =
            system_clock::time_point(chrono::duration_cast<system_clock::duration>(duration));
        auto timeout_ms = chrono::duration_cast<milliseconds>(abstime - system_clock::now());

        if (!SleepConditionVariableSRW(
                &_impl, mutex, timeout_ms.count() > 0 ? timeout_ms.count() : 0, 0))
            return GetLastError();
        return 0;
    }

    CONDITION_VARIABLE _impl = CONDITION_VARIABLE_INIT;
};

#else

class condition_variable : private detail::condition_variable_methods<condition_variable> {
    friend class detail::condition_variable_methods<condition_variable>;

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
    using detail::condition_variable_methods<condition_variable>::wait_for;
    using detail::condition_variable_methods<condition_variable>::wait_until;

    native_handle_type native_handle() noexcept {
        return &this->_impl;
    }

private:
    int _timedWait(mutex::native_handle_type mutex, timespec* ts) {
        return pthread_cond_timedwait(&_impl, mutex, ts);
    }

    pthread_cond_t _impl = PTHREAD_COND_INITIALIZER;
};

#endif

using condition_variable_any = ::std::condition_variable_any;  // NOLINT

}  // namespace stdx
}  // namespace mongo
