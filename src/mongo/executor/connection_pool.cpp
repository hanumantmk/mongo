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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kASIO

#include "mongo/platform/basic.h"

#include "mongo/executor/connection_pool.h"

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/executor/connection_pool_stats.h"
#include "mongo/executor/remote_command_request.h"
#include "mongo/stdx/condition_variable.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/mutex.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/destructor_guard.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

// One interesting implementation note herein concerns how setup() and
// refresh() are invoked outside of the global lock, but setTimeout is not.
// This implementation detail simplifies mocks, allowing them to return
// synchronously sometimes, whereas having timeouts fire instantly adds little
// value. In practice, dumping the locks is always safe (because we restrict
// ourselves to operations over the connection).

namespace mongo {
namespace executor {

namespace {
const Milliseconds kBackgroundPeriod = Milliseconds(100);
const Milliseconds kHealthyAssumption = Milliseconds(100);
}  // namespace

/**
 * A pool for a specific HostAndPort
 *
 * Pools come into existance the first time a connection is requested and
 * go out of existence after hostTimeout passes without any of their
 * connections being used.
 */
class ConnectionPool::SpecificPool {
public:
    using ConnectionState = ConnectionPool::ConnectionState;

    SpecificPool(ConnectionPool* parent, const HostAndPort& hostAndPort);

    /**
     * Gets a connection from the specific pool. Sinks a unique_lock from the
     * parent to preserve the lock on _mutex
     */
    void getConnection(const HostAndPort& hostAndPort,
                       Milliseconds timeout,
                       stdx::unique_lock<stdx::mutex> lk,
                       GetConnectionCallback cb);

    StatusWith<ConnectionHandle> getConnectionSync(const HostAndPort& hostAndPort,
                                                   Milliseconds timeout,
                                                   stdx::unique_lock<stdx::mutex> lk);

    /**
     * Cascades a failure across existing connections and requests. Invoking
     * this function drops all current connections and fails all current
     * requests with the passed status.
     */
    void processFailure(const Status& status, stdx::unique_lock<stdx::mutex> lk);

    /**
     * Returns a connection to a specific pool. Sinks a unique_lock from the
     * parent to preserve the lock on _mutex
     */
    void returnConnection(ConnectionIterator connection, stdx::unique_lock<stdx::mutex>& lk);

    /**
     * Returns the number of connections currently checked out of the pool.
     */
    size_t inUseConnections(const stdx::unique_lock<stdx::mutex>& lk);

    /**
     * Returns the number of available connections in the pool.
     */
    size_t availableConnections(const stdx::unique_lock<stdx::mutex>& lk);

    /**
     * Returns the total number of connections ever created in this pool.
     */
    size_t createdConnections(const stdx::unique_lock<stdx::mutex>& lk);

    void handlePeriodicTasks(stdx::unique_lock<stdx::mutex>& lk);

private:
    using OwnershipPool =
        std::list<std::pair<ConnectionPool::ConnectionState, std::unique_ptr<ConnectionInterface>>>;

    using Request = std::pair<Date_t, GetConnectionCallback>;
    struct RequestComparator {
        bool operator()(const Request& a, const Request& b) {
            return a.first > b.first;
        }
    };

    void addToReady(stdx::unique_lock<stdx::mutex>& lk, OwnershipPool conn);

    void fulfillRequests(stdx::unique_lock<stdx::mutex>& lk);

    void spawnConnections(stdx::unique_lock<stdx::mutex>& lk);

    OwnershipPool takeFromPool(ConnectionState state, ConnectionIterator connection);
    OwnershipPool takeFromProcessingPool(ConnectionIterator connection);

    OwnershipPool& getPool(ConnectionState state);

    ConnectionIterator moveToPool(ConnectionState state, OwnershipPool);

    void updateStateInLock();

private:
    ConnectionPool* const _parent;

    const HostAndPort _hostAndPort;

    std::array<OwnershipPool, 4> _pools;

    std::priority_queue<Request, std::vector<Request>, RequestComparator> _requests;

