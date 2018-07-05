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
#include <list>

#include "mongo/stdx/mutex.h"
#include "mongo/util/notifyable.h"

namespace mongo {

class Waitable;

namespace stdx {

using condition_variable_any = ::std::condition_variable_any;  // NOLINT
using cv_status = ::std::cv_status;                            // NOLINT
using ::std::notify_all_at_thread_exit;                        // NOLINT

/**
 * We wrap std::condition_variable to allow us to register Notifyables which can "wait" on the
 * condvar without actually waiting on the std::condition_variable.  This allows us to possibly do
 * productive work in those types, rather than sleeping in the os.
 */
class condition_variable : private std::condition_variable {  // NOLINT
    friend class ::mongo::Waitable;

public:
    using std::condition_variable::condition_variable;  // NOLINT

    void notify_one() noexcept {
        stdx::lock_guard<stdx::mutex> lk(_mutex);

        auto iter = _notifyables.begin();
        if (iter != _notifyables.end()) {
            (*iter)->notify();
            return;
        }

        std::condition_variable::notify_one();  // NOLINT
    }

    void notify_all() noexcept {
        stdx::lock_guard<stdx::mutex> lk(_mutex);

        for (auto& notifyable : _notifyables) {
            notifyable->notify();
        }

        std::condition_variable::notify_all();  // NOLINT
    }

    using std::condition_variable::wait;           // NOLINT
    using std::condition_variable::wait_for;       // NOLINT
    using std::condition_variable::wait_until;     // NOLINT
    using std::condition_variable::native_handle;  // NOLINT

private:
    /**
     * Runs the callback with the Notifyable registered on the condvar.  This ensures that for the
     * duration of the callback execution, a notification on the condvar will trigger a notify() to
     * the Notifyable.
     *
     * The method is private, and accessed via friendship in Waitable.
     */
    template <typename Callback>
    void _runWithNotifyable(Notifyable& notifyable, Callback&& cb) {
        auto iter = [&] {
            stdx::lock_guard<stdx::mutex> localMutex(_mutex);
            return _notifyables.insert(_notifyables.end(), &notifyable);
        }();

        std::forward<Callback>(cb)();

        stdx::lock_guard<stdx::mutex> localMutex(_mutex);
        _notifyables.erase(iter);
    }

    stdx::mutex _mutex;
    std::list<Notifyable*> _notifyables;
};

}  // namespace stdx
}  // namespace mongo
