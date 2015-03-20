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

#include "mongo/base/data_builder.h"

#include "mongo/platform/endian.h"
#include "mongo/unittest/unittest.h"

namespace mongo {

    TEST(DataBuilder, Basic) {
        DataBuilder db(1);

        ASSERT_EQUALS(true, db.writeNativeAndAdvance<uint16_t>(1).isOK());
        ASSERT_EQUALS(true, db.writeLEAndAdvance<uint32_t>(2).isOK());
        ASSERT_EQUALS(true, db.writeBEAndAdvance<uint64_t>(3).isOK());

        ASSERT_EQUALS(16u, db.reserved());
        ASSERT_EQUALS(14u, db.size());

        db.resize(14u);
        ASSERT_EQUALS(14u, db.reserved());
        ASSERT_EQUALS(14u, db.size());

        db.reserve(2u);
        ASSERT_EQUALS(28u, db.reserved());
        ASSERT_EQUALS(14u, db.size());

        ConstDataRangeCursor cdrc = db.data_range_cursor();

        ASSERT_EQUALS(static_cast<uint16_t>(1), cdrc.readNativeAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(static_cast<uint32_t>(2), cdrc.readLEAndAdvance<uint32_t>().getValue());
        ASSERT_EQUALS(static_cast<uint64_t>(3), cdrc.readBEAndAdvance<uint64_t>().getValue());
        ASSERT_EQUALS(false, cdrc.readNativeAndAdvance<char>().isOK());
    }

    TEST(DataBuilder, ResizeDown) {
        DataBuilder db(1);

        ASSERT_EQUALS(true, db.writeNativeAndAdvance<uint16_t>(1).isOK());
        ASSERT_EQUALS(true, db.writeNativeAndAdvance<uint64_t>(2).isOK());

        db.resize(2u);
        ASSERT_EQUALS(2u, db.reserved());
        ASSERT_EQUALS(2u, db.size());

        ConstDataRangeCursor cdrc = db.data_range_cursor();

        ASSERT_EQUALS(static_cast<uint16_t>(1), cdrc.readNativeAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(false, cdrc.readNativeAndAdvance<char>().isOK());
    }

    TEST(DataBuilder, Clear) {
        DataBuilder db(1);

        ASSERT_EQUALS(true, db.writeNativeAndAdvance<uint16_t>(1).isOK());

        db.clear();
        ASSERT_EQUALS(2u, db.reserved());
        ASSERT_EQUALS(0u, db.size());

        ConstDataRangeCursor cdrc = db.data_range_cursor();
        ASSERT_EQUALS(false, cdrc.readNativeAndAdvance<char>().isOK());
    }

    TEST(DataBuilder, Move) {
        DataBuilder db(1);

        ASSERT_EQUALS(true, db.writeNativeAndAdvance<uint16_t>(1).isOK());

        auto db2 = DataBuilder(std::move(db));

        ConstDataRangeCursor cdrc = db2.data_range_cursor();

        ASSERT_EQUALS(static_cast<uint16_t>(1), cdrc.readNativeAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(2u, db2.reserved());
        ASSERT_EQUALS(2u, db2.size());

        ASSERT_EQUALS(0u, db.reserved());
        ASSERT_EQUALS(0u, db.size());
        ASSERT(!db.data());
    }

    TEST(DataBuilder, Copy) {
        DataBuilder db(1);

        ASSERT_EQUALS(true, db.writeNativeAndAdvance<uint16_t>(1).isOK());

        auto db2 = db;

        ConstDataRangeCursor cdrc1 = db.data_range_cursor();
        ConstDataRangeCursor cdrc2 = db2.data_range_cursor();

        ASSERT_EQUALS(static_cast<uint16_t>(1), cdrc1.readNativeAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(2u, db.reserved());
        ASSERT_EQUALS(2u, db.size());

        ASSERT_EQUALS(static_cast<uint16_t>(1), cdrc2.readNativeAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(2u, db2.reserved());
        ASSERT_EQUALS(2u, db2.size());

        db2.clear();

        ASSERT_EQUALS(true, db2.writeNativeAndAdvance<uint16_t>(2).isOK());
        ASSERT_EQUALS(true, db2.writeNativeAndAdvance<uint16_t>(3).isOK());

        cdrc1 = db.data_range_cursor();
        cdrc2 = db2.data_range_cursor();

        ASSERT_EQUALS(static_cast<uint16_t>(1), cdrc1.readNativeAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(2u, db.reserved());
        ASSERT_EQUALS(2u, db.size());


        ASSERT_EQUALS(static_cast<uint16_t>(2), cdrc2.readNativeAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(static_cast<uint16_t>(3), cdrc2.readNativeAndAdvance<uint16_t>().getValue());
        ASSERT_EQUALS(4u, db2.reserved());
        ASSERT_EQUALS(4u, db2.size());
    }

} // namespace mongo