    size_t _generation;
    bool _inFulfillRequests;

    size_t _created;

    Date_t _lastActive;

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
        kRunning,

        // No current activity, waiting for hostTimeout to pass
        kIdle,

        // hostTimeout is passed, we're waiting for any processing
        // connections to finish before shutting down
        kInShutdown,
    };

    State _state;
};

Milliseconds const ConnectionPool::kDefaultRefreshTimeout = Seconds(20);
Milliseconds const ConnectionPool::kDefaultRefreshRequirement = Seconds(60);
Milliseconds const ConnectionPool::kDefaultHostTimeout = Minutes(5);

const Status ConnectionPool::kConnectionStateUnknown =
    Status(ErrorCodes::InternalError, "Connection is in an unknown state");

ConnectionPool::ConnectionPool(std::unique_ptr<DependentTypeFactoryInterface> impl, Options options)
    : _options(std::move(options)), _factory(std::move(impl)) {}

ConnectionPool::~ConnectionPool() {
    for (auto&& pair : _pools) {
        delete pair.second;
    }
}

void ConnectionPool::start() {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    _taskTimer = _factory->makeTimer();
    _taskTimer->setTimeout(kBackgroundPeriod, [this] { handlePeriodicTasks(); });
}

void ConnectionPool::shutdown() {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    _taskTimer->cancelTimeout();
}

void ConnectionPool::dropConnections(const HostAndPort& hostAndPort) {
    auto key = PoolsMap::HashedKey(&hostAndPort);

    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto iter = _pools.find(key);

    if (iter == _pools.end())
        return;

    iter->second->processFailure(
        Status(ErrorCodes::PooledConnectionsDropped, "Pooled connections dropped"), std::move(lk));
}

void ConnectionPool::get(const HostAndPort& hostAndPort,
                         Milliseconds timeout,
                         GetConnectionCallback cb) {
    SpecificPool* pool;
    auto key = PoolsMap::HashedKey(&hostAndPort);

    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto iter = _pools.find(key);

    if (iter == _pools.end()) {
        auto handle = stdx::make_unique<SpecificPool>(this, hostAndPort);
        pool = handle.get();
        _pools[key] = handle.release();
    } else {
        pool = iter->second;
    }

    dassert(pool);

    pool->getConnection(hostAndPort, timeout, std::move(lk), std::move(cb));
}

auto ConnectionPool::getSync(const HostAndPort& hostAndPort, Milliseconds timeout)
    -> StatusWith<ConnectionHandle> {
    SpecificPool* pool;
    auto key = PoolsMap::HashedKey(&hostAndPort);

    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto iter = _pools.find(key);

    if (iter == _pools.end()) {
        auto handle = stdx::make_unique<SpecificPool>(this, hostAndPort);
        pool = handle.get();
        _pools[key] = handle.release();
    } else {
        pool = iter->second;
    }

    dassert(pool);

    return pool->getConnectionSync(hostAndPort, timeout, std::move(lk));
}

void ConnectionPool::appendConnectionStats(ConnectionPoolStats* stats) const {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    for (const auto& kv : _pools) {
        auto& host = kv.first;

        auto& pool = kv.second;
        ConnectionStatsPerHost hostStats{pool->inUseConnections(lk),
                                         pool->availableConnections(lk),
                                         pool->createdConnections(lk)};
        stats->updateStatsForHost(host, hostStats);
    }
}

void ConnectionPool::returnConnection(ConnectionIterator conn) {
    auto key = PoolsMap::HashedKey(&(conn->second->getHostAndPort()));

    stdx::unique_lock<stdx::mutex> lk(_mutex);

    auto iter = _pools.find(key);

    dassert(iter != _pools.end());

    iter->second->returnConnection(conn, lk);
}

void ConnectionPool::handlePeriodicTasks() {
    stdx::unique_lock<stdx::mutex> lk(_mutex);

    std::vector<SpecificPool*> pools;

    for (auto& pair : _pools) {
        pools.push_back(pair.second);
    }

    for (auto& pool : pools) {
        pool->handlePeriodicTasks(lk);
    }

    _taskTimer->setTimeout(kBackgroundPeriod, [this] { handlePeriodicTasks(); });
}

ConnectionPool::SpecificPool::SpecificPool(ConnectionPool* parent, const HostAndPort& hostAndPort)
    : _parent(parent),
      _hostAndPort(hostAndPort),
      _generation(0),
      _inFulfillRequests(false),
      _created(0),
      _state(State::kRunning) {}

ConnectionPool::SpecificPool::OwnershipPool& ConnectionPool::SpecificPool::getPool(
    ConnectionState state) {
    return _pools[static_cast<uint8_t>(state)];
}

size_t ConnectionPool::SpecificPool::inUseConnections(const stdx::unique_lock<stdx::mutex>& lk) {
    return getPool(ConnectionState::CheckedOut).size();
}

size_t ConnectionPool::SpecificPool::availableConnections(
    const stdx::unique_lock<stdx::mutex>& lk) {
    return getPool(ConnectionState::Ready).size();
}

size_t ConnectionPool::SpecificPool::createdConnections(const stdx::unique_lock<stdx::mutex>& lk) {
    return _created;
}

void ConnectionPool::SpecificPool::getConnection(const HostAndPort& hostAndPort,
                                                 Milliseconds timeout,
                                                 stdx::unique_lock<stdx::mutex> lk,
                                                 GetConnectionCallback cb) {
    // We need some logic here to handle kNoTimeout, which is defined as -1 Milliseconds. If we just
    // added the timeout, we would get a time 1MS in the past, which would immediately timeout - the
    // exact opposite of what we want.
    auto expiration = (timeout == RemoteCommandRequest::kNoTimeout)
        ? RemoteCommandRequest::kNoExpirationDate
        : _parent->_factory->now() + timeout;

    _requests.push(make_pair(expiration, std::move(cb)));

    updateStateInLock();

    spawnConnections(lk);
    fulfillRequests(lk);
}

StatusWith<ConnectionPool::ConnectionHandle> ConnectionPool::SpecificPool::getConnectionSync(
    const HostAndPort& hostAndPort, Milliseconds timeout, stdx::unique_lock<stdx::mutex> lk) {

    auto& readyPool = getPool(ConnectionState::Ready);

    auto iter = readyPool.begin();

    if (iter != readyPool.end()) {
        auto conn = iter->second.get();
        auto handle = takeFromPool(ConnectionState::Ready, iter);

        lk.unlock();
        // Grab the connection and cancel its timeout
        conn->cancelTimeout();

        auto isHealthy = (conn->getLastUsed() + kHealthyAssumption < _parent->_factory->now()) ||
            conn->isHealthy();

        lk.lock();

        if (isHealthy) {
            auto iter = moveToPool(ConnectionState::CheckedOut, std::move(handle));

            updateStateInLock();

            lk.unlock();

            // pass it to the user
            conn->resetToUnknown();

            return ConnectionHandle(_parent, iter);
        }
    }

    stdx::mutex mutex;
    stdx::condition_variable condvar;
    boost::optional<StatusWith<ConnectionHandle>> out;

    getConnection(hostAndPort, timeout, std::move(lk), [&](StatusWith<ConnectionHandle> conn) {
        stdx::lock_guard<stdx::mutex> lk(mutex);

        out.emplace(std::move(conn));
        condvar.notify_one();
    });

    stdx::unique_lock<stdx::mutex> child_lk(mutex);
    condvar.wait(child_lk, [&] { return static_cast<bool>(out); });

    return std::move(out.get());
}

void ConnectionPool::SpecificPool::returnConnection(ConnectionIterator iter,
                                                    stdx::unique_lock<stdx::mutex>& lk) {
    auto conn = iter->second.get();
    auto needsRefreshTP = conn->getLastUsed() + _parent->_options.refreshRequirement;
    auto handle = takeFromPool(ConnectionState::CheckedOut, iter);

    updateStateInLock();

    // Users are required to call indicateSuccess() or indicateFailure() before allowing
    // a connection to be returned. Otherwise, we have entered an unknown state.
    dassert(conn->getStatus() != kConnectionStateUnknown);

    if (conn->getGeneration() != _generation) {
        // If the connection is from an older generation, just return.
        return;
    }

    if (!conn->getStatus().isOK()) {
        // TODO: alert via some callback if the host is bad
        return;
    }

    auto now = _parent->_factory->now();

    if (needsRefreshTP <= now) {
        // If we need to refresh this connection

        if (getPool(ConnectionState::Ready).size() + getPool(ConnectionState::Processing).size() +
                getPool(ConnectionState::CheckedOut).size() >=
            _parent->_options.minConnections) {
            // If we already have minConnections, just let the connection lapse
            return;
        }

        iter = moveToPool(ConnectionState::Processing, std::move(handle));

        // Unlock in case refresh can occur immediately
        lk.unlock();
        conn->refresh(_parent->_options.refreshTimeout,
                      [this](ConnectionIterator conn, Status status) {
                          auto connPtr = conn->second.get();
                          connPtr->indicateUsed();

                          stdx::unique_lock<stdx::mutex> lk(_parent->_mutex);

                          auto handle = takeFromProcessingPool(conn);

                          // If the host and port were dropped, let this lapse
                          if (connPtr->getGeneration() != _generation)
                              return;

                          // If we're in shutdown, we don't need refreshed connections
                          if (_state == State::kInShutdown)
                              return;

                          // If the connection refreshed successfully, throw it back in the ready
                          // pool
                          if (status.isOK()) {
                              addToReady(lk, std::move(handle));
                              return;
                          }

                          // Otherwise pass the failure on through
                          processFailure(status, std::move(lk));
                      });
        lk.lock();
    } else {
        // If it's fine as it is, just put it in the ready queue
        addToReady(lk, std::move(handle));
    }

    updateStateInLock();
}

// Adds a live connection to the ready pool
void ConnectionPool::SpecificPool::addToReady(stdx::unique_lock<stdx::mutex>& lk,
                                              OwnershipPool handle) {
    moveToPool(ConnectionState::Ready, std::move(handle));

    fulfillRequests(lk);
}

// Drop connections and fail all requests
void ConnectionPool::SpecificPool::processFailure(const Status& status,
                                                  stdx::unique_lock<stdx::mutex> lk) {
    // Bump the generation so we don't reuse any pending or checked out
    // connections
    _generation++;

    // Drop ready connections
    getPool(ConnectionState::Ready).clear();

    // Move all processing connections to dropped
    for (auto& pair : getPool(ConnectionState::Processing)) {
        pair.first = ConnectionPool::ConnectionState::Dropped;
    }

    auto& droppedPool = getPool(ConnectionState::Dropped);
    droppedPool.splice(droppedPool.begin(), getPool(ConnectionState::Processing));

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
        requestsToFail.top().second(status);
        requestsToFail.pop();
    }
}

