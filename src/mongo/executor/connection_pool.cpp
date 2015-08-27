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

Milliseconds const ConnectionPool::kDefaultRefreshTimeout = Seconds(30);
Milliseconds const ConnectionPool::kDefaultRefreshRequirement = Minutes(1);
Milliseconds const ConnectionPool::kDefaultHostTimeout = Minutes(5);

ConnectionPool::ConnectionPool(std::unique_ptr<DependentTypeFactoryInterface> impl, Options options)
    : _options(std::move(options)), _factory(std::move(impl)) {}

void ConnectionPool::dropConnections(const HostAndPort& hostAndPort) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto iter = _pools.find(hostAndPort);

    if (iter == _pools.end())
        return;

    iter->second.get()->dropConnections(std::move(lk));
}

void ConnectionPool::get(const HostAndPort& hostAndPort,
                         Milliseconds timeout,
                         GetConnectionCallback cb) {
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

    invariant(pool);

    pool->getConnection(hostAndPort, timeout, std::move(lk), std::move(cb));
}

void ConnectionPool::returnConnection(ConnectionInterface* conn) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto iter = _pools.find(conn->getHostAndPort());

    invariant(iter != _pools.end());

    iter->second.get()->returnConnection(conn, std::move(lk));
}

ConnectionPool::SpecificPool::SpecificPool(ConnectionPool* parent, const HostAndPort& hostAndPort)
    : _parent(parent),
      _hostAndPort(hostAndPort),
      _requestTimer(parent->_factory->makeTimer()),
      _generation(0),
      _inFulfillRequests(false),
      _state(State::kRunning) {}

ConnectionPool::SpecificPool::~SpecificPool() {
    DESTRUCTOR_GUARD(_requestTimer->cancelTimeout();)
}

void ConnectionPool::SpecificPool::dropConnections(stdx::unique_lock<stdx::mutex> lk) {
    _generation++;

    _readyPool.clear();

    for (auto&& x : _processingPool) {
        _droppedProcessingPool[x.first] = std::move(x.second);
    }

    _processingPool.clear();

    failAllRequests(Status(ErrorCodes::PooledConnectionsDropped, "Pooled connections dropped"),
                    std::move(lk));
}

void ConnectionPool::SpecificPool::getConnection(const HostAndPort& hostAndPort,
                                                 Milliseconds timeout,
                                                 stdx::unique_lock<stdx::mutex> lk,
                                                 GetConnectionCallback cb) {
    auto expiration = _parent->_factory->now() + timeout;

    _requests.push(make_pair(expiration, std::move(cb)));

    updateStateInLock();

    spawnConnections(lk, hostAndPort);
    fulfillRequests(lk);
}

void ConnectionPool::SpecificPool::returnConnection(ConnectionInterface* connPtr,
                                                    stdx::unique_lock<stdx::mutex> lk) {
    auto needsRefreshTP = connPtr->getLastUsed() + _parent->_options.refreshRequirement;
    auto now = _parent->_factory->now();

    auto conn = takeFromPool(_checkedOutPool, connPtr);

    if (conn->isFailed() || conn->getGeneration() != _generation) {
        // If the connection failed or has been dropped, simply let it lapse
        //
        // TODO: alert via some callback if the host is bad
    } else if (needsRefreshTP <= now) {
        // If we need to refresh this connection

        if (_readyPool.size() + _processingPool.size() + _checkedOutPool.size() >=
            _parent->_options.minConnections) {
            // If we already have minConnections, just let the connection lapse
            return;
        }

        _processingPool[connPtr] = std::move(conn);

        // Unlock in case refresh can occur immediately
        lk.unlock();
        connPtr->refresh(_parent->_options.refreshTimeout,
                         [this](ConnectionInterface* connPtr, Status status) {
                             connPtr->indicateUsed();

                             stdx::unique_lock<stdx::mutex> lk(_parent->_mutex);

                             auto conn = takeFromProcessingPool(connPtr);

                             // If the host and port were dropped, let this lapse
                             if (conn->getGeneration() != _generation)
                                 return;

                             // If we're in shutdown, we don't need refreshed connections
                             if (_state == State::kInShutdown)
                                 return;

                             // If the connection refreshed successfully, throw it back in the ready
                             // pool
                             if (status.isOK()) {
                                 addToReady(lk, std::move(conn));
                                 return;
                             }

                             // Otherwise pass the failure on through
                             failAllRequests(status, std::move(lk));
                         });
        lk.lock();
    } else {
        // If it's fine as it is, just put it in the ready queue
        addToReady(lk, std::move(conn));
    }

    updateStateInLock();
}

