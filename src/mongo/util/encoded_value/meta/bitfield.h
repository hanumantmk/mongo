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

/* BitField provides safe access to bitfields by memcpy'ing a base type, then
 * doing the bit math for you
 *
 * Template params are as follows:
 *
 * T - The type to return
 *
 * Base - The underlying integer type we're reading in and out of
 *
 * offset - the number of bits we have to shift to line up the T value in the
 *          lower order bits of Base
 *
 * bits - the number of bits we'll represent T with
 *
 * ce - endian conversion
 *
 * */

#include <cstring>

#include "mongo/util/encoded_value/endian.h"
#include "mongo/util/encoded_value/reference.h"

namespace mongo {
namespace encoded_value {
namespace Meta {

    template <typename T, typename Base, int offset, int bits, enum endian::ConvertEndian ce>
    class BitField {
    public:
        static const std::size_t size = sizeof(Base);

        typedef T type;

        /* write a t to some Base that's in memory.  This operation only touches
         * the parts of Base refered to by offset and bits */
        static inline void writeTo(const T& t, void* ptr) {
            /* grab b out of memory and swab it if needed */
            Base b;

            std::memcpy(&b, ptr, sizeof(Base));
            b = endian::swab<Base, ce>(b);

            /* mask out the bits we're responsible for */
            b &= ~(((1 << bits) - 1) << offset);

            /* or in t */
            b |= t << offset;

            /* write everything back to memory */
            b = endian::swab<Base, ce>(b);
            std::memcpy(ptr, &b, sizeof(Base));
        }

        /* copy from some Base in memory and take the relevant bits and assign them
         * to t */
        static inline void readFrom(T& t, const void* ptr) {
            Base b;

            /* grab and swab Base from memory */
            std::memcpy(&b, ptr, sizeof(Base));
            b = endian::swab<Base, ce>(b);

            /* push the relevant bits into t */
            t = (b >> offset) & ((1 << bits) - 1);
        }
    };

} // namespace Meta
} // namespace encoded_value
} // namespace mongo