// fulfills as many outstanding requests as possible
void ConnectionPool::SpecificPool::fulfillRequests(stdx::unique_lock<stdx::mutex>& lk) {
    // If some other thread (possibly this thread) is fulfilling requests,
    // don't keep padding the callstack.
    if (_inFulfillRequests || _requests.empty())
        return;

    _inFulfillRequests = true;
    auto guard = MakeGuard([&] { _inFulfillRequests = false; });

    while (_requests.size()) {
        auto iter = getPool(ConnectionState::Ready).begin();

        if (iter == getPool(ConnectionState::Ready).end())
            break;

        auto conn = iter->second.get();

        // Grab the connection and cancel its timeout
        auto handle = takeFromPool(ConnectionState::Ready, iter);
        conn->cancelTimeout();

        auto isHealthy = (conn->getLastUsed() + kHealthyAssumption < _parent->_factory->now()) ||
            conn->isHealthy();

        if (!isHealthy) {
            log() << "dropping unhealthy pooled connection to " << conn->getHostAndPort();

            if (getPool(ConnectionState::Ready).empty()) {
                log() << "after drop, pool was empty, going to spawn some connections";
                // Spawn some more connections to the bad host if we're all out.
                spawnConnections(lk);
            }

            // Retry.
            continue;
        }

        // Grab the request and callback
        auto cb = std::move(_requests.top().second);
        _requests.pop();

        // check out the connection
        iter = moveToPool(ConnectionState::CheckedOut, std::move(handle));

        updateStateInLock();

        // pass it to the user
        conn->resetToUnknown();
        lk.unlock();
        cb(ConnectionHandle(_parent, iter));
        lk.lock();
    }
}

