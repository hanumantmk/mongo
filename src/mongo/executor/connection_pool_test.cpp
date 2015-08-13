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
namespace ConnectionPoolTestDetails {

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

    ConnectionPoolConnectionInterface* conn1 = nullptr;
    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(5000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());
                           conn1 = swConn.getValue();

                           pool.returnConnection(swConn.getValue());
                       });

    ConnectionPoolConnectionInterface* conn2 = nullptr;
    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(5000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());
                           conn2 = swConn.getValue();

                           pool.returnConnection(swConn.getValue());
                       });

    ASSERT_EQ(conn1, conn2);
}

TEST_F(ConnectionPoolTest, DifferentHostDifferentConn) {
    ConnectionPool pool(stdx::make_unique<PoolImpl>());

    ConnectionPoolConnectionInterface* conn1 = nullptr;
    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort("localhost:30000"),
                       stdx::chrono::milliseconds(5000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());
                           conn1 = swConn.getValue();

                           pool.returnConnection(swConn.getValue());
                       });

    ConnectionPoolConnectionInterface* conn2 = nullptr;
    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort("localhost:30001"),
                       stdx::chrono::milliseconds(5000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());
                           conn2 = swConn.getValue();

                           pool.returnConnection(swConn.getValue());
                       });

    ASSERT_NE(conn1, conn2);
}

TEST_F(ConnectionPoolTest, DifferentConn) {
    ConnectionPool pool(stdx::make_unique<PoolImpl>());

    ConnectionPoolConnectionInterface* conn1 = nullptr;
    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(5000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());
                           conn1 = swConn.getValue();
                       });

    ConnectionPoolConnectionInterface* conn2 = nullptr;
    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(5000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());
                           conn2 = swConn.getValue();
                       });

    ASSERT_NE(conn1, conn2);
}

TEST_F(ConnectionPoolTest, TimeoutOnSetup) {
    ConnectionPool pool(stdx::make_unique<PoolImpl>());

    bool notOk = false;

    auto now = stdx::chrono::steady_clock::now();

    PoolImpl::setNow(now);

    pool.getConnection(
        HostAndPort(),
        stdx::chrono::milliseconds(5000),
        [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) { notOk = !swConn.isOK(); });

    PoolImpl::setNow(now + stdx::chrono::milliseconds(5000));

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
    options.refreshRequirement = stdx::chrono::milliseconds(1000);
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    auto now = stdx::chrono::steady_clock::now();

    PoolImpl::setNow(now);

    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(5000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           pool.returnConnection(swConn.getValue());
                       });

    PoolImpl::setNow(now + stdx::chrono::milliseconds(1000));
    ASSERT(refreshedA);
    ASSERT(!refreshedB);

    PoolImpl::setNow(now + stdx::chrono::milliseconds(1500));
    ASSERT(!refreshedB);

    PoolImpl::setNow(now + stdx::chrono::milliseconds(2000));
    ASSERT(refreshedB);
}

TEST_F(ConnectionPoolTest, refreshTimeoutHappens) {
    ConnectionPool::Options options;
    options.refreshRequirement = stdx::chrono::milliseconds(1000);
    options.refreshTimeout = stdx::chrono::milliseconds(2000);
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    auto now = stdx::chrono::steady_clock::now();

    PoolImpl::setNow(now);

    ConnectionPoolConnectionInterface* conn = nullptr;

    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(5000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           conn = swConn.getValue();

                           pool.returnConnection(swConn.getValue());
                       });

    PoolImpl::setNow(now + stdx::chrono::milliseconds(500));

    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(5000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           ASSERT_EQ(swConn.getValue(), conn);

                           pool.returnConnection(swConn.getValue());
                       });

    PoolImpl::setNow(now + stdx::chrono::milliseconds(2000));
    bool reached = false;

    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(10000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           reached = true;

                           pool.returnConnection(swConn.getValue());
                       });
    ASSERT(!reached);

    PoolImpl::setNow(now + stdx::chrono::milliseconds(3000));

    ConnectionImpl::pushSetup(Status::OK());
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(1000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           ASSERT_NE(swConn.getValue(), conn);

                           pool.returnConnection(swConn.getValue());
                       });

    ASSERT(reached);
}

TEST_F(ConnectionPoolTest, requestsServedByUrgency) {
    ConnectionPool pool(stdx::make_unique<PoolImpl>());

    bool reachedA = false;
    bool reachedB = false;

    ConnectionPoolConnectionInterface* conn;

    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(2000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           reachedA = true;
                       });
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(1000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           reachedB = true;

                           conn = swConn.getValue();
                       });

    ConnectionImpl::pushSetup(Status::OK());

    ASSERT(reachedB);
    ASSERT(!reachedA);

    pool.returnConnection(conn);
    ASSERT(reachedA);
}

TEST_F(ConnectionPoolTest, maxPoolRespected) {
    ConnectionPool::Options options;
    options.minConnections = 1;
    options.maxConnections = 2;
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    ConnectionPoolConnectionInterface* conn1 = nullptr;
    ConnectionPoolConnectionInterface* conn2 = nullptr;
    ConnectionPoolConnectionInterface* conn3 = nullptr;

    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(3000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           conn3 = swConn.getValue();
                       });
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(2000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           conn2 = swConn.getValue();
                       });
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(1000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           conn1 = swConn.getValue();
                       });

    ConnectionImpl::pushSetup(Status::OK());
    ConnectionImpl::pushSetup(Status::OK());
    ConnectionImpl::pushSetup(Status::OK());

    ASSERT(conn1);
    ASSERT(conn2);
    ASSERT(!conn3);

    pool.returnConnection(conn1);

    ASSERT_EQ(conn1, conn3);
}

TEST_F(ConnectionPoolTest, minPoolRespected) {
    ConnectionPool::Options options;
    options.minConnections = 2;
    options.maxConnections = 3;
    options.refreshRequirement = stdx::chrono::milliseconds(1000);
    options.refreshTimeout = stdx::chrono::milliseconds(2000);
    ConnectionPool pool(stdx::make_unique<PoolImpl>(), options);

    auto now = stdx::chrono::steady_clock::now();

    PoolImpl::setNow(now);

    ConnectionPoolConnectionInterface* conn1 = nullptr;
    ConnectionPoolConnectionInterface* conn2 = nullptr;
    ConnectionPoolConnectionInterface* conn3 = nullptr;

    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(1000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           conn1 = swConn.getValue();
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

    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(2000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           conn2 = swConn.getValue();
                       });
    pool.getConnection(HostAndPort(),
                       stdx::chrono::milliseconds(3000),
                       [&](StatusWith<ConnectionPoolConnectionInterface*> swConn) {
                           ASSERT(swConn.isOK());

                           conn3 = swConn.getValue();
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

    PoolImpl::setNow(now + stdx::chrono::milliseconds(1));
    conn1->indicateUsed();
    pool.returnConnection(conn1);

    PoolImpl::setNow(now + stdx::chrono::milliseconds(2));
    conn2->indicateUsed();
    pool.returnConnection(conn2);

    PoolImpl::setNow(now + stdx::chrono::milliseconds(3));
    conn3->indicateUsed();
    pool.returnConnection(conn3);

    PoolImpl::setNow(now + stdx::chrono::milliseconds(5000));

    ASSERT(reachedA);
    ASSERT(reachedB);
    ASSERT(!reachedC);
}

}  // namespace ConnectionPoolTestDetails
}  // namespace executor
}  // namespace mongo
