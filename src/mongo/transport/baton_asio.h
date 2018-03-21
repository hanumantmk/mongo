/**
 *    Copyright (C) 2016 MongoDB Inc.
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

#include <list>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <poll.h>
#include <sys/eventfd.h>

#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/mutex.h"
#include "mongo/transport/baton.h"
#include "mongo/util/future.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace transport {

/**
 * This type contains data needed to associate Messages with connections
 * (on the transport side) and Messages with Client objects (on the database side).
 */
class BatonASIO : public Baton {
    struct PollGuard {
        explicit PollGuard(BatonASIO* parent) : _lk(parent->_mutex), _parent(parent) {
            _parent->_waiters++;
            _parent->breakOutOfPollIfNecessary(_lk);
        }

        PollGuard(const PollGuard&) = delete;
        PollGuard& operator=(const PollGuard&) = delete;

        PollGuard(PollGuard&&) = delete;
        PollGuard& operator=(PollGuard&&) = delete;

        ~PollGuard() {
            _parent->_waiters--;

            if (_parent->_pollerState == PollerState::PrePoll && !_parent->_waiters &&
                ((_parent->_promises.size() > 1) || _parent->_timers.size())) {
                _parent->_pollerCondvar.notify_one();
            }
        }

        stdx::unique_lock<stdx::mutex> _lk;
        BatonASIO* _parent;
    };

    enum class PollerState {
        UnRestricted,
        PrePoll,
        InPoll,
    };

public:
    enum class Type {
        In,
        Out,
    };

    using Handle = decltype(std::declval<pollfd>().fd);

    BatonASIO() {
        int efd = ::eventfd(0, EFD_NONBLOCK);
        _promises.push_back(SharedPromise<void>{});
        _pollSet.push_back({efd, POLLIN, 0});
    }

    Future<void> addToPoll(Handle fd, Type type) {
        PollGuard pg(this);

        _pollSet.push_back({fd, static_cast<short>(type == Type::In ? POLLIN : POLLOUT), 0});
        Promise<void> promise;
        auto out = promise.getFuture();
        _promises.push_back(promise.share());

        return out;
    }

    Future<void> addTimer(Date_t expiration) {
        PollGuard pg(this);

        Promise<void> promise;
        auto out = promise.getFuture();
        auto pair = _timers.insert({
            _nextTimerId++, expiration, promise.share(),
        });
        invariant(pair.second);
        _timersById[pair.first->id] = pair.first;

        return out;
    }

    void cancelPollItem(Handle fd) {
        PollGuard pg(this);

        for (size_t i = 1; i < _pollSet.size(); ++i) {
            if (_pollSet[i].fd == fd) {
                removePollItem(i);
                return;
            }
        }
    }

    void cancelTimer(size_t id) {
        PollGuard pg(this);

        auto iter = _timersById.find(id);

        if (iter != _timersById.end()) {
            _timers.erase(iter->second);
            _timersById.erase(iter);
        }
    }

    size_t depth() {
        stdx::lock_guard<stdx::mutex> lk(_mutex);

        return _promises.size();
    }

    void schedule(stdx::function<void()> func) override {
        addTimer(Date_t::fromMillisSinceEpoch(0)).getAsync([func](auto) { func(); });
    }

protected:
    void breakOutOfPollIfNecessary(stdx::unique_lock<stdx::mutex>& lk) {
        if (PollerState::InPoll == _pollerState) {
            if (_waiters == 1) {
                uint64_t u = 1;
                invariant(::write(_pollSet[0].fd, &u, sizeof(u)) == sizeof(u));
            }
            _waiterCondvar.wait(lk, [&] { return PollerState::InPoll != _pollerState; });
        }
    }

    void removePollItem(size_t idx) {
        invariant(idx != 0);
        if (_promises.size() > 2 && (idx != _promises.size() - 1)) {
            using std::swap;
            swap(_promises[idx], _promises[_promises.size() - 1]);
            swap(_pollSet[idx], _pollSet[_pollSet.size() - 1]);
        }
        _promises.pop_back();
        _pollSet.pop_back();
    }

public:
    void run() override {
        std::vector<SharedPromise<void>> toFulfill;

        auto guard = MakeGuard([&] {
            for (auto& promise : toFulfill) {
                promise.emplaceValue();
            }
        });

        {
            stdx::unique_lock<stdx::mutex> lk(_mutex);

            _pollerState = PollerState::PrePoll;
            _pollerCondvar.wait(
                lk, [&] { return !_waiters && (_promises.size() > 1 || _timers.size()); });

            boost::optional<Milliseconds> timeout;

            if (_timers.size()) {
                timeout = _timers.begin()->expiration - Date_t::now();
            }

            int rval = 0;
            if (!timeout || (timeout && *timeout > Milliseconds(0))) {
                _pollerState = PollerState::InPoll;
                lk.unlock();
                rval = ::poll(
                    _pollSet.data(), _pollSet.size(), timeout.value_or(Milliseconds(-1)).count());
                lk.lock();
                _pollerState = PollerState::UnRestricted;
                if (_waiters) {
                    _waiterCondvar.notify_all();
                }
            }

            auto now = Date_t::now();

            for (auto iter = _timers.begin(); iter != _timers.end();) {
                if (iter->expiration < now) {
                    toFulfill.push_back(std::move(iter->promise));
                    _timersById.erase(iter->id);
                    iter = _timers.erase(iter);
                } else {
                    break;
                }
            }

            if (rval < 0) {
                invariant(errno == EINTR);
            }

            if (rval > 0) {
                size_t remaining = rval;

                for (ssize_t n = _promises.size() - 1; n >= 0; n--) {
                    auto& pollSet = _pollSet[n];
                    if (pollSet.events | pollSet.revents) {
                        if (n == 0) {
                            uint64_t u;
                            size_t r = ::read(_pollSet[0].fd, &u, sizeof(u));
                            invariant(r == sizeof(u));

                            if (_waiters) {
                                _waiterCondvar.notify_all();
                            }
                        } else {
                            toFulfill.push_back(std::move(_promises[n]));
                            removePollItem(n);
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
    }

private:
    struct Timer {
        size_t id;
        Date_t expiration;
        SharedPromise<void> promise;

        struct LessThan {
            bool operator()(const Timer& lhs, const Timer& rhs) const {
                return std::tie(lhs.expiration, lhs.id) < std::tie(rhs.expiration, rhs.id);
            }
        };
    };

    stdx::mutex _mutex;
    stdx::condition_variable _waiterCondvar;
    stdx::condition_variable _pollerCondvar;

    PollerState _pollerState = PollerState::UnRestricted;
    size_t _waiters = 0;

    std::vector<pollfd> _pollSet;
    std::vector<SharedPromise<void>> _promises;

    std::set<Timer, Timer::LessThan> _timers;
    std::unordered_map<size_t, decltype(_timers)::const_iterator> _timersById;

    size_t _nextTimerId = 0;
};

}  // namespace transport
}  // namespace mongo
