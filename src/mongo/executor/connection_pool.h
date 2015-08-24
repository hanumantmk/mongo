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

#include <memory>
#include <unordered_map>

#include "mongo/stdx/chrono.h"
#include "mongo/stdx/functional.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/net/hostandport.h"

namespace mongo {
namespace executor {

/**
 * The actual user visible connection pool.
 *
 * This pool is constructed with a DependentTypeFactoryInterface which provides the tools it
 * needs to generate connections and manage them over time.
 *
 * The overall workflow here is to manage separate pools for each unique
 * HostAndPort. See comments on the various Options for how the pool operates.
 */
class ConnectionPool {
    class ConnectionHandleDeleter;
    class SpecificPool;

public:
    class ConnectionInterface;
    class DependentTypeFactoryInterface;
    class TimerInterface;

    using ConnectionHandle = std::unique_ptr<ConnectionInterface, ConnectionHandleDeleter>;

    using GetConnectionCallback = stdx::function<void(StatusWith<ConnectionHandle>)>;

    static const Milliseconds kDefaultRefreshTimeout;
    static const Milliseconds kDefaultRefreshRequirement;
    static const Milliseconds kDefaultHostTimeout;

    struct Options {
        Options() {}

        /**
         * The minimum number of connections to keep alive while the pool is in
         * operation
         */
        size_t minConnections = 1;

        /**
         * The maximum number of connections to spawn for a host. This includes
         * pending connections in setup and connections checked out of the pool
         * as well as the obvious live connections in the pool.
         */
        size_t maxConnections = std::numeric_limits<size_t>::max();

        /**
         * Amount of time to wait before timing out a refresh attempt
         */
        Milliseconds refreshTimeout = kDefaultRefreshTimeout;

        /**
         * Amount of time a connection may be idle before it cannot be returned
         * for a user request and must instead be checked out and refreshed
         * before handing to a user.
         */
        Milliseconds refreshRequirement = kDefaultRefreshRequirement;

        /**
         * Amount of time to keep a specific pool around without any checked
         * out connections or new requests
         */
        Milliseconds hostTimeout = kDefaultHostTimeout;
    };

    explicit ConnectionPool(std::unique_ptr<DependentTypeFactoryInterface> impl,
                            Options options = Options{});

    void get(const HostAndPort& hostAndPort, Milliseconds timeout, GetConnectionCallback cb);

    /**
     * TODO add a function returning connection pool stats
     */

private:
    void returnConnection(ConnectionInterface* connection);

    // Options are set at startup and never changed at run time, so these are
    // accessed outside the lock
    const Options _options;

    const std::unique_ptr<DependentTypeFactoryInterface> _factory;

    // The global mutex for specific pool access
    stdx::mutex _mutex;
    std::unordered_map<HostAndPort, std::unique_ptr<SpecificPool>> _pools;
};

class ConnectionPool::ConnectionHandleDeleter {
public:
    ConnectionHandleDeleter() {}
    ConnectionHandleDeleter(ConnectionPool* pool) : _pool(pool) {}

    void operator()(ConnectionInterface* connection) {
        if (_pool && connection)
            _pool->returnConnection(connection);
    }

private:
    ConnectionPool* _pool = nullptr;
};

/**
 * Interface for a basic timer
 *
 * Minimal interface sets a timer with a callback and cancels the timer.
 */
class ConnectionPool::TimerInterface {
public:
    using TimeoutCallback = stdx::function<void()>;

    virtual ~TimerInterface() = default;

    /**
     * Sets the timeout for the timer. Setting an already set timer should
     * override the previous timer.
     */
    virtual void setTimeout(Milliseconds timeout, TimeoutCallback cb) = 0;

    /**
     * It should be safe to cancel a previously canceled, or never set, timer.
     */
    virtual void cancelTimeout() = 0;
};

/**
 * Interface for connection pool connections
 *
 * Provides a minimal interface to manipulate connections within the pool,
 * specifically callbacks to set them up (connect + auth + whatever else),
 * refresh them (issue some kind of ping) and manage a timer.
 */
class ConnectionPool::ConnectionInterface : public TimerInterface {
    friend class ConnectionPool;

public:
    virtual ~ConnectionInterface() = default;

