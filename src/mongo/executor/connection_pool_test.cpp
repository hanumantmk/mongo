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

#include "mongo/platform/basic.h"

#include <iostream>
#include <memory>
#include <utility>
#include <set>

#include "mongo/executor/connection_pool.h"
#include "mongo/unittest/unittest.h"
#include "mongo/stdx/memory.h"

namespace mongo {
namespace executor {
namespace {

class ConnectionImpl : public AbstractConnectionPoolConnection {
public:
    ConnectionImpl(const HostAndPort& hostAndPort, TimerHandle timer) {
        _hostAndPort = hostAndPort;
        _timer = std::move(timer);
    }

    void setup(setupCallback cb, stdx::chrono::milliseconds timeout) override {
        cb(this, Status::OK());
    }

    void refresh(refreshCallback cb, stdx::chrono::milliseconds timeout) override {
        cb(this, Status::OK());
    }
};

class TimerImpl : public ConnectionPoolTimerInterface {
public:
    void setTimeout(timeoutCallback cb, stdx::chrono::milliseconds timeout) override {
        _cb = std::move(cb);

        _timers.emplace(this);
    }

    void cancelTimeout() override {
        _timers.erase(this);
    }

    void timeout() {
        _cb();
    }

    static std::set<TimerImpl*> _timers;

private:
    timeoutCallback _cb;
};

std::set<TimerImpl*> TimerImpl::_timers;

class PoolImpl : public ConnectionPoolImplInterface {
public:
    ConnectionHandle makeConnection(const HostAndPort& hostAndPort) override {
        return stdx::make_unique<ConnectionImpl>(hostAndPort, makeTimer());
    }

    TimerHandle makeTimer() override {
        return stdx::make_unique<TimerImpl>();
    }

private:
};

class ConnectionPoolTest: public mongo::unittest::Test {
public:

private:
};

TEST_F(ConnectionPoolTest, Basic) {
    ConnectionPool pool(stdx::make_unique<PoolImpl>());

    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(5000),
                       [&](StatusWith<ConnectionHandle> swConn) {
                           ASSERT(swConn.isOK());
                       });
}

}  // namespace
}  // namespace executor
}  // namespace mongo