// spawn enough connections to satisfy open requests and minpool, while
// honoring maxpool
void ConnectionPool::SpecificPool::spawnConnections(stdx::unique_lock<stdx::mutex>& lk) {
    // We want minConnections <= outstanding requests <= maxConnections
    auto target = [&] {
        return std::max(_parent->_options.minConnections,
                        std::min(_requests.size() + getPool(ConnectionState::CheckedOut).size(),
                                 _parent->_options.maxConnections));
    };

    // While all of our inflight connections are less than our target
    while (getPool(ConnectionState::Ready).size() + getPool(ConnectionState::Processing).size() +
               getPool(ConnectionState::CheckedOut).size() <
           target()) {
        getPool(ConnectionState::Processing)
            .emplace_front(std::piecewise_construct,
                           std::forward_as_tuple(ConnectionPool::ConnectionState::Processing),
                           std::forward_as_tuple());
        // make a new connection and put it in processing
        _parent->_factory->makeConnection(
            _hostAndPort, _generation, getPool(ConnectionState::Processing).begin());

        auto connPtr = getPool(ConnectionState::Processing).front().second.get();

        ++_created;

        // Run the setup callback
        lk.unlock();
        connPtr->setup(_parent->_options.refreshTimeout,
                       [this](ConnectionIterator iter, Status status) {
                           auto connPtr = iter->second.get();
                           connPtr->indicateUsed();

                           stdx::unique_lock<stdx::mutex> lk(_parent->_mutex);

                           auto handle = takeFromProcessingPool(iter);

                           if (connPtr->getGeneration() != _generation) {
                               // If the host and port was dropped, let the
                               // connection lapse
                           } else if (status.isOK()) {
                               addToReady(lk, std::move(handle));
                           } else {
                               // If the setup failed, cascade the failure edge
                               processFailure(status, std::move(lk));
                           }
                       });
        // Note that this assumes that the refreshTimeout is sound for the
        // setupTimeout

        lk.lock();
    }
}

