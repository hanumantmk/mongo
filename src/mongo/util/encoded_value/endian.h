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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <algorithm>
#include <stdint.h>

/* These should get pulled out into some host provided defines */

/* mongo specific for now */

#define ENCODED_VALUE_ENDIAN_HOST 1234
#define ENCODED_VALUE_ENDIAN_DEFAULT encoded_value::endian::Little

namespace mongo {
namespace encoded_value {
namespace endian {

    enum ConvertEndian {
        Noop = 0,
        Big = 1,
        Little = 2,
    };

#ifdef ENCODED_VALUE_ENDIAN_DEFAULT
    const enum ConvertEndian kDefault = ENCODED_VALUE_ENDIAN_DEFAULT;
#else
    const enum ConvertEndian kDefault = Noop;
#endif

    /* basically, needsSwab<T, ce>::result == true for integers and floats that are
     * opposite endian, assuming you actually care about their endianness (a
     * ConvertEndian other than Noop was provided) */

    template<typename T, enum ConvertEndian e>
    class needsSwab {
    public:
        static const bool result = false;
    };

#pragma push_macro("NEEDS_SWAB")

#if ENCODED_VALUE_ENDIAN_HOST == 4321
#define NEEDS_SWAB(type) \
        template<> \
        class needsSwab<type, Little> { \
        public: \
            static const bool result = true; \
        };
#elif ENCODED_VALUE_ENDIAN_HOST == 1234
#define NEEDS_SWAB(type) \
        template<> \
        class needsSwab<type, Big> { \
        public: \
            static const bool result = true; \
        };
#else
#  error "Unknown host endianness"
#endif

    NEEDS_SWAB(int16_t)
    NEEDS_SWAB(uint16_t)
    NEEDS_SWAB(int32_t)
    NEEDS_SWAB(uint32_t)
    NEEDS_SWAB(int64_t)
    NEEDS_SWAB(uint64_t)
    NEEDS_SWAB(double)
    NEEDS_SWAB(float)

#undef NEEDS_SWAB
#pragma pop_macro("NEEDS_SWAB")

    /* only providing a simplistic non-high performance swab at this point */

    template<typename T, enum ConvertEndian ce>
    inline T swab(T t) {
        char* front;
        char* back;

        if (! needsSwab<T, ce>::result) {
            return t;
        }

        front = reinterpret_cast<char *>(&t);
        back = front + sizeof(T) - 1;

        std::reverse(front, back);

        return t;
    }

} // namespace endian
} // namespace encoded_value
} // namespace mongo