    /**
     * Intended to be called whenever a socket is used in a way which indicates
     * liveliness. I.e. if an operation is executed over the connection.
     */
    virtual void indicateUsed() = 0;

    virtual void indicateFailed() = 0;

    /**
     * The HostAndPort for the connection. This should be the same as the
     * HostAndPort passed to DependentTypeFactoryInterface::makeConnection.
     */
    virtual const HostAndPort& getHostAndPort() const = 0;

protected:
    /**
     * Making these protected makes the definitions available to override in
     * children.
     */
    using SetupCallback = stdx::function<void(ConnectionInterface*, Status)>;
    using RefreshCallback = stdx::function<void(ConnectionInterface*, Status)>;

private:
    /**
     * Returns the last used time point for the connection
     */
    virtual Date_t getLastUsed() const = 0;

    /**
     * Returns true if the connection is failed. This implies that it should
     * not be returned to the pool.
     */
    virtual bool isFailed() const = 0;

    /**
     * Sets up the connection. This should include connection + auth + any
     * other associated hooks.
     */
    virtual void setup(Milliseconds timeout, SetupCallback cb) = 0;

    /**
     * Refreshes the connection. This should involve a network round trip and
     * should strongly imply an active connection
     */
    virtual void refresh(Milliseconds timeout, RefreshCallback cb) = 0;
};

/**
 * Implementation interface for the connection pool
 *
 * This factory provides generators for connections, timers and a clock for the
 * connection pool.
 */
class ConnectionPool::DependentTypeFactoryInterface {
public:
    virtual ~DependentTypeFactoryInterface() = default;

    /**
     * Makes a new connection given a host and port
     */
    virtual std::unique_ptr<ConnectionInterface> makeConnection(const HostAndPort& hostAndPort) = 0;

    /**
     * Makes a new timer
     */
    virtual std::unique_ptr<TimerInterface> makeTimer() = 0;

    /**
     * Returns the current time point
     */
    virtual Date_t now() = 0;
};

/**
 * A pool for a specific HostAndPort
 *
 * Pools come into existance the first time a connection is requested and
 * go out of existence after hostTimeout passes without any of their
 * connections being used.
 */
class ConnectionPool::SpecificPool {
public:
    SpecificPool(ConnectionPool* parent, const HostAndPort& hostAndPort);
    ~SpecificPool();

    /**
     * Get's a connection from the specific pool. Sinks a unique_lock from the
     * parent to preserve the lock on _mutex
     */
    void getConnection(const HostAndPort& hostAndPort,
                       Milliseconds timeout,
                       stdx::unique_lock<stdx::mutex> lk,
                       GetConnectionCallback cb);

    /**
     * Returns a connection to a specific pool. Sinks a unique_lock from the
     * parent to preserve the lock on _mutex
     */
    void returnConnection(ConnectionInterface* connection, stdx::unique_lock<stdx::mutex> lk);

private:
    using OwnedConnection = std::unique_ptr<ConnectionInterface>;
    using OwnershipPool = std::unordered_map<ConnectionInterface*, OwnedConnection>;

    void addToReady(stdx::unique_lock<stdx::mutex>& lk, OwnedConnection conn);

    void fulfillRequests(stdx::unique_lock<stdx::mutex>& lk);

    void spawnConnections(stdx::unique_lock<stdx::mutex>& lk, const HostAndPort& hostAndPort);

    void shutdown();

    void sortRequests();

    OwnedConnection takeFromPool(OwnershipPool& pool, ConnectionInterface* connection);

    void updateState();

private:
    ConnectionPool* const _parent;

    const HostAndPort _hostAndPort;

    OwnershipPool _readyPool;
    OwnershipPool _processingPool;
    OwnershipPool _checkedOutPool;
    std::vector<std::pair<Date_t, GetConnectionCallback>> _requests;
    std::unique_ptr<TimerInterface> _requestTimer;
    Date_t _requestTimerExpiration;

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
    enum class State {
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

}  // namespace executor
}  // namespace mongo
