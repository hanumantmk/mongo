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

#include "mongo/executor/connection_pool.h"
#include "mongo/executor/network_interface_asio.h"
#include "mongo/executor/network_interface.h"
#include "mongo/executor/async_stream_interface.h"

namespace mongo {
namespace executor {
namespace connection_pool_asio {

/**
 * Implements connection pool timers on top of asio
 */
class ASIOTimer : public ConnectionPool::TimerInterface {
public:
    ASIOTimer(asio::io_service* service);

    void setTimeout(Milliseconds timeout, timeoutCallback cb) override;
    void cancelTimeout() override;

private:
    timeoutCallback _cb;
    asio::io_service* _io_service;
    asio::steady_timer _impl;
};

/**
 * Implements connection pool connections on top of asio
 *
 * Owns an async op when it's out of the pool
 */
class ASIOConnection : public ConnectionPool::ConnectionInterface {
public:
    ASIOConnection(const HostAndPort& hostAndPort, ASIOImpl* global);

    void indicateUsed() override;
    const HostAndPort& getHostAndPort() const override;

    std::unique_ptr<NetworkInterfaceASIO::AsyncOp> releaseAsyncOp();
    void bindAsyncOp(std::unique_ptr<NetworkInterfaceASIO::AsyncOp> op);

protected:
    Date_t getLastUsed() const override;

    void setTimeout(Milliseconds timeout, timeoutCallback cb) override;
    void cancelTimeout() override;

    void setup(Milliseconds timeout, setupCallback cb) override;
    void refresh(Milliseconds timeout, refreshCallback cb) override;

private:
    setupCallback _setupCallback;
    refreshCallback _refreshCallback;
    ASIOImpl* _global;
    ASIOTimer _timer;
    Date_t _lastUsed;
    HostAndPort _hostAndPort;
    std::unique_ptr<NetworkInterfaceASIO::AsyncOp> _impl;
};

/**
 * Implementions connection pool implementation for asio
 */
class ASIOImpl : public ConnectionPool::DependentTypeFactoryInterface {
    friend class ASIOConnection;

public:
    ASIOImpl(NetworkInterfaceASIO* impl);

    std::unique_ptr<ConnectionPool::ConnectionInterface> makeConnection(
        const HostAndPort& hostAndPort) override;
    std::unique_ptr<ConnectionPool::TimerInterface> makeTimer() override;

    Date_t now() override;

private:
    NetworkInterfaceASIO* _impl;
};

}  // namespace connection_pool_asio
}  // namespace executor
}  // namespace mongo