/**
 * Adds a live connection to the ready pool
 */
void ConnectionPool::SpecificPool::addToReady(stdx::unique_lock<stdx::mutex>& lk,
                                              OwnedConnection conn) {
    auto connPtr = conn.get();

    _readyPool[connPtr] = std::move(conn);

    // Our strategy for refreshing connections is to check them out and
    // immediately check them back in (which kicks off the refresh logic in
    // returnConnection
    connPtr->setTimeout(_parent->_options.refreshRequirement,
                        [this, connPtr]() {
                            OwnedConnection conn;

                            stdx::unique_lock<stdx::mutex> lk(_parent->_mutex);

                            conn = takeFromPool(_readyPool, connPtr);

                            // If we're in shutdown, we don't need to refresh connections
                            if (_state == State::kInShutdown)
                                return;

                            _checkedOutPool[connPtr] = std::move(conn);

                            returnConnection(connPtr, std::move(lk));
                        });

    fulfillRequests(lk);
}

void ConnectionPool::SpecificPool::failAllRequests(const Status& status,
                                                   stdx::unique_lock<stdx::mutex> lk) {
    // Move the requests out so they aren't visible
    // in other threads
    decltype(_requests) requestsToFail;

    {
        using std::swap;
        swap(requestsToFail, _requests);
    }

    // Update state to reflect the lack of requests
    updateStateInLock();

    // Drop the lock and process all of the requests
    // with the same failed status
    lk.unlock();

    while (requestsToFail.size()) {
        requestsToFail.top().second(status, nullptr);
        requestsToFail.pop();
    }
}

/**
 * fulfills as many outstanding requests as possible
 */
void ConnectionPool::SpecificPool::fulfillRequests(stdx::unique_lock<stdx::mutex>& lk) {
    // If some other thread (possibly this thread) is fulfilling requests,
    // don't keep padding the callstack.
    if (_inFulfillRequests)
        return;

    _inFulfillRequests = true;

    while (_requests.size()) {
        auto iter = _readyPool.begin();

        if (iter == _readyPool.end())
            break;

        // Grab the connection and cancel its timeout
        auto conn = std::move(iter->second);
        _readyPool.erase(iter);
        conn->cancelTimeout();

        // Grab the request and callback
        auto cb = std::move(_requests.top().second);
        _requests.pop();

        updateStateInLock();

        auto connPtr = conn.get();

        // check out the connection
        _checkedOutPool[connPtr] = std::move(conn);

        // pass it to the user
        lk.unlock();
        cb(Status::OK(), ConnectionHandle(connPtr, ConnectionHandleDeleter(_parent)));
        lk.lock();
    }

    _inFulfillRequests = false;
}

/**
 * spawn enough connections to satisfy open requests and minpool, while
 * honoring maxpool
 */
void ConnectionPool::SpecificPool::spawnConnections(stdx::unique_lock<stdx::mutex>& lk,
                                                    const HostAndPort& hostAndPort) {
    // We want minConnections <= outstanding requests <= maxConnections
    auto target = [&] {
        return std::max(
            _parent->_options.minConnections,
            std::min(_requests.size() + _checkedOutPool.size(), _parent->_options.maxConnections));
    };

    // While all of our inflight connections are less than our target
    while (_readyPool.size() + _processingPool.size() + _checkedOutPool.size() < target()) {
        // make a new connection and put it in processing
        auto handle = _parent->_factory->makeConnection(hostAndPort, _generation);
        auto connPtr = handle.get();
        _processingPool[connPtr] = std::move(handle);

        // Run the setup callback
        lk.unlock();
        connPtr->setup(_parent->_options.refreshTimeout,
                       [this](ConnectionInterface* connPtr, Status status) {
                           connPtr->indicateUsed();

                           stdx::unique_lock<stdx::mutex> lk(_parent->_mutex);

                           auto conn = takeFromProcessingPool(connPtr);

                           if (conn->getGeneration() != _generation) {
                               // If the host and port was dropped, let the
                               // connection lapse
                           } else if (status.isOK()) {
                               addToReady(lk, std::move(conn));
                           } else if (_requests.size()) {
                               // If the setup request failed, immediately fail
                               // all other pending requests.

                               failAllRequests(status, std::move(lk));
                           }
                       });
        // Note that this assumes that the refreshTimeout is sound for the
        // setupTimeout

        lk.lock();
    }
}

