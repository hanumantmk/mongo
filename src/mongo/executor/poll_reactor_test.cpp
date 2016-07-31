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

#include <asio.hpp>
#include <memory>
#include <system_error>
#include <vector>

#include "mongo/executor/async_stream.h"
#include "mongo/executor/network_interface_asio_test_utils.h"
#include "mongo/executor/poll_reactor.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/mutex.h"
#include "mongo/stdx/thread.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"

namespace mongo {
namespace {

using asio::ip::tcp;

class Server {
public:
    Server(asio::io_service& io_service)
        : _acceptor(io_service, tcp::endpoint{asio::ip::make_address_v4("127.0.0.1"), 0}),
          _socket(io_service) {
        _endpoint.emplace(_acceptor.local_endpoint());

        doAccept();
    }

    void doAccept() {
        _acceptor.async_accept(_socket, [this](asio::error_code ec) {
            if (!ec) {
                handleSession(std::move(_socket));
            }

            doAccept();
        });
    }

    void handleSession(tcp::socket socket) {
        stdx::lock_guard<stdx::mutex> lk(_mutex);

        auto clientSocket = std::make_shared<tcp::socket>(std::move(socket));

        _clientThreads.emplace_back([this, clientSocket] {
            try {
                char buf[5] = "ping";
                asio::read(*clientSocket, asio::buffer(buf, sizeof(buf)));

                ASSERT_EQ("ping"_sd, buf);

                memcpy(buf, "pong", sizeof(buf));

                asio::write(*clientSocket, asio::buffer(buf, sizeof(buf)));

                clientSocket->shutdown(asio::ip::tcp::socket::shutdown_both);
                clientSocket->close();
            } catch (...) {
                log() << "Failure on client thread: " << exceptionToStatus();
            }
        });
    }

    ~Server() {
        for (auto&& thread : _clientThreads) {
            thread.join();
        }
    }

    tcp::endpoint endpoint() {
        return _endpoint.get();
    }

private:
    stdx::mutex _mutex;
    std::vector<stdx::thread> _clientThreads;
    executor::Deferred<tcp::endpoint> _endpoint;
    tcp::acceptor _acceptor;
    tcp::socket _socket;
    stdx::condition_variable _condvar;
};

TEST(PollReactor, Basic) {
    asio::io_service io_service;
    stdx::thread clientWorker([&io_service] {
        asio::io_service::work work(io_service);
        io_service.run();
    });
    auto guard = MakeGuard([&clientWorker, &io_service] {
        io_service.stop();
        clientWorker.join();
    });
    Server server(io_service);

    const size_t kNumClients = 10;

    struct State {
        State(asio::io_service& io_service, asio::ip::tcp::resolver::iterator& endpoints)
            : strand(io_service), stream(&strand) {
            stream.connect(endpoints, [this](std::error_code ec) mutable { opened.emplace(!ec); });
        }

        asio::io_service::strand strand;
        executor::AsyncStream stream;
        executor::Deferred<bool> opened;
        char buf[5] = "ping";
    };

    tcp::resolver resolver{io_service};
    auto endpoints = resolver.resolve(server.endpoint());

    std::vector<std::unique_ptr<State>> clients;
    for (size_t i = 0; i < kNumClients; i++) {
        clients.emplace_back(stdx::make_unique<State>(io_service, endpoints));
    }

    executor::PollReactor pr;
    size_t done = 0;

    for (auto& client : clients) {
        ASSERT_TRUE(client->opened.get());
        pr.asyncWrite(&client->stream,
                      ConstDataRange(client->buf, client->buf + sizeof(client->buf)),
                      [&](Status status) {
                          ASSERT_OK(status);

                          pr.asyncRead(&client->stream,
                                       DataRange(client->buf, client->buf + sizeof(client->buf)),
                                       [&](Status status) {
                                           ASSERT_OK(status);

                                           ASSERT_EQ("pong"_sd, client->buf);

                                           done++;
                                       });
                      });
    }

    while (!pr.empty() && done != kNumClients) {
        pr.run();
    }

    ASSERT_EQ(done, kNumClients);
}

TEST(PollReactor, AddCancels) {
    executor::PollReactor pr;

    AtomicBool done(false);

    stdx::thread thread([&pr, &done] {
        for (int i = 0; i < 1000; i++) {
            auto timerId = pr.setTimer(Date_t::now() + Minutes(1), [] {});
            pr.cancelTimer(timerId);
            sleepmillis(1);
        }

        done.store(true);
    });

    int iterations = 0;
    while (!done.load()) {
        pr.run();
        iterations++;
    }

    ASSERT_GTE(iterations, 10);

    thread.join();
}

}  // namespace
}  // namespace mongo
