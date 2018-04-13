/**
 * Copyright (C) 2018 MongoDB Inc.
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

#include <memory>
#include <set>
#include <vector>

#include <poll.h>
#include <sys/eventfd.h>

#include "mongo/base/checked_cast.h"
#include "mongo/db/operation_context.h"
#include "mongo/stdx/mutex.h"
#include "mongo/stdx/unordered_map.h"
#include "mongo/transport/baton.h"
#include "mongo/transport/session_asio.h"
#include "mongo/util/errno_util.h"
#include "mongo/util/future.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace transport {

/**
 * TransportLayerASIO Baton implementation for linux.
 *
 * We implement our networking reactor on top of poll + eventfd for wakeups
 */
class TransportLayerASIO::BatonASIO : public Baton {
public:
    BatonASIO(OperationContext* opCtx) : _opCtx(opCtx) {
        int efd = ::eventfd(0, EFD_NONBLOCK);
        _promises.push_back(SharedPromise<void>{});
        _pollSet.push_back({efd, POLLIN, 0});
    }

    ~BatonASIO() {
        invariant(_promises.size() == 1);
        invariant(_scheduled.empty());
        invariant(_timers.empty());

        invariant(close(_pollSet[0].fd) == 0);
    }

    void detach() override {
        {
            stdx::lock_guard<stdx::mutex> lk(_mutex);
            invariant(_promises.size() == 1);
            invariant(_scheduled.empty());
            invariant(_timers.empty());
        }

        stdx::lock_guard<Client> lk(*_opCtx->getClient());
        invariant(_opCtx->getBaton().get() == this);
        _opCtx->setBaton(nullptr);
    }

    Future<void> addSession(Session& session, Type type) override {
        auto fd =
            checked_cast<TransportLayerASIO::ASIOSession&>(session).getSocket().native_handle();

        Promise<void> promise;
        auto out = promise.getFuture();

        _safeExecute([ fd, type, sp = promise.share(), this ] {
            _pollSet.push_back({fd, static_cast<short>(type == Type::In ? POLLIN : POLLOUT), 0});
            _promises.push_back(sp);
            _idxById[fd] = _pollSet.size() - 1;
        });

        return out;
    }

    Future<void> waitFor(const ReactorTimer& timer, Milliseconds timeout) override {
        return waitUntil(timer, Date_t::now() + timeout);
    }

    Future<void> waitUntil(const ReactorTimer& timer, Date_t expiration) override {
        Promise<void> promise;
        auto out = promise.getFuture();

        _safeExecute([ timerPtr = &timer, expiration, sp = promise.share(), this ] {
            auto pair = _timers.insert({
                timerPtr, expiration, sp,
            });
            invariant(pair.second);
            _timersById[pair.first->id] = pair.first;
        });

        return out;
    }

    void cancelSession(Session& session) override {
        auto fd =
            checked_cast<TransportLayerASIO::ASIOSession&>(session).getSocket().native_handle();

        _safeExecute([fd, this] {
            auto iter = _idxById.find(fd);

            if (iter != _idxById.end()) {
                _removePollItem(iter->second);
                _idxById.erase(iter);
            }
        });
    }

    void cancelTimer(const ReactorTimer& timer) override {
        _safeExecute([ timerPtr = &timer, this ] {
            auto iter = _timersById.find(timerPtr);

            if (iter != _timersById.end()) {
                _timers.erase(iter->second);
                _timersById.erase(iter);
            }
        });
    }

    void schedule(stdx::function<void()> func) override {
        _safeExecute([ func = std::move(func), this ] { _scheduled.push_back(std::move(func)); });
    }

