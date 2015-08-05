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
#include "mongo/stdx/functional.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/net/hostandport.h"

namespace mongo {
namespace executor {

class ConnectionPool;
class ConnectionPoolConnectionInterface;
class ConnectionPoolTimerInterface;

/**
 * Interface for connection pool connections
 *
 * Provides a minimal interface to manipulate connections within the pool,
 * specifically callbacks to set them up (connect + auth + whatever else),
 * refresh them (issue some kind of ping) and manage a timer.
 */
class ConnectionPoolConnectionInterface {
    friend class ConnectionPool;

public:
    virtual ~ConnectionPoolConnectionInterface() = default;

    /**
     * Intended to be called whenever a socket is used in a way which indicates
     * liveliness. I.e. if an operation is executed over the connection.
     */
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

/**
 * Interface for a basic timer
 *
 * Minimal interface sets a timer with a callback and cancels the timer.
 */
class ConnectionPoolTimerInterface {
public:
    virtual ~ConnectionPoolTimerInterface() = default;

    using timeoutCallback = stdx::function<void()>;
    virtual void setTimeout(timeoutCallback cb, stdx::chrono::milliseconds timeout) = 0;

    /**
     * It should be safe to cancel a previously canceled, or never set, timer.
     */
    virtual void cancelTimeout() = 0;
};

/**
 * Implementation interface for the connection pool
 *
 * This factory provides generators for connections, timers and a clock for the
 * connection pool.
 */
class ConnectionPoolImplInterface {
public:
    virtual ~ConnectionPoolImplInterface() = default;

    virtual std::unique_ptr<ConnectionPoolConnectionInterface> makeConnection(
        const HostAndPort& hostAndPort) = 0;
    virtual std::unique_ptr<ConnectionPoolTimerInterface> makeTimer() = 0;

    virtual stdx::chrono::time_point<stdx::chrono::steady_clock> now() = 0;
};

/**
 * The actual user visible connection pool.
 *
 * This pool is constructed with a ImplInterface which provides the tools it
 * needs to generate connections and manage them over time.
 *
 * The overall workflow here is to manage separate pools for each unique
 * HostAndPort. See comments on the various Options for how the pool operates.
 */
class ConnectionPool {
public:
    static const stdx::chrono::milliseconds kRefreshTimeout;
    static const stdx::chrono::milliseconds kRefreshRequirement;
    static const stdx::chrono::milliseconds kHostTimeout;

    struct Options {
        Options() {}

        // The minimum number of connections to keep alive while the pool is in
        // operation
        size_t minConnections = 1;

        // The maximum number of connections to spawn for a host. This includes
        // pending connections in setup and connections checked out of the pool
        // as well as the obvious live connections in the pool.
        size_t maxConnections = 10;

        // Amount of time to wait before timing out a refresh attempt
        stdx::chrono::milliseconds refreshTimeout = kRefreshTimeout;

        // Amount of time to hold a connection in the pool before attempting to refresh it
        stdx::chrono::milliseconds refreshRequirement = kRefreshRequirement;

        // Amount of time to keep a specific pool around without any checked
        // out connections or new requests
        stdx::chrono::milliseconds hostTimeout = kHostTimeout;
    };

    ConnectionPool(std::unique_ptr<ConnectionPoolImplInterface> impl, Options options = Options{});

    using getConnectionCallback =
        stdx::function<void(StatusWith<ConnectionPoolConnectionInterface*>)>;
    void getConnection(const HostAndPort& hostAndPort,
                       stdx::chrono::milliseconds timeout,
                       getConnectionCallback cb);

    void returnConnection(ConnectionPoolConnectionInterface* connPtr);

private:
    /**
     * A pool for a specific HostAndPort
     *
     * Pools come into existance the first time a connection is requested and
     * go out of existence after hostTimeout passes without any of their
     * connections being used.
     */
    class SpecificPool {
    public:
        SpecificPool(ConnectionPool* global, const HostAndPort& hostAndPort);
        ~SpecificPool();

        void getConnection(const HostAndPort& hostAndPort,
                           stdx::chrono::milliseconds timeout,
                           getConnectionCallback cb,
                           stdx::unique_lock<stdx::mutex> lk);
        void returnConnection(ConnectionPoolConnectionInterface* connPtr,
                              stdx::unique_lock<stdx::mutex> lk);

    private:
        using ConnectionHandle = std::unique_ptr<ConnectionPoolConnectionInterface>;

        void addToReady(stdx::unique_lock<stdx::mutex>& lk, ConnectionHandle conn);

        void fulfillRequests(stdx::unique_lock<stdx::mutex>& lk);

        void spawnConnections(stdx::unique_lock<stdx::mutex>& lk, const HostAndPort& hostAndPort);

        void shutdown();

        void sortRequests();

        ConnectionHandle takeFromPool(
            std::unordered_map<ConnectionPoolConnectionInterface*, ConnectionHandle>& pool,
            ConnectionPoolConnectionInterface* connPtr);

        void updateState();

        ConnectionPool* _global;

        HostAndPort _hostAndPort;

        std::unordered_map<ConnectionPoolConnectionInterface*, ConnectionHandle> _readyPool;
        std::unordered_map<ConnectionPoolConnectionInterface*, ConnectionHandle> _processingPool;
        std::unordered_map<ConnectionPoolConnectionInterface*, ConnectionHandle> _checkedOutPool;
        std::vector<std::pair<stdx::chrono::time_point<stdx::chrono::steady_clock>,
                              getConnectionCallback>> _requests;
        std::unique_ptr<ConnectionPoolTimerInterface> _requestTimer;
        stdx::chrono::time_point<stdx::chrono::steady_clock> _requestTimerExpiration;

        /**
         * The current state of the pool
         *
         * The pool begins in a running state. Moves to idle when no requests
         * are pending and no connections are checked out. It finally enters
         * shutdown after hostTimeout has passed (and waits there for current
         * refreshes to process out).
         *
         * At any point a new request sets the state back to running and
         * restarts all timers.
         */
        enum class State : uint8_t {
            // The pool is active
            running,

            // No current activity, waiting for hostTimeout to pass
            idle,

            // hostTimeout is passed, we're waiting for any processing
            // connections to finish before shutting down
            inShutdown,
        };

        State _state;
    };

    // The global mutex for everything
    stdx::mutex _mutex;
    std::unique_ptr<ConnectionPoolImplInterface> _impl;
    std::unordered_map<HostAndPort, std::unique_ptr<SpecificPool>> _pools;

    // options are set at startup and never changed at run time, so these are
    // accessed outside the lock
    Options _options;
};

}  // namespace executor
}  // namespace mongo