ConnectionPool::SpecificPool::OwnershipPool ConnectionPool::SpecificPool::takeFromPool(
    ConnectionState state, ConnectionIterator iter) {

    dassert(state == iter->first);

    OwnershipPool out;

    out.splice(out.begin(), getPool(state), iter);

    iter->first = ConnectionState::Unknown;

    return out;
}

ConnectionPool::SpecificPool::OwnershipPool ConnectionPool::SpecificPool::takeFromProcessingPool(
    ConnectionIterator iter) {

    if (iter->first == ConnectionPool::ConnectionState::Processing) {
        return takeFromPool(ConnectionState::Processing, iter);
    }

    return takeFromPool(ConnectionState::Dropped, iter);
}

ConnectionPool::ConnectionIterator ConnectionPool::SpecificPool::moveToPool(ConnectionState state,
                                                                            OwnershipPool handle) {
    dassert(handle.size() == 1);

    auto iter = handle.begin();

    dassert(iter->first == ConnectionState::Unknown);

    auto& pool = getPool(state);

    pool.splice(pool.begin(), handle);

    iter->first = state;

    return iter;
}

// Updates our state and manages the request timer
void ConnectionPool::SpecificPool::updateStateInLock() {
    if (_requests.size()) {
        _lastActive = _parent->_factory->now();

        // We have some outstanding requests, we're live

        _state = State::kRunning;
    } else if (getPool(ConnectionState::CheckedOut).size()) {
        _lastActive = _parent->_factory->now();

        // If we have no requests, but someone's using a connection, we just
        // hang around until the next request or a return

        _state = State::kRunning;
    } else {
        // If we don't have any live requests and no one has checked out connections

        _state = State::kIdle;
    }
}

