/**
 *    Copyright (C) 2016 MongoDB Inc.
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

#include "mongo/executor/poll_reactor_executor_factory.h"

#include "mongo/executor/network_interface_poll_reactor.h"
#include "mongo/executor/network_interface_thread_pool.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/stdx/memory.h"

namespace mongo {
namespace executor {

const ServiceContext::Decoration<PollReactorExecutorFactory> PollReactorExecutorFactory::get =
    ServiceContext::declareDecoration<PollReactorExecutorFactory>();

class PollReactorExecutorFactory::Executor {
public:
    Executor(const std::shared_ptr<NetworkInterface>& network)
        : _networkInterfacePollReactor(&_reactor),
          _executor(stdx::make_unique<executor::NetworkInterfaceThreadPool>(
                        &_networkInterfacePollReactor),
                    network,
                    &_reactor) {
        _executor.startup();
    }

    ~Executor() {
        _executor.shutdown();
        _executor.join();
    }

    TaskExecutor* getExecutor() {
        return &_executor;
    }

private:
    PollReactor _reactor;
    NetworkInterfacePollReactor _networkInterfacePollReactor;
    ThreadPoolTaskExecutor _executor;
};


PollReactorExecutorFactory::Handle::Handle(PollReactorExecutorFactory* factory, Executor* ptr)
    : data(ptr, Dtor(factory)) {}

PollReactorExecutorFactory::Handle::~Handle() = default;

PollReactorExecutorFactory::Handle::Handle(Handle&& other) = default;
auto PollReactorExecutorFactory::Handle::operator=(Handle&& other) -> Handle& = default;

TaskExecutor* PollReactorExecutorFactory::Handle::get() {
    return data->getExecutor();
}

auto PollReactorExecutorFactory::getExecutor(const std::shared_ptr<NetworkInterface>& network)
    -> Handle {
    stdx::lock_guard<stdx::mutex> lk(_mutex);

    if (!_network) {
        _network = network;
    } else {
        invariant(_network == network);
    }

    if (_executors.empty()) {
        _executors.push(stdx::make_unique<Executor>(_network));
    }

    auto executor = std::move(_executors.top());
    _executors.pop();

    return Handle(this, executor.release());
}

void PollReactorExecutorFactory::returnExecutor(Executor* executor) {
    stdx::lock_guard<stdx::mutex> lk(_mutex);

    _executors.push(std::unique_ptr<Executor>(executor));
}

}  // namespace executor
}  // namespace mongo
