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

#include <thread>
#include <type_traits>

namespace mongo {
namespace stdx {

class thread : private ::std::thread {
public:
    using ::std::thread::native_handle_type;
    using ::std::thread::id;

    thread() noexcept : ::std::thread::thread() {}

    thread(thread&& other) noexcept
        : ::std::thread::thread(static_cast<::std::thread&&>(std::move(other))) {}

    /**
     * As of C++14, the Function overload for std::thread requires that this constructor only
     * participate in overload resolution if std::decay_t<Function> is not the same type as thread.
     * That prevents this overload from intercepting calls that might generate implicit conversions
     * before binding to other constructors (specifically move/copy constructors).
     */
    template <
        class Function,
        class... Args,
        typename std::enable_if<!std::is_same<thread, typename std::decay<Function>::type>::value,
                                int>::type = 0>
    explicit thread(Function&& f, Args&&... args) try:
        ::std::thread::thread(std::forward<Function>(f), std::forward<Args>(args)...) {}
    catch (...) {
        std::terminate();
    }

    thread(const thread&) = delete;

    thread& operator=(thread&& other) noexcept {
        return static_cast<thread&>(
            ::std::thread::operator=(static_cast<::std::thread&&>(std::move(other))));
    };

    using ::std::thread::joinable;
    using ::std::thread::get_id;
    using ::std::thread::native_handle;
    using ::std::thread::hardware_concurrency;

    using ::std::thread::join;
    using ::std::thread::detach;

    void swap(thread& other) noexcept {
        ::std::thread::swap(static_cast<::std::thread&>(other));
    }
};

namespace this_thread = ::std::this_thread;  // NOLINT

}  // namespace stdx
}  // namespace mongo

namespace std {
template <>
inline void swap<mongo::stdx::thread>(mongo::stdx::thread& lhs, mongo::stdx::thread& rhs) noexcept {
    lhs.swap(rhs);
}
}  // namespace std
