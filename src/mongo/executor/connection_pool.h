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
class ConnectionPoolConnectionInterface;
class ConnectionPoolTimerInterface;

class ConnectionPoolConnectionInterface {
    friend class ConnectionPool;

public:
    virtual ~ConnectionPoolConnectionInterface() = default;

    virtual void indicateUsed() = 0;
    virtual const HostAndPort& getHostAndPort() const = 0;

protected:
    virtual stdx::chrono::time_point<stdx::chrono::steady_clock> getLastUsed() const = 0;

    using timeoutCallback = stdx::function<void()>;
    virtual void setTimeout(timeoutCallback cb, stdx::chrono::milliseconds timeout) = 0;
    virtual void cancelTimeout() = 0;

    using setupCallback = stdx::function<void(ConnectionPoolConnectionInterface*, Status)>;
    virtual void setup(setupCallback cb, stdx::chrono::milliseconds timeout) = 0;

    using refreshCallback = stdx::function<void(ConnectionPoolConnectionInterface*, Status)>;
    virtual void refresh(refreshCallback cb, stdx::chrono::milliseconds timeout) = 0;
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

    virtual std::unique_ptr<ConnectionPoolConnectionInterface> makeConnection(
        const HostAndPort& hostAndPort) = 0;
    virtual std::unique_ptr<ConnectionPoolTimerInterface> makeTimer() = 0;

    virtual stdx::chrono::time_point<stdx::chrono::steady_clock> now() = 0;
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

    using getConnectionCallback =
        stdx::function<void(StatusWith<ConnectionPoolConnectionInterface*>)>;
    void getConnection(const HostAndPort& hostAndPort,
                       stdx::chrono::milliseconds timeout,
                       getConnectionCallback cb);

    void returnConnection(ConnectionPoolConnectionInterface* connPtr);

private:
    class SpecificPool {
    public:
        SpecificPool(ConnectionPool* global);
        ~SpecificPool() {
            std::cerr << "Have " << _readyPool.size() << " connections at dtor\n";
        }

        void getConnection(const HostAndPort& hostAndPort,
                           stdx::chrono::milliseconds timeout,
                           getConnectionCallback cb);
        void returnConnection(ConnectionPoolConnectionInterface* connPtr);

    private:
        using ConnectionHandle = std::unique_ptr<ConnectionPoolConnectionInterface>;

        void addToReady(stdx::unique_lock<stdx::mutex>& spLk, ConnectionHandle conn);
        void fulfillRequests(stdx::unique_lock<stdx::mutex>& spLk);
        void sortRequests();
        void spawnConnections(stdx::unique_lock<stdx::mutex>& spLk, const HostAndPort& hostAndPort);
        ConnectionHandle takeFromPool(
            std::map<ConnectionPoolConnectionInterface*, ConnectionHandle>& pool,
            ConnectionPoolConnectionInterface* connPtr);

        ConnectionPool* _global;
        stdx::mutex _mutex;

        std::map<ConnectionPoolConnectionInterface*, ConnectionHandle> _readyPool;
        std::map<ConnectionPoolConnectionInterface*, ConnectionHandle> _processingPool;
        std::map<ConnectionPoolConnectionInterface*, ConnectionHandle> _checkedOutPool;
        std::vector<std::pair<stdx::chrono::time_point<stdx::chrono::steady_clock>,
                              getConnectionCallback>> _requests;
        std::unique_ptr<ConnectionPoolTimerInterface> _requestTimer;
    };

    stdx::mutex _mutex;
    std::unique_ptr<ConnectionPoolImplInterface> _impl;
    std::unordered_map<HostAndPort, std::unique_ptr<SpecificPool>> _pools;
    Options _options;
};

}  // namespace executor
}  // namespace mongo
