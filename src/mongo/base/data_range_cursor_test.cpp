/**
 *    Copyright (C) 2014 MongoDB Inc.
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

#include <cstring>

#include "mongo/base/data_type_endian.h"
#include "mongo/base/data_type_sized.h"
#include "mongo/base/data_type_terminated.h"
#include "mongo/base/data_type_tuple.h"

#include "mongo/platform/endian.h"
#include "mongo/unittest/unittest.h"

namespace mongo {

    TEST(DataRangeCursor, ConstDataRangeCursor) {
        char buf[14];

        DataView(buf).writeNative<uint16_t>(1);
        DataView(buf).writeLE<uint32_t>(2, sizeof(uint16_t));
        DataView(buf).writeBE<uint64_t>(3, sizeof(uint16_t) + sizeof(uint32_t));

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));
        ConstDataRangeCursor backup(cdrc);

        ASSERT_EQUALS(static_cast<uint16_t>(1), cdrc.readNativeAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(static_cast<uint32_t>(2), cdrc.readLEAndAdvance<uint32_t>().getValue());
        ASSERT_EQUALS(static_cast<uint64_t>(3), cdrc.readBEAndAdvance<uint64_t>().getValue());
        ASSERT_EQUALS(false, cdrc.readNativeAndAdvance<char>().isOK());

        // test skip()
        cdrc = backup;
        ASSERT_EQUALS(true, cdrc.skip<uint32_t>().isOK());;
        ASSERT_EQUALS(buf + sizeof(uint32_t), cdrc.view().getValue());
        ASSERT_EQUALS(true, cdrc.advance(10).isOK());
        ASSERT_EQUALS(false, cdrc.readNativeAndAdvance<char>().isOK());

        cdrc = backup;
        auto x = cdrc.readNativeAndAdvance<std::tuple<uint16_t, LittleEndian<uint32_t>, BigEndian<uint64_t>>>();
        ASSERT_EQUALS(true, x.isOK());
        ASSERT_EQUALS(1u, std::get<0>(x.getValue()));
        ASSERT_EQUALS(2u, std::get<1>(x.getValue()).value);
        ASSERT_EQUALS(3u, std::get<2>(x.getValue()).value);

        uint16_t first;
        LittleEndian<uint32_t> second;
        BigEndian<uint64_t> third;

        auto y = std::tie(first, second, third);

        cdrc = backup;
        auto z = cdrc.readNativeAndAdvance(&y);

        ASSERT_EQUALS(true, z.isOK());

        ASSERT_EQUALS(1u, first);
        ASSERT_EQUALS(2u, second.value);
        ASSERT_EQUALS(3u, third.value);
    }

    TEST(DataRangeCursor, DataRangeCursor) {
        char buf[100] = { 0 };

        DataRangeCursor dc(buf, buf + 14);

        ASSERT_EQUALS(true, dc.writeNativeAndAdvance<uint16_t>(1).isOK());
        ASSERT_EQUALS(true, dc.writeLEAndAdvance<uint32_t>(2).isOK());
        ASSERT_EQUALS(true, dc.writeBEAndAdvance<uint64_t>(3).isOK());
        ASSERT_EQUALS(false, dc.writeNativeAndAdvance<char>(1).isOK());

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));
        ConstDataRangeCursor read_backup = cdrc;

        ASSERT_EQUALS(static_cast<uint16_t>(1), cdrc.readNativeAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(static_cast<uint32_t>(2), cdrc.readLEAndAdvance<uint32_t>().getValue());
        ASSERT_EQUALS(static_cast<uint64_t>(3), cdrc.readBEAndAdvance<uint64_t>().getValue());
        ASSERT_EQUALS(static_cast<char>(0), cdrc.readNativeAndAdvance<char>().getValue());

        dc = DataRangeCursor(buf, buf + 20);
        cdrc = read_backup;

        std::memset(buf, 0, sizeof(buf));

        auto x = dc.writeNativeAndAdvance(std::make_tuple(uint16_t{1u}, Terminated<'\0'>{"foo", 3}, LittleEndian<uint32_t>{2u}, Sized<2>{"XX"}, BigEndian<uint64_t>{3u}));
        ASSERT_EQUALS(true, x.isOK());

        uint16_t first;
        Terminated<'\0'> second;
        LittleEndian<uint32_t> third;
        Sized<2> fourth;
        BigEndian<uint64_t> fifth;

        auto helper = std::tie(second, third, fourth, fifth);
        auto out = std::tie(first, helper);

        x = cdrc.readNativeAndAdvance(&out);

        ASSERT_EQUALS(true, x.isOK());

        ASSERT_EQUALS(1u, first);
        ASSERT_EQUALS(3u, second.len);
        ASSERT_EQUALS(0, std::memcmp("foo", second.ptr, 3));
        ASSERT_EQUALS(2u, third.value);
        ASSERT_EQUALS(0, std::memcmp("XX", fourth.ptr, 2));
        ASSERT_EQUALS(3u, fifth.value);

        ASSERT_EQUALS(false, dc.writeNativeAndAdvance(uint8_t{1u}).isOK());
    }

} // namespace mongo
