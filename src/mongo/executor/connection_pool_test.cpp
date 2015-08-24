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

#include "mongo/executor/connection_pool_test_fixture.h"

#include "mongo/executor/connection_pool.h"
#include "mongo/unittest/unittest.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/future.h"

namespace mongo {
namespace executor {
namespace connection_pool_test_details {

class ConnectionPoolTest : public unittest::Test {
public:
protected:
    void setUp() override {}

    void tearDown() override {
        ConnectionImpl::clear();
        TimerImpl::clear();
    }

private:
};

TEST_F(ConnectionPoolTest, SameConn) {
    ConnectionPool pool(stdx::make_unique<PoolImpl>());

    ConnectionPool::ConnectionInterface* conn1 = nullptr;
    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());
                 conn1 = swConn.getValue().get();
             });

    ConnectionPool::ConnectionInterface* conn2 = nullptr;
    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());
                 conn2 = swConn.getValue().get();
             });

    ASSERT_EQ(conn1, conn2);
}

TEST_F(ConnectionPoolTest, DifferentHostDifferentConn) {
    ConnectionPool pool(stdx::make_unique<PoolImpl>());

    ConnectionPool::ConnectionInterface* conn1 = nullptr;
    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort("localhost:30000"),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());
                 conn1 = swConn.getValue().get();
             });

    ConnectionPool::ConnectionInterface* conn2 = nullptr;
    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort("localhost:30001"),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());
                 conn2 = swConn.getValue().get();
             });

    ASSERT_NE(conn1, conn2);
}

TEST_F(ConnectionPoolTest, DifferentConn) {
    ConnectionPool pool(stdx::make_unique<PoolImpl>());

    ConnectionPool::ConnectionHandle conn1;
    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());
                 conn1 = std::move(swConn.getValue());
             });

    ConnectionPool::ConnectionHandle conn2;
    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());
                 conn2 = std::move(swConn.getValue());
             });

    ASSERT_NE(conn1.get(), conn2.get());
}

TEST_F(ConnectionPoolTest, TimeoutOnSetup) {
    ConnectionPool pool(stdx::make_unique<PoolImpl>());

    bool notOk = false;

    auto now = Date_t::now();

    PoolImpl::setNow(now);

    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) { notOk = !swConn.isOK(); });

    PoolImpl::setNow(now + Milliseconds(5000));

    ASSERT(notOk);
}

TEST_F(ConnectionPoolTest, refreshHappens) {
    bool refreshedA = false;
    bool refreshedB = false;
    ConnectionImpl::pushRefresh([&]() {
        refreshedA = true;
        return Status::OK();
    });

    ConnectionImpl::pushRefresh([&]() {
        refreshedB = true;
        return Status::OK();
    });

    ConnectionPool::Options options;
    options.refreshRequirement = Milliseconds(1000);
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    auto now = Date_t::now();

    PoolImpl::setNow(now);

    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) { ASSERT(swConn.isOK()); });

    PoolImpl::setNow(now + Milliseconds(1000));
    ASSERT(refreshedA);
    ASSERT(!refreshedB);

    PoolImpl::setNow(now + Milliseconds(1500));
    ASSERT(!refreshedB);

    PoolImpl::setNow(now + Milliseconds(2000));
    ASSERT(refreshedB);
}

TEST_F(ConnectionPoolTest, refreshTimeoutHappens) {
    ConnectionPool::Options options;
    options.refreshRequirement = Milliseconds(1000);
    options.refreshTimeout = Milliseconds(2000);
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    auto now = Date_t::now();

    PoolImpl::setNow(now);

    ConnectionPool::ConnectionInterface* conn = nullptr;

    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn = swConn.getValue().get();
             });

    PoolImpl::setNow(now + Milliseconds(500));

    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 ASSERT_EQ(swConn.getValue().get(), conn);
             });

    PoolImpl::setNow(now + Milliseconds(2000));
    bool reached = false;

    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(10000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 reached = true;
             });
    ASSERT(!reached);

    PoolImpl::setNow(now + Milliseconds(3000));

    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(1000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 ASSERT_NE(swConn.getValue().get(), conn);
             });

    ASSERT(reached);
}

TEST_F(ConnectionPoolTest, requestsServedByUrgency) {
    ConnectionPool pool(stdx::make_unique<PoolImpl>());

    bool reachedA = false;
    bool reachedB = false;

    ConnectionPool::ConnectionHandle conn;

    pool.get(HostAndPort(),
             Milliseconds(2000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 reachedA = true;
             });

    pool.get(HostAndPort(),
             Milliseconds(1000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 reachedB = true;

                 conn = std::move(swConn.getValue());
             });

    ConnectionImpl::pushSetup(Status::OK());

    ASSERT(reachedB);
    ASSERT(!reachedA);

    conn.reset();

    ASSERT(reachedA);
}

TEST_F(ConnectionPoolTest, maxPoolRespected) {
    ConnectionPool::Options options;
    options.minConnections = 1;
    options.maxConnections = 2;
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    ConnectionPool::ConnectionHandle conn1;
    ConnectionPool::ConnectionHandle conn2;
    ConnectionPool::ConnectionHandle conn3;

    pool.get(HostAndPort(),
             Milliseconds(3000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn3 = std::move(swConn.getValue());
             });
    pool.get(HostAndPort(),
             Milliseconds(2000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn2 = std::move(swConn.getValue());
             });
    pool.get(HostAndPort(),
             Milliseconds(1000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn1 = std::move(swConn.getValue());
             });

    ConnectionImpl::pushSetup(Status::OK());
    ConnectionImpl::pushSetup(Status::OK());
    ConnectionImpl::pushSetup(Status::OK());

    ASSERT(conn1);
    ASSERT(conn2);
    ASSERT(!conn3);

    ConnectionPool::ConnectionInterface* conn1Ptr = conn1.get();
    conn1.reset();

    ASSERT_EQ(conn1Ptr, conn3.get());
}

