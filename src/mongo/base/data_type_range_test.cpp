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

#include "mongo/base/data_type_range.h"

#include <cstring>

#include "mongo/platform/endian.h"
#include "mongo/unittest/unittest.h"

namespace mongo {

    bool errorIsOverflow(const DBException& ex) {
        return ex.getCode() == ErrorCodes::Overflow;
    };

    TEST(DataTypeRange, ConstDataTypeRange) {
        uint32_t nums[] = {1,2,3};
        char buf[sizeof(uint32_t) * 3 + 2];
        uint16_t last = 4;

        std::memcpy (buf, nums, 12);
        std::memcpy (buf + 12, &last, 2);

        ConstDataRange cdr(buf, buf + sizeof(buf));

        ConstDataTypeRange<uint32_t> cdtr(cdr, 3);

        ASSERT_EQUALS(cdtr.validated_bytes(), 0u);
        auto x = cdtr.begin();
        ASSERT_EQUALS(cdtr.validated_bytes(), 4u);

        ASSERT_EQUALS(1u, *x);
        ++x;

        ASSERT_EQUALS(2u, *x);
        ++x;

        ASSERT_EQUALS(3u, *x);
        x++;

        ASSERT(cdtr.end() == x);

        ASSERT_EQUALS(cdtr.validated_bytes(), 12u);
        ASSERT_EQUALS(cdtr.safely_exhausted(), true);

        ConstDataTypeRange<uint16_t> cdtr16(cdtr.unvalidated());

        for (auto&& y : cdtr16) {
            ASSERT_EQUALS(4u, y);
        }

        ASSERT_EQUALS(cdtr16.validated_bytes(), 2u);

        auto cdtr32 = cdtr.cast_unvalidated<uint32_t>();

        ASSERT_THROWS_PRED(cdtr32.begin(), UserException, errorIsOverflow);
    }

    TEST(DataTypeRange, DataTypeRange) {
        std::array<uint32_t, 3> nums{{1,2,3}};
        char buf[sizeof(uint32_t) * 3 + 2];

        DataRange dr(buf, buf + sizeof(buf));

        DataTypeRange<uint32_t> dtr(dr, 3);
        std::copy(nums.begin(), nums.end(), std::back_inserter(dtr));
        auto dtr16 = dtr.cast_unvalidated<uint16_t>();
        dtr16.push_back(4u);
        DataTypeRange<uint32_t> dtr32(dtr.unvalidated());

        ASSERT_THROWS_PRED(dtr32.push_back(5u), UserException, errorIsOverflow);

        auto x = dtr.begin();

        ASSERT_EQUALS(1u, *x);
        ++x;

        ASSERT_EQUALS(2u, *x);
        ++x;

        ASSERT_EQUALS(3u, *x);
        x++;

        ASSERT(dtr.end() == x);

        ASSERT_EQUALS(dtr.safely_exhausted(), true);

        for (auto&& y : dtr16) {
            ASSERT_EQUALS(4u, y);
        }

        ASSERT_THROWS_PRED(dtr32.begin(), UserException, errorIsOverflow);
    }

} // namespace mongo
