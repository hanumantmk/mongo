/**
 *    Copyright (C) 2015 MongoDB Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT mongo::logger::LogComponent::kExecutor

#include "mongo/platform/basic.h"

#include "mongo/executor/this_thread_pool.h"

#include "mongo/executor/network_interface.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace executor {

ThisThreadPool::ThisThreadPool(NetworkInterface* net)
    : _net(net) {}

ThisThreadPool::~ThisThreadPool() {
    {
        stdx::unique_lock<stdx::mutex> lk(_mutex);
        _inShutdown = true;
    }

    join();

    invariant(_tasks.empty());
}

void ThisThreadPool::startup() {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    invariant(!_started);
    _started = true;

    consumeTasks(std::move(lk));
}

void ThisThreadPool::shutdown() {
    stdx::lock_guard<stdx::mutex> lk(_mutex);
    _inShutdown = true;
    _net->signalWorkAvailable();
}

void ThisThreadPool::join() {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    _joining = true;

    _net->signalWorkAvailable();
    _condvar.wait(lk, [&] { return _tasks.empty(); });
    invariant(_tasks.empty());
}

Status ThisThreadPool::schedule(Task task) {
    stdx::unique_lock<stdx::mutex> lk(_mutex);
    if (_inShutdown) {
        return {ErrorCodes::ShutdownInProgress, "Shutdown in progress"};
    }
    _tasks.emplace_back(std::move(task));

    if (_started)
        consumeTasks(std::move(lk));

    return Status::OK();
}

void ThisThreadPool::consumeTasks(stdx::unique_lock<stdx::mutex> lk) {
    if (_consumingTasks || _tasks.empty())
        return;

    if (! _net->onNetworkThread()) {
        lk.unlock();
        _net->setAlarm(_net->now(), [this]{
            stdx::unique_lock<stdx::mutex> lk(_mutex);
            consumeTasks(std::move(lk));
        });

        return;
    }

    _consumingTasks = true;
    auto guard = MakeGuard([&]{ _consumingTasks = false; });

    decltype(_tasks) tasks;

    while (_tasks.size()) {
        using std::swap;
        swap(tasks, _tasks);

        lk.unlock();

        for (auto&& task : tasks) {
            task();
        }

        tasks.clear();

        lk.lock();
    }

    if (_joining)
        _condvar.notify_one();
}

}  // namespace executor
}  // namespace mongo
