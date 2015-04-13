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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/base/data_range_cursor.h"

#include "mongo/base/data_type_collection.h"
#include "mongo/base/data_type_endian.h"
#include "mongo/base/data_type_string_data.h"
#include "mongo/base/data_type_terminated.h"
#include "mongo/base/data_type_tuple.h"
#include "mongo/base/data_type_packet.h"
#include "mongo/platform/endian.h"
#include "mongo/unittest/unittest.h"

namespace mongo {

    TEST(DataRangeCursor, ConstDataRangeCursor) {
        char buf[14];

        DataView(buf).write<uint16_t>(1);
        DataView(buf).write<LittleEndian<uint32_t>>(2, sizeof(uint16_t));
        DataView(buf).write<BigEndian<uint64_t>>(3, sizeof(uint16_t) + sizeof(uint32_t));

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));
        ConstDataRangeCursor backup(cdrc);

        ASSERT_EQUALS(static_cast<uint16_t>(1), cdrc.readAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(static_cast<uint32_t>(2),
                      cdrc.readAndAdvance<LittleEndian<uint32_t>>().getValue());
        ASSERT_EQUALS(static_cast<uint64_t>(3),
                      cdrc.readAndAdvance<BigEndian<uint64_t>>().getValue());
        ASSERT_EQUALS(false, cdrc.readAndAdvance<char>().isOK());

        // test skip()
        cdrc = backup;
        ASSERT_EQUALS(true, cdrc.skip<uint32_t>().isOK());;
        ASSERT_EQUALS(buf + sizeof(uint32_t), cdrc.view().getValue());
        ASSERT_EQUALS(true, cdrc.advance(10).isOK());
        ASSERT_EQUALS(false, cdrc.readAndAdvance<char>().isOK());
    }

    TEST(DataRangeCursor, DataRangeCursor) {
        char buf[100] = { 0 };

        DataRangeCursor dc(buf, buf + 14);

        ASSERT_EQUALS(true, dc.writeAndAdvance<uint16_t>(1).isOK());
        ASSERT_EQUALS(true, dc.writeAndAdvance<LittleEndian<uint32_t>>(2).isOK());
        ASSERT_EQUALS(true, dc.writeAndAdvance<BigEndian<uint64_t>>(3).isOK());
        ASSERT_EQUALS(false, dc.writeAndAdvance<char>(1).isOK());

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));

        ASSERT_EQUALS(static_cast<uint16_t>(1), cdrc.readAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(static_cast<uint32_t>(2),
                      cdrc.readAndAdvance<LittleEndian<uint32_t>>().getValue());
        ASSERT_EQUALS(static_cast<uint64_t>(3),
                      cdrc.readAndAdvance<BigEndian<uint64_t>>().getValue());
        ASSERT_EQUALS(static_cast<char>(0), cdrc.readAndAdvance<char>().getValue());
    }

    TEST(DataRangeCursor, DataTypeTupleLoad) {
        char buf[sizeof(uint32_t) * 3];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        ASSERT_EQUALS(true, drc.writeAndAdvance(uint32_t(1)).isOK());
        ASSERT_EQUALS(true, drc.writeAndAdvance(uint32_t(2)).isOK());
        ASSERT_EQUALS(true, drc.writeAndAdvance(uint32_t(3)).isOK());

        uint32_t a = 0;
        uint32_t b = 0;
        uint32_t c = 0;

        auto out = std::tie(a, b, c);

        ConstDataRange cdr(buf, buf + sizeof(buf));

        ASSERT_EQUALS(true, cdr.read(&out).isOK());

        ASSERT_EQUALS(1u, a);
        ASSERT_EQUALS(2u, b);
        ASSERT_EQUALS(3u, c);
    }

    TEST(DataRangeCursor, DataTypeTupleStore) {
        char buf[sizeof(uint32_t) * 3];

        uint32_t a = 1;
        uint32_t b = 2;
        uint32_t c = 3;
        auto in = std::tie(a, b, c);

        DataRange dr(buf, buf + sizeof(buf));

        ASSERT_EQUALS(true, dr.write(in).isOK());

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));

        auto sw = cdrc.readAndAdvance<uint32_t>();
        ASSERT_EQUALS(true, sw.isOK());
        ASSERT_EQUALS(1u, sw.getValue());

        sw = cdrc.readAndAdvance<uint32_t>();
        ASSERT_EQUALS(true, sw.isOK());
        ASSERT_EQUALS(2u, sw.getValue());

        sw = cdrc.readAndAdvance<uint32_t>();
        ASSERT_EQUALS(true, sw.isOK());
        ASSERT_EQUALS(3u, sw.getValue());
    }

    TEST(DataRangeCursor, DataTypePacketLoad) {
        char buf[sizeof(uint32_t) * 3];
        char message[] = "foo";

        DataRangeCursor drc(buf, buf + sizeof(buf));

        ASSERT_EQUALS(true, drc.writeAndAdvance(BigEndian<uint32_t>(sizeof(message))).isOK());
        ASSERT_EQUALS(true, drc.writeAndAdvance(
            DataRange(message, message + sizeof(message))).isOK());

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));

        auto out = cdrc.readAndAdvance<Packet<BigEndian<uint32_t>, ConstDataRange>>();

        ASSERT_EQUALS(true, out.isOK());
        ASSERT_EQUALS(sizeof(message), out.getValue().length);
        ASSERT_EQUALS(std::string(message), out.getValue().t.data());
        ASSERT_EQUALS(cdrc.data(), buf + sizeof(uint32_t) + sizeof(message));
    }

    TEST(DataRangeCursor, DataTypePacketStore) {
        char buf[sizeof(uint32_t) * 3];
        char message[] = "foo";

        DataRangeCursor drc(buf, buf + sizeof(buf));

        ASSERT_EQUALS(true, drc.writeAndAdvance(Packet<BigEndian<uint32_t>, DataRange>(
            DataRange(message, message + sizeof(message)))).isOK());

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));

        auto out = cdrc.readAndAdvance<Packet<BigEndian<uint32_t>, ConstDataRange>>();

        ASSERT_EQUALS(true, out.isOK());
        ASSERT_EQUALS(sizeof(message), out.getValue().length);
        ASSERT_EQUALS(std::string(message), out.getValue().t.data());
        ASSERT_EQUALS(cdrc.data(), buf + sizeof(uint32_t) + sizeof(message));
    }

    TEST(DataRangeCursor, DataTypeCount) {
        char buf[100] = { 0 };
        char message_foo[] = "foo";
        char message_bar[] = " bar";
        char message_baz[] = "  baz";

        DataRangeCursor drc(buf, buf + sizeof(buf));

        std::vector<Packet<uint8_t, ConstDataRange>> out_vec;
        out_vec.emplace_back(DataRange(message_foo, message_foo + sizeof(message_foo)));
        out_vec.emplace_back(DataRange(message_bar, message_bar + sizeof(message_bar)));
        out_vec.emplace_back(DataRange(message_baz, message_baz + sizeof(message_baz)));

        ASSERT_EQUALS(true, drc.writeAndAdvance(
            Count<uint32_t, decltype(out_vec)>(out_vec)).isOK());

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));
        std::vector<Packet<uint8_t, ConstDataRange>> in_vec;
        Count<uint32_t, decltype(in_vec)> in_count(&in_vec);

        ASSERT_EQUALS(true, cdrc.readAndAdvance(&in_count).isOK());
        ASSERT_EQUALS(std::string(message_foo), in_vec[0].t.data());
        ASSERT_EQUALS(std::string(message_bar), in_vec[1].t.data());
        ASSERT_EQUALS(std::string(message_baz), in_vec[2].t.data());
        ASSERT_EQUALS(3u, in_count.count);
        ASSERT_EQUALS(3u, in_vec.size());
    }

    TEST(DataRangeCursor, DataTypeConsume) {
        char buf[100] = { 0 };
        char message_foo[] = "foo";
        char message_bar[] = " bar";
        char message_baz[] = "  baz";

        DataRangeCursor drc(buf, buf + sizeof(buf));

        std::vector<Packet<uint8_t, ConstDataRange>> out_vec;
        out_vec.emplace_back(DataRange(message_foo, message_foo + sizeof(message_foo)));
        out_vec.emplace_back(DataRange(message_bar, message_bar + sizeof(message_bar)));
        out_vec.emplace_back(DataRange(message_baz, message_baz + sizeof(message_baz)));

        Consume<decltype(out_vec)> consume_vec(out_vec);

        ASSERT_EQUALS(true, drc.writeAndAdvance(
            Packet<uint32_t, decltype(consume_vec)>(consume_vec)).isOK());

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));
        std::vector<Packet<uint8_t, ConstDataRange>> in_vec;
        Consume<decltype(in_vec)> in_consume(&in_vec);
        Packet<uint32_t, decltype(in_consume)> in_consume_pack(in_consume);

        ASSERT_OK(cdrc.readAndAdvance(&in_consume_pack));
        ASSERT_EQUALS(std::string(message_foo), in_vec[0].t.data());
        ASSERT_EQUALS(std::string(message_bar), in_vec[1].t.data());
        ASSERT_EQUALS(std::string(message_baz), in_vec[2].t.data());
        ASSERT_EQUALS(3u, in_vec.size());
    }

    TEST(DataRangeCursor, DataTypeTerminated) {
        char buf[100] = { 0 };
        StringData foo("foo");
        StringData bar(" bar");
        StringData baz("  baz");

        DataRangeCursor drc(buf, buf + sizeof(buf));

        ASSERT_OK(drc.writeAndAdvance(Terminated<'\0', StringData>(foo)));
        ASSERT_OK(drc.writeAndAdvance(Terminated<'\0', StringData>(bar)));
        ASSERT_OK(drc.writeAndAdvance(Terminated<'\0', StringData>(baz)));
        ASSERT_EQUALS(drc.data(), buf + 4 + 5 + 6);

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));

        auto sw_foo = cdrc.readAndAdvance<Terminated<'\0', StringData>>();
        ASSERT_EQUALS(foo, sw_foo.getValue().t);

        auto sw_bar = cdrc.readAndAdvance<Terminated<'\0', ConstDataRange>>();
        ASSERT_EQUALS(bar, sw_bar.getValue().t.data());
        ASSERT_EQUALS(bar.size(), sw_bar.getValue().t.length());

        auto sw_baz = cdrc.readAndAdvance<Terminated<'\0', StringData>>();
        ASSERT_EQUALS(baz, sw_baz.getValue().t);

        ASSERT_EQUALS(cdrc.data(), buf + 4 + 5 + 6);
    }

} // namespace mongo
