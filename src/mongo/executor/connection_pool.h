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

#pragma once

#include <deque>
#include <map>
#include <memory>
#include <set>

#include "mongo/stdx/chrono.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/functional.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/net/hostandport.h"

namespace mongo {
namespace executor {

class ConnectionPool;
class AbstractConnectionPoolConnection;
class ConnectionPoolTimerInterface;

using ConnectionHandle = std::unique_ptr<AbstractConnectionPoolConnection>;
using TimerHandle = std::unique_ptr<ConnectionPoolTimerInterface>;

class AbstractConnectionPoolConnection {
    friend class ConnectionPool;

public:
    virtual ~AbstractConnectionPoolConnection() = default;

    void indicateUsed();
    const HostAndPort& getHostAndPort() const;

protected:
    using setupCallback = stdx::function<void(AbstractConnectionPoolConnection*, Status)>;
    virtual void setup(setupCallback cb, stdx::chrono::milliseconds timeout) = 0;

    using refreshCallback = stdx::function<void(AbstractConnectionPoolConnection*, Status)>;
    virtual void refresh(refreshCallback cb, stdx::chrono::milliseconds timeout) = 0;

    HostAndPort _hostAndPort;
    TimerHandle _timer;

private:
    stdx::chrono::time_point<stdx::chrono::steady_clock> _lastUsed;
};

class ConnectionPoolTimerInterface {
public:
    virtual ~ConnectionPoolTimerInterface() = default;

    using timeoutCallback = stdx::function<void()>;
    virtual void setTimeout(timeoutCallback cb, stdx::chrono::milliseconds timeout) = 0;
    virtual void cancelTimeout() = 0;
};

class ConnectionPoolImplInterface {
public:
    virtual ~ConnectionPoolImplInterface() = default;

    virtual ConnectionHandle makeConnection(const HostAndPort& hostAndPort) = 0;
    virtual TimerHandle makeTimer() = 0;
};

class ConnectionPool {
public:
    static const stdx::chrono::milliseconds kRefreshTimeout;
    static const stdx::chrono::milliseconds kRefreshRequirement;

    struct Options {
        Options() {}

        size_t minConnections = 1;
        size_t maxConnections = 10;
        stdx::chrono::milliseconds refreshTimeout = kRefreshTimeout;
        stdx::chrono::milliseconds refreshRequirement = kRefreshRequirement;
    };
    ConnectionPool(std::unique_ptr<ConnectionPoolImplInterface> impl, Options options = Options{});

    using getConnectionCallback = stdx::function<void(StatusWith<ConnectionHandle>)>;
    void getConnection(const HostAndPort& hostAndPort,
                       stdx::chrono::milliseconds timeout,
                       getConnectionCallback cb);

    void returnConnection(ConnectionHandle conn);

private:
    class SpecificPool {
    public:
        SpecificPool(ConnectionPool* global);

        void getConnection(const HostAndPort& hostAndPort,
                           stdx::chrono::milliseconds timeout,
                           getConnectionCallback cb);
        void returnConnection(ConnectionHandle conn);

    private:
        void addToReady(stdx::unique_lock<stdx::mutex>& spLk, ConnectionHandle conn);
        void fulfillRequests(stdx::unique_lock<stdx::mutex>& spLk);
        void sortRequests();

        ConnectionPool* _global;
        stdx::mutex _mutex;
        std::map<AbstractConnectionPoolConnection*, ConnectionHandle> _readyPool;
        std::map<AbstractConnectionPoolConnection*, ConnectionHandle> _processingPool;
        std::vector<std::pair<stdx::chrono::time_point<stdx::chrono::steady_clock>,
                              getConnectionCallback>> _requests;
        TimerHandle _requestTimer;
        size_t _numberCheckedOut = 0;
    };

    stdx::mutex _mutex;
    std::unique_ptr<ConnectionPoolImplInterface> _impl;
    std::unordered_map<HostAndPort, std::unique_ptr<SpecificPool>> _pools;
    Options _options;
};

}  // namespace executor
}  // namespace mongo
