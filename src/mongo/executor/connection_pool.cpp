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

namespace mongo {
namespace executor {

stdx::chrono::milliseconds const ConnectionPool::kRefreshTimeout = stdx::chrono::milliseconds(5000);
stdx::chrono::milliseconds const ConnectionPool::kRefreshRequirement =
    stdx::chrono::milliseconds(5000);

ConnectionPool::ConnectionPool(std::unique_ptr<ConnectionPoolImplInterface> impl, Options options)
    : _impl(std::move(impl)), _options(std::move(options)) {}

void ConnectionPool::getConnection(const HostAndPort& hostAndPort,
                                   stdx::chrono::milliseconds timeout,
                                   getConnectionCallback cb) {
    SpecificPool* pool;

    {
        stdx::unique_lock<stdx::mutex> cpLk(_mutex);

        auto iter = _pools.find(hostAndPort);

        if (iter == _pools.end()) {
            auto handle = stdx::make_unique<SpecificPool>(this);
            pool = handle.get();
            _pools[hostAndPort] = std::move(handle);
        } else {
            pool = iter->second.get();
        }
    }

    pool->getConnection(hostAndPort, timeout, std::move(cb));
}

ConnectionPool::SpecificPool::SpecificPool(ConnectionPool* global)
    : _global(global), _requestTimer(global->_impl->makeTimer()) {}

void ConnectionPool::SpecificPool::sortRequests() {
    using pair = decltype(_requests)::const_reference;

    std::sort(_requests.begin(), _requests.end(), [](pair a, pair b) { return a.first > b.first; });
}

void ConnectionPool::SpecificPool::getConnection(const HostAndPort& hostAndPort,
                                                 stdx::chrono::milliseconds timeout,
                                                 getConnectionCallback cb) {
    stdx::unique_lock<stdx::mutex> spLk(_mutex);

    auto expiration = _global->_impl->now() + timeout;
    bool timerNeedsUpdate = !_requests.size() || (expiration < _requests.back().first);

    _requests.push_back(make_pair(expiration, std::move(cb)));
    sortRequests();

    if (timerNeedsUpdate) {
        _requestTimer->cancelTimeout();

        spLk.unlock();
        _requestTimer->setTimeout([this]() {
            stdx::unique_lock<stdx::mutex> spLk(_mutex);

            auto now = _global->_impl->now();

            while (_requests.size()) {
                auto& x = _requests.back();

                if (x.first <= now) {
                    auto cb = std::move(x.second);
                    _requests.pop_back();

                    spLk.unlock();
                    cb(Status(ErrorCodes::ExceededTimeLimit,
                              "Couldn't get a connection within the time limit"));
                    spLk.lock();
                } else {
                    break;
                }
            }
        }, timeout);
        spLk.lock();
    }

    if (_readyPool.size()) {
        fulfillRequests(spLk);
    } else {
        spawnConnections(spLk, hostAndPort);
    }
}

void ConnectionPool::returnConnection(ConnectionPoolConnectionInterface* conn) {
    SpecificPool* pool;

    {
        stdx::unique_lock<stdx::mutex> cpLk(_mutex);

        auto iter = _pools.find(conn->getHostAndPort());

        invariant(iter != _pools.end());

        pool = iter->second.get();
    }

    pool->returnConnection(conn);
}

void ConnectionPool::SpecificPool::spawnConnections(stdx::unique_lock<stdx::mutex>& spLk,
                                                    const HostAndPort& hostAndPort) {
    size_t target = std::max(_global->_options.minConnections,
                             std::min(_requests.size(), _global->_options.maxConnections));

    while (_readyPool.size() + _processingPool.size() + _checkedOutPool.size() < target) {
        auto handle = _global->_impl->makeConnection(hostAndPort);
        auto connPtr = handle.get();
        _processingPool[connPtr] = std::move(handle);

        spLk.unlock();
        connPtr->setup(
            [this](ConnectionPoolConnectionInterface* connPtr, Status status) {
                connPtr->indicateUsed();

                stdx::unique_lock<stdx::mutex> spLk(_mutex);

                auto conn = takeFromPool(_processingPool, connPtr);

                if (status.isOK()) {
                    addToReady(spLk, std::move(conn));
                }

                // TODO the else here needs to decide if that error goes to
                // some request, or how we want manage that (let them timeout?

            },
            _global->_options.refreshTimeout);
        spLk.lock();
    }
}

ConnectionPool::SpecificPool::ConnectionHandle ConnectionPool::SpecificPool::takeFromPool(
    std::map<ConnectionPoolConnectionInterface*, ConnectionHandle>& pool,
    ConnectionPoolConnectionInterface* connPtr) {
    auto iter = pool.find(connPtr);
    invariant(iter != pool.end());

    auto conn = std::move(iter->second);
    pool.erase(iter);
    return conn;
}

void ConnectionPool::SpecificPool::returnConnection(ConnectionPoolConnectionInterface* connPtr) {
    stdx::unique_lock<stdx::mutex> spLk(_mutex);

    auto needsRefreshTP = connPtr->getLastUsed() + _global->_options.refreshRequirement;
    auto now = _global->_impl->now();

    auto conn = takeFromPool(_checkedOutPool, connPtr);

    if (needsRefreshTP <= now) {
        if (_readyPool.size() + _processingPool.size() + _checkedOutPool.size() >
            _global->_options.minConnections) {
            return;
        }

        _processingPool[connPtr] = std::move(conn);

        spLk.unlock();
        connPtr->refresh([this](ConnectionPoolConnectionInterface* connPtr, Status status) {
            connPtr->indicateUsed();

            stdx::unique_lock<stdx::mutex> spLk(_mutex);

            auto conn = takeFromPool(_processingPool, connPtr);

            if (status.isOK()) {
                addToReady(spLk, std::move(conn));
            }
        }, _global->_options.refreshTimeout);
        spLk.lock();
    } else {
        addToReady(spLk, std::move(conn));
    }
}

void ConnectionPool::SpecificPool::fulfillRequests(stdx::unique_lock<stdx::mutex>& spLk) {
    while (_requests.size()) {
        auto iter = _readyPool.begin();

        if (iter == _readyPool.end())
            break;

        auto conn = std::move(iter->second);
        _readyPool.erase(iter);
        conn->cancelTimeout();

        auto request = _requests.back();
        auto cb = std::move(request.second);
        _requests.pop_back();

        auto connPtr = conn.get();
        _checkedOutPool[connPtr] = std::move(conn);

        spLk.unlock();
        cb(connPtr);
        spLk.lock();
    }
}

void ConnectionPool::SpecificPool::addToReady(stdx::unique_lock<stdx::mutex>& spLk,
                                              ConnectionHandle conn) {
    auto connPtr = conn.get();

    _readyPool[connPtr] = std::move(conn);

    spLk.unlock();
    connPtr->setTimeout([this, connPtr]() {
        ConnectionHandle conn;

        {
            stdx::unique_lock<stdx::mutex> spLk(_mutex);

            conn = takeFromPool(_readyPool, connPtr);

            _checkedOutPool[connPtr] = std::move(conn);
        }

        returnConnection(connPtr);
    }, _global->_options.refreshRequirement);
    spLk.lock();

    fulfillRequests(spLk);
}

}  // namespace executor
}  // namespace mongo