TEST_F(ConnectionPoolTest, minPoolRespected) {
    ConnectionPool::Options options;
    options.minConnections = 2;
    options.maxConnections = 3;
    options.refreshRequirement = Milliseconds(1000);
    options.refreshTimeout = Milliseconds(2000);
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    auto now = Date_t::now();

    PoolImpl::setNow(now);

    ConnectionPool::ConnectionHandle conn1;
    ConnectionPool::ConnectionHandle conn2;
    ConnectionPool::ConnectionHandle conn3;

    pool.get(HostAndPort(),
             Milliseconds(1000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn1 = std::move(swConn.getValue());
             });

    bool reachedA = false;
    bool reachedB = false;
    bool reachedC = false;

    ConnectionImpl::pushSetup([&]() {
        reachedA = true;
        return Status::OK();
    });
    ConnectionImpl::pushSetup([&]() {
        reachedB = true;
        return Status::OK();
    });
    ConnectionImpl::pushSetup([&]() {
        reachedC = true;
        return Status::OK();
    });

    ASSERT(reachedA);
    ASSERT(reachedB);
    ASSERT(!reachedC);

    pool.get(HostAndPort(),
             Milliseconds(2000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn2 = std::move(swConn.getValue());
             });
    pool.get(HostAndPort(),
             Milliseconds(3000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn3 = std::move(swConn.getValue());
             });

    reachedA = false;
    reachedB = false;
    reachedC = false;

    ConnectionImpl::pushRefresh([&]() {
        reachedA = true;
        return Status::OK();
    });
    ConnectionImpl::pushRefresh([&]() {
        reachedB = true;
        return Status::OK();
    });
    ConnectionImpl::pushRefresh([&]() {
        reachedC = true;
        return Status::OK();
    });

    PoolImpl::setNow(now + Milliseconds(1));
    conn1->indicateUsed();
    conn1.reset();

    PoolImpl::setNow(now + Milliseconds(2));
    conn2->indicateUsed();
    conn2.reset();

    PoolImpl::setNow(now + Milliseconds(3));
    conn3->indicateUsed();
    conn3.reset();

    PoolImpl::setNow(now + Milliseconds(5000));

    ASSERT(reachedA);
    ASSERT(reachedB);
    ASSERT(!reachedC);
}

TEST_F(ConnectionPoolTest, hostTimeoutHappens) {
    ConnectionPool::Options options;
    options.refreshRequirement = Milliseconds(5000);
    options.refreshTimeout = Milliseconds(5000);
    options.hostTimeout = Milliseconds(1000);
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    auto now = Date_t::now();

    PoolImpl::setNow(now);

    ConnectionPool::ConnectionInterface* conn = nullptr;

    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn = swConn.getValue().get();
             });

    PoolImpl::setNow(now + Milliseconds(1000));

    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 ASSERT_NE(conn, swConn.getValue().get());
             });
}

TEST_F(ConnectionPoolTest, hostTimeoutHappensMoreGetsDelay) {
    ConnectionPool::Options options;
    options.refreshRequirement = Milliseconds(5000);
    options.refreshTimeout = Milliseconds(5000);
    options.hostTimeout = Milliseconds(1000);
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    auto now = Date_t::now();

    PoolImpl::setNow(now);

    ConnectionPool::ConnectionInterface* conn = nullptr;

    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn = swConn.getValue().get();
             });

    PoolImpl::setNow(now + Milliseconds(999));

    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 ASSERT_EQ(conn, swConn.getValue().get());
             });

    PoolImpl::setNow(now + Milliseconds(2000));

    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 ASSERT_NE(conn, swConn.getValue().get());
             });
}

TEST_F(ConnectionPoolTest, hostTimeoutHappensCheckoutDelays) {
    ConnectionPool::Options options;
    options.refreshRequirement = Milliseconds(5000);
    options.refreshTimeout = Milliseconds(5000);
    options.hostTimeout = Milliseconds(1000);
    options.minConnections = 1;
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    auto now = Date_t::now();

    PoolImpl::setNow(now);

    ConnectionPool::ConnectionHandle conn1;
    ConnectionPool::ConnectionInterface* conn2Ptr = nullptr;

    ConnectionImpl::pushSetup(Status::OK());
    ConnectionImpl::pushSetup(Status::OK());
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn1 = std::move(swConn.getValue());
             });
    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 conn2Ptr = swConn.getValue().get();
             });

    PoolImpl::setNow(now + Milliseconds(1000));

    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 ASSERT_EQ(conn2Ptr, swConn.getValue().get());
             });

    conn1.reset();

    PoolImpl::setNow(now + Milliseconds(2000));

    pool.get(HostAndPort(),
             Milliseconds(5000),
             [&](StatusWith<ConnectionPool::ConnectionHandle> swConn) {
                 ASSERT(swConn.isOK());

                 ASSERT_NE(conn2Ptr, swConn.getValue().get());
             });
}

}  // namespace connection_pool_test_details
}  // namespace executor
}  // namespace mongo
