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

    TEST(DataTypeRange, ConstDataTypeRange) {
        uint32_t nums[] = {1,2,3};
        char buf[sizeof(uint32_t) * 3 + 2];
        uint16_t last = 4;

        std::memcpy (buf, nums, 12);
        std::memcpy (buf + 12, &last, 2);

        ConstDataRangeCursor cdrc(buf, buf + sizeof(buf));

        ConstDataTypeRange<uint32_t> cdtr(&cdrc);

        ASSERT_EQUALS(cdrc.length(), 14u);
        auto x = cdtr.begin();
        ASSERT_EQUALS(cdrc.length(), 10u);

        ASSERT_EQUALS(1u, *x);
        ++x;

        ASSERT_EQUALS(2u, *x);
        ++x;

        ASSERT_EQUALS(3u, *x);
        x++;

        ASSERT(cdtr.end() == x);

        ASSERT_EQUALS(cdrc.length(), 2u);

        ConstDataTypeRange<uint16_t> cdtr16(&cdrc);

        for (auto&& y : cdtr16) {
            ASSERT_EQUALS(4u, y);
        }

        ASSERT_EQUALS(cdrc.length(), 0u);
    }

} // namespace mongo
