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

#include "mongo/base/data_range.h"

#include <cstring>

#include "mongo/platform/endian.h"
#include "mongo/unittest/unittest.h"

namespace mongo {

    TEST(DataRange, ConstDataRange) {
        char buf[sizeof(uint32_t) * 3];
        uint32_t native = 1234;
        uint32_t le = endian::nativeToLittle(native);
        uint32_t be = endian::nativeToBig(native);

        std::memcpy(buf, &native, sizeof(uint32_t));
        std::memcpy(buf + sizeof(uint32_t), &le, sizeof(uint32_t));
        std::memcpy(buf + sizeof(uint32_t) * 2, &be, sizeof(uint32_t));

        ConstDataRange cdv(buf, buf + sizeof(buf));

        ASSERT_EQUALS(buf, cdv.view().getValue());
        ASSERT_EQUALS(buf + 5, cdv.view(5).getValue());
        ASSERT_EQUALS(false, cdv.view(50).isOK());

        ASSERT_EQUALS(native, cdv.readNative<uint32_t>().getValue());
        ASSERT_EQUALS(native, cdv.readLE<uint32_t>(sizeof(uint32_t)).getValue());
        ASSERT_EQUALS(native, cdv.readBE<uint32_t>(sizeof(uint32_t) * 2).getValue());

        ASSERT_EQUALS(false, cdv.readNative<uint32_t>(sizeof(uint32_t) * 3).isOK());
    }

    TEST(DataRange, DataRange) {
        char buf[sizeof(uint32_t) * 3];
        uint32_t native = 1234;

        DataRange dv(buf, buf + sizeof(buf));

        ASSERT_EQUALS(true, dv.writeNative(native).isOK());
        ASSERT_EQUALS(true, dv.writeLE(native, sizeof(uint32_t)).isOK());
        ASSERT_EQUALS(true, dv.writeBE(native, sizeof(uint32_t) * 2).isOK());
        ASSERT_EQUALS(false, dv.writeNative(native, sizeof(uint32_t) * 3).isOK());

        ASSERT_EQUALS(buf, dv.view().getValue());
        ASSERT_EQUALS(buf + 5, dv.view(5).getValue());
        ASSERT_EQUALS(false, dv.view(50).isOK());

        ASSERT_EQUALS(native, dv.readNative<uint32_t>().getValue());
        ASSERT_EQUALS(native, dv.readLE<uint32_t>(sizeof(uint32_t)).getValue());
        ASSERT_EQUALS(native, dv.readBE<uint32_t>(sizeof(uint32_t) * 2).getValue());

        ASSERT_EQUALS(false, dv.readNative<uint32_t>(sizeof(uint32_t) * 3).isOK());
    }

} // namespace mongo
