/**
 * Copyright (C) 2017 MongoDB Inc.
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

#include "mongo/platform/basic.h"

#include "mongo/base/data_builder.h"
#include "mongo/base/data_view.h"
#include "mongo/base/data_range_cursor.h"
#include "mongo/db/jsobj.h"
#include "mongo/transport/transport_layer_inproc.h"
#include "mongo/transport/service_entry_point_mock.h"
#include "mongo/unittest/bson_test_util.h"
#include "mongo/unittest/unittest.h"

namespace mongo {

TEST(IngressHeaderTest, TestHeaders) {
    ServiceEntryPointMock mock;

    transport::TransportLayerInProc tlip(transport::TransportLayerInProc::Options{}, &mock);

    mock.setTL(&tlip);

    tlip.setup();
    tlip.start();

    auto conn = tlip.connectInProcClient();

    DataBuilder b;
    b.writeAndAdvance<int32_t>(0);
    b.writeAndAdvance<int32_t>(0);
    b.writeAndAdvance<int32_t>(0);
    b.writeAndAdvance<int32_t>(2004);
    b.writeAndAdvance<int32_t>(0);
    b.writeAndAdvance("admin.command"_sd);
    b.writeAndAdvance<int32_t>(0);
    b.writeAndAdvance<int32_t>(1);
    b.writeAndAdvance(BSON("isMaster" << 1));
    b.getCursor().write<int32_t>(b.size());

    ASSERT(conn->clientSend(b.getCursor().data(), b.size(), stdx::chrono::seconds(10)));

    char buf[10000];
    ASSERT(conn->clientRecv(buf, 4, stdx::chrono::seconds(10)));
    ASSERT(conn->clientRecv(buf + 4, DataView(buf).read<int32_t>() - 4, stdx::chrono::seconds(10)));

    auto len = DataView(buf).read<int32_t>();
    DataRangeCursor cursor(buf, buf + len);
    cursor.skip<int32_t>();
    cursor.skip<int32_t>();
    cursor.skip<int32_t>();
    cursor.skip<int32_t>();
    auto obj = cursor.readAndAdvance<BSONObj>().getValue();

    ASSERT_BSONOBJ_EQ(obj, BSON("ok" << 1));

    conn->close();
}

}  // namespace
