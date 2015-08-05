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

#include <deque>
#include <memory>
#include <set>

#include "mongo/executor/connection_pool.h"

namespace mongo {
namespace executor {
namespace connection_pool_test_details {

class ConnectionPoolTest;
class PoolImpl;

/**
 * Mock interface for the timer
 */
class TimerImpl : public ConnectionPoolTimerInterface {
public:
    TimerImpl(PoolImpl* global);
    ~TimerImpl() override;

    void setTimeout(timeoutCallback cb, stdx::chrono::milliseconds timeout) override;

    void cancelTimeout() override;

    // launches all timers for whom now() has passed
    static void fireIfNecessary();

    // dump all timers
    static void clear();

private:
    static std::set<TimerImpl*> _timers;

    timeoutCallback _cb;
    PoolImpl* _global;
    stdx::chrono::time_point<stdx::chrono::steady_clock> _expiration;
};

/**
 * Mock interface for the connections
 *
 * pushSetup() and pushRefresh() calls can be queued up ahead of time (in which
 * case callbacks immediately fire), or calls queue up and pushSetup() and
 * pushRefresh() fire as they're called.
 */
class ConnectionImpl : public ConnectionPoolConnectionInterface {
public:
    ConnectionImpl(const HostAndPort& hostAndPort, PoolImpl* global);

    void indicateUsed() override;

    const HostAndPort& getHostAndPort() const override;

    // Dump all connection callbacks
    static void clear();

    // Push either a callback that returns the status for a setup, or just the Status
    using pushSetupCallback = stdx::function<Status()>;
    static void pushSetup(pushSetupCallback status);
    static void pushSetup(Status status);

    // Push either a callback that returns the status for a refresh, or just the Status
    using pushRefreshCallback = stdx::function<Status()>;
    static void pushRefresh(pushRefreshCallback status);
    static void pushRefresh(Status status);

protected:
    stdx::chrono::time_point<stdx::chrono::steady_clock> getLastUsed() const override;

    void setTimeout(timeoutCallback cb, stdx::chrono::milliseconds timeout) override;

    void cancelTimeout() override;

    void setup(setupCallback cb, stdx::chrono::milliseconds timeout) override;

    void refresh(refreshCallback cb, stdx::chrono::milliseconds timeout) override;

private:
    HostAndPort _hostAndPort;
    stdx::chrono::time_point<stdx::chrono::steady_clock> _lastUsed;
    setupCallback _setupCallback;
    refreshCallback _refreshCallback;
    TimerImpl _timer;
    PoolImpl* _global;

    // Answer queues
    static std::deque<pushSetupCallback> _pushSetupQueue;
    static std::deque<pushRefreshCallback> _pushRefreshQueue;

    // Question queues
    static std::deque<ConnectionImpl*> _setupQueue;
    static std::deque<ConnectionImpl*> _refreshQueue;
};

/**
 * Mock for the pool implementation
 */
class PoolImpl : public ConnectionPoolImplInterface {
public:
    std::unique_ptr<ConnectionPoolConnectionInterface> makeConnection(
        const HostAndPort& hostAndPort) override;

    std::unique_ptr<ConnectionPoolTimerInterface> makeTimer() override;

    stdx::chrono::time_point<stdx::chrono::steady_clock> now() override;

    /**
     * setNow() can be used to fire all timers that have passed a point in time
     */
    static void setNow(stdx::chrono::time_point<stdx::chrono::steady_clock> now);

private:
    static boost::optional<stdx::chrono::time_point<stdx::chrono::steady_clock>> _now;
};

}  // namespace connection_pool_test_details
}  // namespace executor
}  // namespace mongo
