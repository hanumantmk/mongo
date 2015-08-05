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

#include "mongo/executor/connection_pool.h"

#include "mongo/stdx/memory.h"
#include "mongo/util/assert_util.h"

/**
 * One interesting implementation note herein concerns how setup() and
 * refresh() are invoked outside of the global lock, but setTimeout is not.
 * This implementation detail simplifies mocks, allowing them to return
 * synchronously sometimes, whereas having timeouts fire instantly adds little
 * value. In practice, dumping the locks is always safe (because we restrict
 * ourselves to operations over the connection).
 */

namespace mongo {
namespace executor {

stdx::chrono::milliseconds const ConnectionPool::kRefreshTimeout = stdx::chrono::seconds(30);
stdx::chrono::milliseconds const ConnectionPool::kRefreshRequirement = stdx::chrono::minutes(1);
stdx::chrono::milliseconds const ConnectionPool::kHostTimeout = stdx::chrono::minutes(5);

ConnectionPool::ConnectionPool(std::unique_ptr<ConnectionPoolImplInterface> impl, Options options)
    : _impl(std::move(impl)), _options(std::move(options)) {}

void ConnectionPool::getConnection(const HostAndPort& hostAndPort,
                                   stdx::chrono::milliseconds timeout,
                                   getConnectionCallback cb) {
    SpecificPool* pool;

    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto iter = _pools.find(hostAndPort);

    if (iter == _pools.end()) {
        auto handle = stdx::make_unique<SpecificPool>(this, hostAndPort);
        pool = handle.get();
        _pools[hostAndPort] = std::move(handle);
    } else {
        pool = iter->second.get();
    }

    pool->getConnection(hostAndPort, timeout, std::move(cb), std::move(lk));
}

void ConnectionPool::returnConnection(ConnectionPoolConnectionInterface* conn) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto iter = _pools.find(conn->getHostAndPort());

    invariant(iter != _pools.end());

