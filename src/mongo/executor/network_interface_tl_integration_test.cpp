/**
 * Copyright (C) 2018 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kASIO

#include "mongo/platform/basic.h"

#include <algorithm>
#include <exception>

#include "mongo/client/connection_string.h"
#include "mongo/db/client.h"
#include "mongo/db/service_context_noop.h"
#include "mongo/executor/network_interface_tl.h"
#include "mongo/rpc/get_status_from_command_result.h"
#include "mongo/stdx/functional.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/thread.h"
#include "mongo/transport/transport_layer_asio.h"
#include "mongo/unittest/integration_test.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"

namespace mongo {
namespace executor {
namespace {

class MockCallbackState final : public TaskExecutor::CallbackState {
public:
    MockCallbackState() = default;
    void cancel() override {}
    void waitForCompletion() override {}
    bool isCanceled() const override {
        return false;
    }
};

TEST(NetworkInterfaceTLIntegrationTest, Ping) {
    //    return;
    ConnectionPool::Options opts;
    auto svcContext = std::make_unique<ServiceContextNoop>();

    transport::TransportLayerASIO::Options tlasioOpts;
    tlasioOpts.mode = transport::TransportLayerASIO::kEgress;
    auto tl = std::make_unique<transport::TransportLayerASIO>(tlasioOpts, nullptr);
    auto tlPtr = tl.get();
    svcContext->setTransportLayer(std::move(tl));

    auto net = std::make_unique<NetworkInterfaceTL>(
        "NetworkInterfaceTL", opts, svcContext.get(), nullptr, nullptr);
    net->startup();

    constexpr size_t nThreads = 1;

    std::array<stdx::thread, nThreads> threads;

    std::string padding(1 << 24, 'x');

    for (auto& thread : threads) {
        thread = stdx::thread([&net, tlPtr, &svcContext, &padding] {
            auto client = svcContext->makeClient("Ping");
            auto opCtx = client->makeOperationContext();

            //auto baton = tlPtr->makeBaton(opCtx.get());
            //            baton.reset();

            stdx::mutex mutex;
            std::vector<std::pair<size_t, size_t>> todo;
            size_t remaining = 1;
            stdx::condition_variable condvar;

            RemoteCommandRequest request{unittest::getFixtureConnectionString().getServers()[0],
                                         "admin",
                                         BSON("ping" << 1 << "ignored" << padding),
                                         BSONObj(),
                                         nullptr,
                                         Seconds(30)};

            auto runCommand = [&](size_t idx, size_t n) {
                return net
                    ->startCommand(
                        TaskExecutor::CallbackHandle(std::make_shared<MockCallbackState>()),
                        request,
                    [idx, n, &mutex, &todo, &remaining, &condvar](auto res) {
                        ASSERT_OK(res.status);
                        ASSERT_OK(getStatusFromCommandResult(res.data));
                        ASSERT(!res.data["writeErrors"]);

                        stdx::lock_guard<stdx::mutex> lk(mutex);
                        if (n > 0) {
                            todo.push_back(std::make_pair(idx, n - 1));
                        } else {
                            remaining--;
                        }

                        condvar.notify_one();
                    });
            };

            for (size_t idx = 0; idx != remaining; ++idx) {
                todo.push_back(std::make_pair(idx, 1));
            }

            stdx::unique_lock<stdx::mutex> lk(mutex);

            while (remaining) {
                using std::swap;
                decltype(todo) working;

                swap(working, todo);

                lk.unlock();
                for (auto& task : working) {
                    invariant(runCommand(task.first, task.second).isOK());
                }

//                if (baton) {
//                    invariant(baton->run(boost::none, opCtx.get()));
//                    lk.lock();
//                } else {
                    lk.lock();
                    condvar.wait(lk, [&] { return todo.size() || !remaining; });
//                }
            }

 //           if (baton) {
 //               baton->detach();
 //           }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    sleep(1);

    net->shutdown();
    svcContext->getTransportLayer()->shutdown();

    std::cout << "done\n";
}

}  // namespace
}  // namespace executor
}  // namespace mongo