/**
 * Called every second after hostTimeout until all processing connections reap
 */
void ConnectionPool::SpecificPool::shutdown() {
    stdx::unique_lock<stdx::mutex> lk(_parent->_mutex);

    _state = State::kInShutdown;

    // If we have processing connections, wait for them to finish or timeout
    // before shutdown
    if (_processingPool.size() || _droppedProcessingPool.size()) {
        _requestTimer->setTimeout(Seconds(1), [this]() { shutdown(); });

        return;
    }

    invariant(_requests.empty());
    invariant(_checkedOutPool.empty());

    _parent->_pools.erase(_hostAndPort);
}

ConnectionPool::SpecificPool::OwnedConnection ConnectionPool::SpecificPool::takeFromPool(
    OwnershipPool& pool, ConnectionInterface* connPtr) {
    auto iter = pool.find(connPtr);
    invariant(iter != pool.end());

    auto conn = std::move(iter->second);
    pool.erase(iter);
    return conn;
}

ConnectionPool::SpecificPool::OwnedConnection ConnectionPool::SpecificPool::takeFromProcessingPool(
    ConnectionInterface* connPtr) {
    if (_processingPool.count(connPtr))
        return takeFromPool(_processingPool, connPtr);

    return takeFromPool(_droppedProcessingPool, connPtr);
}

/**
 * Updates our state and manages the request timer
 */
void ConnectionPool::SpecificPool::updateStateInLock() {
    if (_requests.size()) {
        // We have some outstanding requests, we're live

        // If we were already running and the timer is the same as it was
        // before, nothing to do
        if (_state == State::kRunning && _requestTimerExpiration == _requests.top().first)
            return;

        _state = State::kRunning;

        _requestTimer->cancelTimeout();

        _requestTimerExpiration = _requests.top().first;

        auto timeout = _requests.top().first - _parent->_factory->now();

        // We set a timer for the most recent request, then invoke each timed
        // out request we couldn't service
        _requestTimer->setTimeout(
            timeout,
            [this]() {
                stdx::unique_lock<stdx::mutex> lk(_parent->_mutex);

                auto now = _parent->_factory->now();

                while (_requests.size()) {
                    auto& x = _requests.top();

                    if (x.first <= now) {
                        auto cb = std::move(x.second);
                        _requests.pop();

                        lk.unlock();
                        cb(Status(ErrorCodes::ExceededTimeLimit,
                                  "Couldn't get a connection within the time limit"),
                           nullptr);
                        lk.lock();
                    } else {
                        break;
                    }
                }

                updateStateInLock();
            });
    } else if (_checkedOutPool.size()) {
        // If we have no requests, but someone's using a connection, we just
        // hang around until the next request or a return

        _requestTimer->cancelTimeout();
        _state = State::kRunning;
        _requestTimerExpiration = _requestTimerExpiration.max();
    } else {
        // If we don't have any live requests and no one has checked out connections

        // If we used to be idle, just bail
        if (_state == State::kIdle)
            return;

        _state = State::kIdle;

        _requestTimer->cancelTimeout();

        _requestTimerExpiration = _parent->_factory->now() + _parent->_options.hostTimeout;

        auto timeout = _parent->_options.hostTimeout;

        // Set the shutdown timer
        _requestTimer->setTimeout(timeout, [this]() { shutdown(); });
    }
}

}  // namespace executor
}  // namespace mongo