    iter->second.get()->returnConnection(conn, std::move(lk));
}

ConnectionPool::SpecificPool::SpecificPool(ConnectionPool* global, const HostAndPort& hostAndPort)
    : _global(global),
      _hostAndPort(hostAndPort),
      _requestTimer(global->_impl->makeTimer()),
      _state(State::running) {}

ConnectionPool::SpecificPool::~SpecificPool() {
    _requestTimer->cancelTimeout();
}

void ConnectionPool::SpecificPool::getConnection(const HostAndPort& hostAndPort,
                                                 stdx::chrono::milliseconds timeout,
                                                 getConnectionCallback cb,
                                                 stdx::unique_lock<stdx::mutex> lk) {
    auto expiration = _global->_impl->now() + timeout;

    _requests.push_back(make_pair(expiration, std::move(cb)));

    sortRequests();

    updateState();

    spawnConnections(lk, hostAndPort);
    fulfillRequests(lk);
}

void ConnectionPool::SpecificPool::returnConnection(ConnectionPoolConnectionInterface* connPtr,
                                                    stdx::unique_lock<stdx::mutex> lk) {
    auto needsRefreshTP = connPtr->getLastUsed() + _global->_options.refreshRequirement;
    auto now = _global->_impl->now();

    auto conn = takeFromPool(_checkedOutPool, connPtr);

    // If we need to refresh this connection
    if (needsRefreshTP <= now) {
        if (_readyPool.size() + _processingPool.size() + _checkedOutPool.size() >
            _global->_options.minConnections) {
            // If we already have minConnections, just let the connection lapse
            return;
        }

        _processingPool[connPtr] = std::move(conn);

        lk.unlock();
        connPtr->refresh([this](ConnectionPoolConnectionInterface* connPtr, Status status) {
            connPtr->indicateUsed();

            stdx::unique_lock<stdx::mutex> lk(_global->_mutex);

            auto conn = takeFromPool(_processingPool, connPtr);

            // If we're in shutdown, we don't need refreshed connections
            if (_state == State::inShutdown)
                return;

            // If the connection refreshed successfully, throw it back in the ready pool
            if (status.isOK()) {
                addToReady(lk, std::move(conn));
            }
        }, _global->_options.refreshTimeout);
        lk.lock();
    } else {
        // If it's fine as it is, just put it in the ready queue
        addToReady(lk, std::move(conn));
    }

    updateState();
}

/**
 * Adds a live connection to the ready pool
 */
void ConnectionPool::SpecificPool::addToReady(stdx::unique_lock<stdx::mutex>& lk,
                                              ConnectionHandle conn) {
    auto connPtr = conn.get();

    _readyPool[connPtr] = std::move(conn);

    // Our strategy for refreshing connections is to check them out and
    // immediately check them back in (which kicks off the refresh logic in
    // returnConnection
    connPtr->setTimeout([this, connPtr]() {
        ConnectionHandle conn;

        stdx::unique_lock<stdx::mutex> lk(_global->_mutex);

        conn = takeFromPool(_readyPool, connPtr);

        // If we're in shutdown, we don't need to refresh connections
        if (_state == State::inShutdown)
            return;

        _checkedOutPool[connPtr] = std::move(conn);

        returnConnection(connPtr, std::move(lk));
    }, _global->_options.refreshRequirement);

    fulfillRequests(lk);
}

/**
 * fulfills as many outstanding requests as possible
 */
void ConnectionPool::SpecificPool::fulfillRequests(stdx::unique_lock<stdx::mutex>& lk) {
    while (_requests.size()) {
        auto iter = _readyPool.begin();

        if (iter == _readyPool.end())
            break;

        // Grab the connection and cancel it's timeout
        auto conn = std::move(iter->second);
        _readyPool.erase(iter);
        conn->cancelTimeout();

        // Grab the request and callback
        auto cb = std::move(_requests.back().second);
        _requests.pop_back();

        updateState();

        auto connPtr = conn.get();

        // check out the connection
        _checkedOutPool[connPtr] = std::move(conn);

        // pass it to the user
        lk.unlock();
        cb(connPtr);
        lk.lock();
    }

    updateState();
}

/**
 * spawn enough connections to satisfy open requests and minpool, while
 * honoring maxpool
 */
void ConnectionPool::SpecificPool::spawnConnections(stdx::unique_lock<stdx::mutex>& lk,
                                                    const HostAndPort& hostAndPort) {
    // We want minConnections <= outstanding requests <= maxConnections
    size_t target = std::max(_global->_options.minConnections,
                             std::min(_requests.size(), _global->_options.maxConnections));

    // While all of our inflight connections are less than our target
    while (_readyPool.size() + _processingPool.size() + _checkedOutPool.size() < target) {
        // make a new connection and put it in processing
        auto handle = _global->_impl->makeConnection(hostAndPort);
        auto connPtr = handle.get();
        _processingPool[connPtr] = std::move(handle);

        // Run the setup callback
        lk.unlock();
        connPtr->setup([this](ConnectionPoolConnectionInterface* connPtr, Status status) {
            connPtr->indicateUsed();

            stdx::unique_lock<stdx::mutex> lk(_global->_mutex);

            auto conn = takeFromPool(_processingPool, connPtr);

            if (status.isOK()) {
                addToReady(lk, std::move(conn));
            }
        }, _global->_options.refreshTimeout);
        // Note that this assumes that the refreshTimeout is sound for the
        // setupTimeout

        lk.lock();
    }
}

/**
 * Called every second after hostTimeout until all processing connections reap
 */
void ConnectionPool::SpecificPool::shutdown() {
    stdx::unique_lock<stdx::mutex> lk(_global->_mutex);

    _state = State::inShutdown;

    // If we have processing connections, wait for them to finish or timeout
    // before shutdown
    if (_processingPool.size()) {
        _requestTimer->setTimeout([this]() { shutdown(); }, stdx::chrono::seconds(1));

        return;
    }

    _global->_pools.erase(_hostAndPort);
}

void ConnectionPool::SpecificPool::sortRequests() {
    using pair = decltype(_requests)::const_reference;

    std::sort(_requests.begin(), _requests.end(), [](pair a, pair b) { return a.first > b.first; });
}

ConnectionPool::SpecificPool::ConnectionHandle ConnectionPool::SpecificPool::takeFromPool(
    std::unordered_map<ConnectionPoolConnectionInterface*, ConnectionHandle>& pool,
    ConnectionPoolConnectionInterface* connPtr) {
    auto iter = pool.find(connPtr);
    invariant(iter != pool.end());

    auto conn = std::move(iter->second);
    pool.erase(iter);
    return conn;
}

/**
 * Updates our state and manages the request timer
 */
void ConnectionPool::SpecificPool::updateState() {
    if (_requests.size()) {
        // We have some outstanding requests, we're live

        // If we were already running and the timer is the same as it was
        // before, nothing to do
        if (_state == State::running && _requestTimerExpiration == _requests.back().first)
            return;

        _state = State::running;

        _requestTimer->cancelTimeout();

        _requestTimerExpiration = _requests.back().first;

        auto timeout = stdx::chrono::duration_cast<stdx::chrono::milliseconds>(
            _requests.back().first - _global->_impl->now());

        // We set a timer for the most recent request, then invoke each timed
        // out request we couldn't service
        _requestTimer->setTimeout([this]() {
            stdx::unique_lock<stdx::mutex> lk(_global->_mutex);

            auto now = _global->_impl->now();

            while (_requests.size()) {
                auto& x = _requests.back();

                if (x.first <= now) {
                    auto cb = std::move(x.second);
                    _requests.pop_back();

                    lk.unlock();
                    cb(Status(ErrorCodes::ExceededTimeLimit,
                              "Couldn't get a connection within the time limit"));
                    lk.lock();
                } else {
                    break;
                }
            }

            updateState();
        }, timeout);
    } else if (_checkedOutPool.size()) {
        // If we have no requests, but someone's using a connection, we just
        // hang around until the next request or a return

        _requestTimer->cancelTimeout();
        _state = State::running;
        _requestTimerExpiration = _requestTimerExpiration.max();
    } else {
        // If we don't have any live requests and no one has checked out connections

        // If we used to be idle, just bail
        if (_state == State::idle)
            return;

        _state = State::idle;

        _requestTimer->cancelTimeout();

        _requestTimerExpiration = _global->_impl->now() + _global->_options.hostTimeout;

        auto timeout = _global->_options.hostTimeout;

        // Set the shutdown timer
        _requestTimer->setTimeout([this]() { shutdown(); }, timeout);
    }
}

}  // namespace executor
}  // namespace mongo