    bool run(boost::optional<Date_t> deadline) override {
        std::vector<SharedPromise<void>> toFulfill;

        // We'll fulfill promises and run jobs on the way out, ensuring we don't hold any locks
        const auto guard = MakeGuard([&] {
            for (auto& promise : toFulfill) {
                promise.emplaceValue();
            }

            stdx::unique_lock<stdx::mutex> lk(_mutex);
            while (_scheduled.size()) {
                decltype(_scheduled) toRun;
                {
                    using std::swap;
                    swap(_scheduled, toRun);
                }

                lk.unlock();
                for (auto& job : toRun) {
                    job();
                }
                lk.lock();
            }
        });

        bool eventfdFired = false;

        {
            stdx::unique_lock<stdx::mutex> lk(_mutex);

            auto now = Date_t::now();

            // If our deadline has passed, return that we've already failed
            if (deadline && *deadline <= now) {
                return false;
            }

            // If anything was scheduled, run it now.  No need to poll
            if (_scheduled.size()) {
                return true;
            }

            boost::optional<Milliseconds> timeout;

            // If we have a timer, poll no longer than that
            if (_timers.size()) {
                timeout = _timers.begin()->expiration - now;
            }

            // If we have a deadline
            if (deadline) {
                auto deadlineTimeout = *deadline - now;
                // If we didn't have a timer with a deadline, or our deadline is sooner than that
                // timer
                if (!timeout || (deadlineTimeout < *timeout)) {
                    // use the deadline timeout
                    timeout = deadlineTimeout;
                }
            }

            int rval = 0;
            // If we don't have a timeout, or we have a timeout that's unexpired, run poll.
            if (!timeout || (*timeout > Milliseconds(0))) {
                _inPoll = true;
                lk.unlock();
                rval = ::poll(
                    _pollSet.data(), _pollSet.size(), timeout.value_or(Milliseconds(-1)).count());

                // If poll failed, it better be in EINTR
                if (rval < 0) {
                    severe() << "error in poll: " << errnoWithDescription(errno);
                    fassertFailed(50788);
                }

                lk.lock();
                _inPoll = false;
            }

            now = Date_t::now();

            // If our deadline passed while in poll, we've failed
            if (deadline && now > *deadline) {
                return false;
            }

            // Fire expired timers
            for (auto iter = _timers.begin(); iter != _timers.end() && iter->expiration < now;) {
                toFulfill.push_back(std::move(iter->promise));
                _timersById.erase(iter->id);
                iter = _timers.erase(iter);
            }

            // If poll found some activity
            if (rval > 0) {
                size_t remaining = rval;

                // Walk from right to left, to simplify our swap strategy
                for (ssize_t n = _promises.size() - 1; n >= 0 && remaining; n--) {
                    auto& pollSet = _pollSet[n];
                    if (pollSet.revents) {
                        if (n == 0) {
                            // If we have activity on the eventfd, pull the count out
                            uint64_t u;
                            invariant(0 == ::eventfd_read(_pollSet[0].fd, &u));
                            eventfdFired = true;
                        } else {
                            // fulfill the promise
                            toFulfill.push_back(std::move(_promises[n]));
                            _removePollItem(n);
                        }

                        remaining--;
                    }
                }

                invariant(remaining == 0);
            }

            // If we got here, we should have done something
            invariant(toFulfill.size() || _scheduled.size() || eventfdFired);
        }

        return true;
    }

private:
    struct Timer {
        const ReactorTimer* id;
        Date_t expiration;
        SharedPromise<void> promise;

        struct LessThan {
            bool operator()(const Timer& lhs, const Timer& rhs) const {
                return std::tie(lhs.expiration, lhs.id) < std::tie(rhs.expiration, rhs.id);
            }
        };
    };

    /**
     * Safely executes method on the reactor.  If we're in poll, we schedule a task, then write to
     * the eventfd.  If not, we run inline.
     */
    template <typename Callback>
    void _safeExecute(Callback&& cb) {
        stdx::lock_guard<stdx::mutex> lk(_mutex);

        if (_inPoll) {
            _scheduled.push_back([cb, this] {
                stdx::lock_guard<stdx::mutex> lk(_mutex);
                cb();
            });

            invariant(0 == eventfd_write(_pollSet[0].fd, 1));
        } else {
            cb();
        }
    }

    /**
     * We manage the poll set by swapping to the end of the vector, then popping the last item
     */
    void _removePollItem(size_t idx) {
        // We should never remove the eventfd
        invariant(idx != 0);

        // We shouldn't index past the end of the vector
        invariant(idx < _promises.size());

        // If we have more than one fd and we're not removing the last item, swap whatever we're
        // removing with the last item
        if (_promises.size() > 2 && (idx != _promises.size() - 1)) {
            using std::swap;
            swap(_idxById[_pollSet[idx].fd], _idxById[_pollSet[_pollSet.size() - 1].fd]);
            swap(_promises[idx], _promises[_promises.size() - 1]);
            swap(_pollSet[idx], _pollSet[_pollSet.size() - 1]);
        }

        // Then pop it off
        _promises.pop_back();
        _pollSet.pop_back();
    }

    stdx::mutex _mutex;

    OperationContext* _opCtx;

    bool _inPoll = false;

    // These index the same request.  I.e. activation on _pollSet[n] should fulfill _promises[n]
    std::vector<pollfd> _pollSet;
    std::vector<SharedPromise<void>> _promises;
    stdx::unordered_map<int, size_t> _idxById;

    // The set is used to find the next timer which will fire.  The unordered_map looks up the
    // timers so we can remove them in O(1)
    std::set<Timer, Timer::LessThan> _timers;
    stdx::unordered_map<const ReactorTimer*, decltype(_timers)::const_iterator> _timersById;

    // For tasks that come in via schedule.  Or that were deferred because we were in poll
    std::vector<std::function<void()>> _scheduled;
};

}  // namespace transport
}  // namespace mongo