void ConnectionPool::SpecificPool::handlePeriodicTasks(stdx::unique_lock<stdx::mutex>& lk) {
    auto now = _parent->_factory->now();

    auto& checkedOutPool = getPool(ConnectionState::CheckedOut);

    if (_state == State::kRunning ||
        (_state == State::kIdle && _lastActive + _parent->_options.hostTimeout >= now)) {
        auto now = _parent->_factory->now();

        OwnershipPool toRefresh;

        for (auto iter = getPool(ConnectionState::Ready).begin();
             iter != getPool(ConnectionState::Ready).end();) {
            if (iter->second->getLastUsed() + _parent->_options.refreshRequirement <= now) {
                // Our strategy for refreshing connections is to check them out and
                // immediately check them back in (which kicks off the refresh logic in
                // returnConnection

                auto next = iter;
                ++next;

                toRefresh.splice(toRefresh.begin(), getPool(ConnectionState::Ready), iter);

                iter = next;
            } else {
                ++iter;
            }
        }

        while (toRefresh.begin() != toRefresh.end()) {
            auto iter = toRefresh.begin();

            checkedOutPool.splice(checkedOutPool.begin(), toRefresh, iter);
            iter->first = ConnectionPool::ConnectionState::CheckedOut;

            iter->second->indicateSuccess();

            returnConnection(iter, lk);
        }

        while (_requests.size()) {
            auto& x = _requests.top();

            if (x.first <= now) {
                auto cb = std::move(x.second);
                _requests.pop();

                lk.unlock();
                cb(Status(ErrorCodes::ExceededTimeLimit,
                          "Couldn't get a connection within the time limit"));
                lk.lock();
            } else {
                break;
            }
        }

        updateStateInLock();
    }

    if (_state == State::kIdle && _lastActive + _parent->_options.hostTimeout < now) {
        _state = State::kInShutdown;
    }

    if (_state == State::kInShutdown) {
        if (!(getPool(ConnectionState::Processing).size() ||
              getPool(ConnectionState::Dropped).size())) {
            dassert(_requests.empty());
            dassert(getPool(ConnectionState::CheckedOut).empty());

            auto key = PoolsMap::HashedKey(&_hostAndPort);

            _parent->_pools.erase(key);
            delete this;
        }
    }
}

ConnectionPool::ConnectionHandle::ConnectionHandle() : _pool(nullptr) {}

ConnectionPool::ConnectionHandle::~ConnectionHandle() {
    if (_pool) {
        _pool->returnConnection(_conn);
    }
}

ConnectionPool::ConnectionHandle::ConnectionHandle(ConnectionHandle&& other)
    : _pool(other._pool), _conn(other._conn) {
    other._pool = nullptr;
}

ConnectionPool::ConnectionHandle& ConnectionPool::ConnectionHandle::operator=(
    ConnectionHandle&& other) {
    if (this == &other) {
        return *this;
    }

    if (_pool) {
        _pool->returnConnection(_conn);
    }

    _pool = other._pool;
    _conn = other._conn;

    other._pool = nullptr;

    return *this;
}

ConnectionPool::ConnectionInterface* ConnectionPool::ConnectionHandle::get() const {
    if (_pool) {
        return _conn->second.get();
    } else {
        return nullptr;
    }
}

void ConnectionPool::ConnectionHandle::reset() {
    if (_pool) {
        _pool->returnConnection(_conn);
    }

    _pool = nullptr;
}

ConnectionPool::ConnectionHandle::ConnectionHandle(ConnectionPool* pool, ConnectionIterator conn)
    : _pool(pool), _conn(conn) {}

}  // namespace executor
}  // namespace mongo
