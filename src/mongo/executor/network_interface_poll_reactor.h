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

#pragma once

#include "mongo/executor/network_interface.h"
#include "mongo/executor/poll_reactor.h"
#include "mongo/stdx/functional.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace executor {

class NetworkInterfacePollReactor : public NetworkInterface {
public:
    NetworkInterfacePollReactor(PollReactor* reactor) : _reactor(reactor) {}

    std::string getDiagnosticString() override {
        return "";
    }
    void appendConnectionStats(ConnectionPoolStats* stats) const override {}
    std::string getHostName() override {
        return "";
    }
    void startup() override {}
    void shutdown() override {}
    bool inShutdown() const override {
        return false;
    }
    void waitForWork() override {}
    void waitForWorkUntil(Date_t when) override {}
    void signalWorkAvailable() override {}
    Date_t now() override {
        return Date_t::now();
    }
    Status startCommand(const TaskExecutor::CallbackHandle& cbHandle,
                        RemoteCommandRequest& request,
                        const RemoteCommandCompletionFn& onFinish,
                        PollReactor* reactor = nullptr) override {
        return Status::OK();
    }
    void cancelCommand(const TaskExecutor::CallbackHandle& cbHandle) override {}
    void cancelAllCommands() override {}
    Status setAlarm(Date_t when, const stdx::function<void()>& action) override {
        _reactor->setTimer(when, action);
        return Status::OK();
    }

    bool onNetworkThread() override {
        return true;
    }

private:
    PollReactor* _reactor;
};


}  // namespace executor
}  // namespace mongo
