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
#include <unordered_map>
#include <vector>

#include <poll.h>
#include <sys/eventfd.h>

#include "mongo/db/operation_context.h"
#include "mongo/stdx/mutex.h"
#include "mongo/transport/baton.h"
#include "mongo/transport/session_asio.h"
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
        stdx::lock_guard<Client> lk(*_opCtx->getClient());
        invariant(_opCtx->getBaton().get() == this);
        _opCtx->setBaton(nullptr);
    }

    Future<void> addSession(Session& session, Type type) override {
        auto fd =
            static_cast<TransportLayerASIO::ASIOSession&>(session).getSocket().native_handle();

        Promise<void> promise;
        auto out = promise.getFuture();

        _safeExecute([ fd, type, sp = promise.share(), this ] {
            _pollSet.push_back({fd, static_cast<short>(type == Type::In ? POLLIN : POLLOUT), 0});
            _promises.push_back(sp);
        });

        return out;
    }

    Future<void> waitFor(const ReactorTimer& timer, Milliseconds timeout) override {
        return waitUntil(timer, Date_t::now() + timeout);
    }

    Future<void> waitUntil(const ReactorTimer& timer, Date_t expiration) override {
        const ReactorTimer* timerPtr = &timer;

        Promise<void> promise;
        auto out = promise.getFuture();

        _safeExecute([ timerPtr, expiration, sp = promise.share(), this ] {
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
            static_cast<TransportLayerASIO::ASIOSession&>(session).getSocket().native_handle();

        _safeExecute([fd, this] {
            for (size_t i = 1; i < _pollSet.size(); ++i) {
                if (_pollSet[i].fd == fd) {
                    _removePollItem(i);
                    return;
                }
            }
        });
    }

    void cancelTimer(const ReactorTimer& timer) override {
        const ReactorTimer* timerPtr = &timer;

        _safeExecute([timerPtr, this] {
            auto iter = _timersById.find(timerPtr);

            if (iter != _timersById.end()) {
                _timers.erase(iter->second);
                _timersById.erase(iter);
            }
        });
    }

    void schedule(stdx::function<void()> func) override {
        _safeExecute([func, this] { _scheduled.push_back(std::move(func)); });
    }

    bool run(boost::optional<Date_t> deadline) override {
        std::vector<SharedPromise<void>> toFulfill;
        std::vector<stdx::function<void()>> toRun;

        // We'll fulfill promises and run jobs on the way out, ensuring we don't hold any locks
        const auto guard = MakeGuard([&] {
            for (auto& job : toRun) {
                job();
            }

            for (auto& promise : toFulfill) {
                promise.emplaceValue();
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
                using std::swap;
                swap(_scheduled, toRun);
                return true;
            }

            boost::optional<Milliseconds> timeout;

            // If we have timers, or the caller passed a deadline, we'll work that into our call to
            // poll
            if (_timers.size() || deadline) {
                timeout = std::min(_timers.size() ? _timers.begin()->expiration : Date_t::max(),
                                   deadline.value_or(Date_t::max())) -
                    now;
            }

            int rval = 0;
            // If we don't have a timeout, or we have a timeout but it's already expired, skip poll
            // and let the timer logic kick in
            if (!timeout || (timeout && *timeout > Milliseconds(0))) {
                _inPoll = true;
                lk.unlock();
                rval = ::poll(
                    _pollSet.data(), _pollSet.size(), timeout.value_or(Milliseconds(-1)).count());
                lk.lock();
                _inPoll = false;
            }

            // If poll failed, it better be in EINTR
            if (rval < 0) {
                invariant(errno == EINTR);
            }

            now = Date_t::now();

            // If our deadline passed while in poll, we've failed
            if (deadline && now > *deadline) {
                return false;
            }

            // Fire expired timers
            for (auto iter = _timers.begin(); iter != _timers.end();) {
                if (iter->expiration < now) {
                    toFulfill.push_back(std::move(iter->promise));
                    _timersById.erase(iter->id);
                    iter = _timers.erase(iter);
                } else {
                    break;
                }
            }

            // If poll found some activity
            if (rval > 0) {
                size_t remaining = rval;

                // Walk from right to left, to simplify our swap strategy
                for (ssize_t n = _promises.size() - 1; n >= 0; n--) {
                    auto& pollSet = _pollSet[n];
                    if (pollSet.events | pollSet.revents) {
                        if (n == 0) {
                            // If we have activity on the eventfd, pull the count out
                            uint64_t u;
                            size_t r = ::read(_pollSet[0].fd, &u, sizeof(u));
                            invariant(r == sizeof(u));
                            eventfdFired = true;
                        } else {
                            // fulfill the promise
                            toFulfill.push_back(std::move(_promises[n]));
                            _removePollItem(n);
                        }
                    }

                    remaining--;
                    if (remaining == 0) {
                        break;
                    }
                }

                invariant(remaining == 0);
            }
        }

        // If we got here, we should have done something
        invariant(toFulfill.size() || toRun.size() || eventfdFired);

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

            uint64_t u = 1;
            invariant(::write(_pollSet[0].fd, &u, sizeof(u)) == sizeof(u));
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

    // The set is used to find the next timer which will fire.  The unordered_map looks up the
    // timers so we can remove them in O(1)
    std::set<Timer, Timer::LessThan> _timers;
    std::unordered_map<const ReactorTimer*, decltype(_timers)::const_iterator> _timersById;

    // For tasks that come in via schedule.  Or that were deferred because we were in poll
    std::vector<std::function<void()>> _scheduled;
};

}  // namespace transport
}  // namespace mongo
