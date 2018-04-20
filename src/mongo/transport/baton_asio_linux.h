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
    struct EventFDHolder {
        EventFDHolder() : fd(::eventfd(0, 0)) {
            if (fd < 0) {
                severe() << "error in eventfd: " << errnoWithDescription(errno);
                fassertFailed(50797);
            }
        }

        ~EventFDHolder() {
            invariant(::close(fd) == 0);
        }

        int fd;
    };

public:
    BatonASIO(OperationContext* opCtx) : _opCtx(opCtx) {}

    ~BatonASIO() {
        invariant(_sessions.empty());
        invariant(_scheduled.empty());
        invariant(_timers.empty());
    }

    void detach() override {
        {
            stdx::lock_guard<stdx::mutex> lk(_mutex);
            invariant(_sessions.empty());
            invariant(_scheduled.empty());
            invariant(_timers.empty());
        }

        stdx::lock_guard<Client> lk(*_opCtx->getClient());
        invariant(_opCtx->getBaton().get() == this);
        _opCtx->setBaton(nullptr);
    }

    Future<void> addSession(Session& session, Type type) override {
        auto fd = checked_cast<ASIOSession&>(session).getSocket().native_handle();

        Promise<void> promise;
        auto out = promise.getFuture();

        _safeExecute([ fd, type, sp = promise.share(), this ] {
            _sessions[fd] = TransportSession{type, sp};
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

    bool cancelSession(Session& session) override {
        auto fd = checked_cast<ASIOSession&>(session).getSocket().native_handle();

        stdx::unique_lock<stdx::mutex> lk(_mutex);

        if (_sessions.find(fd) != _sessions.end()) {
            _safeExecute(
                [fd, this] {
                    auto iter = _sessions.find(fd);

                    if (iter != _sessions.end()) {
                        _sessions.erase(iter);
                    }
                },
                std::move(lk));

            return true;
        }

        return false;
    }

    bool cancelTimer(const ReactorTimer& timer) override {
        stdx::unique_lock<stdx::mutex> lk(_mutex);

        if (_timersById.find(&timer) != _timersById.end()) {
            _safeExecute([ timerPtr = &timer, this ] {
                auto iter = _timersById.find(timerPtr);

                if (iter != _timersById.end()) {
                    _timers.erase(iter->second);
                    _timersById.erase(iter);
                }
            },
                         std::move(lk));

            return true;
        }

        return false;
    }

    void schedule(stdx::function<void()> func) override {
        stdx::lock_guard<stdx::mutex> lk(_mutex);

        _scheduled.push_back(std::move(func));

        if (_inPoll) {
            invariant(eventfd_write(_efd.fd, 1) == 0);
        }
    }

    bool run(boost::optional<Date_t> deadline, OperationContext* opCtx) override {
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

        stdx::unique_lock<stdx::mutex> lk(_mutex);
        if (opCtx) {
            opCtx->checkForInterrupt();
        }

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

        if (deadline) {
            auto deadlineTimeout = *deadline - now;

            // If we didn't have a timer with a deadline, or our deadline is sooner than that
            // timer
            if (!timeout || (deadlineTimeout < *timeout)) {
                timeout = deadlineTimeout;
            }
        }

        std::vector<decltype(_sessions)::iterator> sessions;
        sessions.reserve(_sessions.size());
        std::vector<pollfd> pollSet;
        pollSet.reserve(_sessions.size() + 1);

        pollSet.push_back(pollfd{_efd.fd, POLLIN, 0});

        for (auto iter = _sessions.begin(); iter != _sessions.end(); ++iter) {
            pollSet.push_back(
                pollfd{iter->first,
                       static_cast<short>(iter->second.type == Type::In ? POLLIN : POLLOUT),
                       0});
            sessions.push_back(iter);
        }

        int rval = 0;
        // If we don't have a timeout, or we have a timeout that's unexpired, run poll.
        if (!timeout || (*timeout > Milliseconds(0))) {
            _inPoll = true;
            lk.unlock();
            rval =
                ::poll(pollSet.data(), pollSet.size(), timeout.value_or(Milliseconds(-1)).count());

            // If poll failed, it better be in EINTR
            if (rval < 0 && errno != EINTR) {
                severe() << "error in poll: " << errnoWithDescription(errno);
                fassertFailed(50798);
            }

            lk.lock();
            _inPoll = false;
            if (opCtx) {
                opCtx->checkForInterrupt();
            }
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

            auto pollIter = pollSet.begin();

            if (pollIter->revents) {
                // If we have activity on the eventfd, pull the count out
                uint64_t u;
                invariant(::eventfd_read(pollIter->fd, &u) == 0);
                eventfdFired = true;

                remaining--;
            }

            ++pollIter;
            for (auto sessionIter = sessions.begin(); sessionIter != sessions.end() && remaining;
                 ++sessionIter, ++pollIter) {
                if (pollIter->revents) {
                    toFulfill.push_back(std::move((*sessionIter)->second.promise));
                    _sessions.erase(*sessionIter);

                    remaining--;
                }
            }

            invariant(remaining == 0);
        }

        // If we got here, we should have done something
        invariant(toFulfill.size() || _scheduled.size() || eventfdFired);

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

    struct TransportSession {
        Type type;
        SharedPromise<void> promise;
    };

    template <typename Callback>
    void _safeExecute(Callback&& cb) {
        return _safeExecute(std::forward<Callback>(cb), stdx::unique_lock<stdx::mutex>(_mutex));
    }

    /**
     * Safely executes method on the reactor.  If we're in poll, we schedule a task, then write to
     * the eventfd.  If not, we run inline.
     */
    template <typename Callback>
    void _safeExecute(Callback&& cb, stdx::unique_lock<stdx::mutex> lk) {
        if (_inPoll) {
            _scheduled.push_back([cb, this] {
                stdx::lock_guard<stdx::mutex> lk(_mutex);
                cb();
            });

            invariant(eventfd_write(_efd.fd, 1) == 0);
        } else {
            cb();
        }
    }

    stdx::mutex _mutex;

    OperationContext* _opCtx;

    bool _inPoll = false;

    EventFDHolder _efd;

    // This map stores the sessions we need to poll on. We unwind it into a pollset for every
    // blocking call to run
    stdx::unordered_map<int, TransportSession> _sessions;

    // The set is used to find the next timer which will fire.  The unordered_map looks up the
    // timers so we can remove them in O(1)
    std::set<Timer, Timer::LessThan> _timers;
    stdx::unordered_map<const ReactorTimer*, decltype(_timers)::const_iterator> _timersById;

    // For tasks that come in via schedule.  Or that were deferred because we were in poll
    std::vector<std::function<void()>> _scheduled;
};

}  // namespace transport
}  // namespace mongo
