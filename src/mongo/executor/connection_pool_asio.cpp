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

#include "mongo/executor/connection_pool_asio.h"

#include <asio.hpp>

#include "mongo/executor/async_stream_factory_interface.h"

#include "mongo/stdx/memory.h"
#include "mongo/util/assert_util.h"
#include "mongo/rpc/factory.h"
#include "mongo/rpc/reply_interface.h"

namespace mongo {
namespace executor {
namespace connection_pool_asio {

ASIOConnection::ASIOConnection(const HostAndPort& hostAndPort, ASIOImpl* global)
    : _global(global),
      _timer(&global->_impl->_io_service),
      _hostAndPort(hostAndPort),
      _impl(new NetworkInterfaceASIO::AsyncOp(
          TaskExecutor::CallbackHandle(),
          RemoteCommandRequest(hostAndPort, std::string("admin"), BSON("isMaster" << 1), BSONObj()),
          [this](const TaskExecutor::ResponseStatus& status) {
              _setupCallback(this, status.isOK() ? Status::OK() : status.getStatus());
          },
          Date_t::now())) {}

void ASIOConnection::indicateUsed() {
    _lastUsed = _global->now();
}

const HostAndPort& ASIOConnection::getHostAndPort() const {
    return _hostAndPort;
}

stdx::chrono::time_point<stdx::chrono::steady_clock> ASIOConnection::getLastUsed() const {
    return _lastUsed;
}

void ASIOConnection::setTimeout(timeoutCallback cb, stdx::chrono::milliseconds timeout) {
    _timer.setTimeout(std::move(cb), timeout);
}

void ASIOConnection::cancelTimeout() {
    _timer.cancelTimeout();
}

void ASIOConnection::setup(setupCallback cb, stdx::chrono::milliseconds timeout) {
    _setupCallback = std::move(cb);

    _global->_impl->_connect(_impl.get());
}

void ASIOConnection::refresh(refreshCallback cb, stdx::chrono::milliseconds timeout) {}

std::unique_ptr<NetworkInterfaceASIO::AsyncOp> ASIOConnection::releaseAsyncOp() {
    return std::move(_impl);
}

void ASIOConnection::bindAsyncOp(std::unique_ptr<NetworkInterfaceASIO::AsyncOp> op) {
    _impl = std::move(op);
}

ASIOTimer::ASIOTimer(asio::io_service* io_service) : _io_service(io_service), _impl(*io_service) {}

void ASIOTimer::setTimeout(timeoutCallback cb, stdx::chrono::milliseconds timeout) {
    _cb = std::move(cb);

    _impl.expires_after(timeout);
    _impl.async_wait([this](const asio::error_code& error) {
        if (error != asio::error::operation_aborted)
            _cb();
    });
}

void ASIOTimer::cancelTimeout() {
    _impl.cancel();
}

ASIOImpl::ASIOImpl(NetworkInterfaceASIO* impl) : _impl(impl) {}

stdx::chrono::time_point<stdx::chrono::steady_clock> ASIOImpl::now() {
    return stdx::chrono::steady_clock::now();
}

std::unique_ptr<ConnectionPoolTimerInterface> ASIOImpl::makeTimer() {
    return stdx::make_unique<ASIOTimer>(&_impl->_io_service);
}

std::unique_ptr<ConnectionPoolConnectionInterface> ASIOImpl::makeConnection(
    const HostAndPort& hostAndPort) {
    return stdx::make_unique<ASIOConnection>(hostAndPort, this);
}

}  // namespace connection_pool_asio
}  // namespace executor
}  // namespace mongo
