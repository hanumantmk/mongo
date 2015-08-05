/** *    Copyright (C) 2015 MongoDB Inc.
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

#include "mongo/platform/basic.h"

#include "mongo/executor/connection_pool_test_fixture.h"

#include "mongo/stdx/memory.h"

namespace mongo {
namespace executor {
namespace connection_pool_test_details {

TimerImpl::TimerImpl(PoolImpl* global) : _global(global) {}

TimerImpl::~TimerImpl() {
    cancelTimeout();
}

void TimerImpl::setTimeout(timeoutCallback cb, stdx::chrono::milliseconds timeout) {
    _cb = std::move(cb);
    _expiration = _global->now() + timeout;

    _timers.emplace(this);
}

void TimerImpl::cancelTimeout() {
    _timers.erase(this);
}

void TimerImpl::clear() {
    _timers.clear();
}

void TimerImpl::fireIfNecessary() {
    auto now = PoolImpl().now();

    std::vector<TimerImpl*> timers;

    std::copy(_timers.begin(), _timers.end(), std::back_inserter(timers));

    for (auto&& x : timers) {
        if (_timers.count(x) && (x->_expiration <= now)) {
            x->_cb();
        }
    }
}

std::set<TimerImpl*> TimerImpl::_timers;

ConnectionImpl::ConnectionImpl(const HostAndPort& hostAndPort, PoolImpl* global)
    : _hostAndPort(hostAndPort), _timer(global), _global(global) {}

void ConnectionImpl::indicateUsed() {
    _lastUsed = _global->now();
}

const HostAndPort& ConnectionImpl::getHostAndPort() const {
    return _hostAndPort;
}

void ConnectionImpl::clear() {
    _setupQueue.clear();
    _refreshQueue.clear();
    _pushSetupQueue.clear();
    _pushRefreshQueue.clear();
}

void ConnectionImpl::pushSetup(pushSetupCallback status) {
    _pushSetupQueue.push_back(status);

    if (_setupQueue.size()) {
        _setupQueue.front()->_setupCallback(_setupQueue.front(), _pushSetupQueue.front()());
        _setupQueue.pop_front();
        _pushSetupQueue.pop_front();
    }
}

void ConnectionImpl::pushSetup(Status status) {
    pushSetup([status]() { return status; });
}

void ConnectionImpl::pushRefresh(pushRefreshCallback status) {
    _pushRefreshQueue.push_back(status);

    if (_refreshQueue.size()) {
        _refreshQueue.front()->_refreshCallback(_refreshQueue.front(), _pushRefreshQueue.front()());
        _refreshQueue.pop_front();
        _pushRefreshQueue.pop_front();
    }
}

void ConnectionImpl::pushRefresh(Status status) {
    pushRefresh([status]() { return status; });
}

stdx::chrono::time_point<stdx::chrono::steady_clock> ConnectionImpl::getLastUsed() const {
    return _lastUsed;
}

void ConnectionImpl::setTimeout(timeoutCallback cb, stdx::chrono::milliseconds timeout) {
    _timer.setTimeout(cb, timeout);
}

void ConnectionImpl::cancelTimeout() {
    _timer.cancelTimeout();
}

void ConnectionImpl::setup(setupCallback cb, stdx::chrono::milliseconds timeout) {
    _setupCallback = std::move(cb);

    _timer.setTimeout([this] {
        _setupCallback(this, Status(ErrorCodes::ExceededTimeLimit, "timeout"));
    }, timeout);

    _setupQueue.push_back(this);

    if (_pushSetupQueue.size()) {
        _setupQueue.front()->_setupCallback(_setupQueue.front(), _pushSetupQueue.front()());
        _setupQueue.pop_front();
        _pushSetupQueue.pop_front();
    }
}

void ConnectionImpl::refresh(refreshCallback cb, stdx::chrono::milliseconds timeout) {
    _refreshCallback = std::move(cb);

    _timer.setTimeout([this] {
        _refreshCallback(this, Status(ErrorCodes::ExceededTimeLimit, "timeout"));
    }, timeout);

    _refreshQueue.push_back(this);

    if (_pushRefreshQueue.size()) {
        _refreshQueue.front()->_refreshCallback(_refreshQueue.front(), _pushRefreshQueue.front()());
        _refreshQueue.pop_front();
        _pushRefreshQueue.pop_front();
    }
}

std::deque<ConnectionImpl::pushSetupCallback> ConnectionImpl::_pushSetupQueue;
std::deque<ConnectionImpl::pushRefreshCallback> ConnectionImpl::_pushRefreshQueue;
std::deque<ConnectionImpl*> ConnectionImpl::_setupQueue;
std::deque<ConnectionImpl*> ConnectionImpl::_refreshQueue;

std::unique_ptr<ConnectionPoolConnectionInterface> PoolImpl::makeConnection(
    const HostAndPort& hostAndPort) {
    return stdx::make_unique<ConnectionImpl>(hostAndPort, this);
}

std::unique_ptr<ConnectionPoolTimerInterface> PoolImpl::makeTimer() {
    return stdx::make_unique<TimerImpl>(this);
}

stdx::chrono::time_point<stdx::chrono::steady_clock> PoolImpl::now() {
    if (_now) {
        return _now.get();
    }

    return stdx::chrono::steady_clock::now();
}

void PoolImpl::setNow(stdx::chrono::time_point<stdx::chrono::steady_clock> now) {
    _now = now;
    TimerImpl::fireIfNecessary();
}

boost::optional<stdx::chrono::time_point<stdx::chrono::steady_clock>> PoolImpl::_now;

}  // namespace connection_pool_test_details
}  // namespace executor
}  // namespace mongo
