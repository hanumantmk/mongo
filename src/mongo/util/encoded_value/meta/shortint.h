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

/* Memcpy provides safe access to values by memcpy'ing them
 *
 * Template params are as follows:
 *
 * T - The type to return
 *
 * bytes - The number of bytes for storage
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

    template <typename T, int bytes, enum endian::ConvertEndian ce>
    class ShortInt {
    public:
        static const std::size_t size = bytes;
        typedef T type;

        static const int adjustment = (ce == endian::Little || (ENCODED_VALUE_ENDIAN_HOST == endian::Little && ce == endian::Noop)) ? 0 : sizeof(T) - bytes;

        static inline void writeTo(const T& t, void* ptr) {
            T tmp = endian::swab<T, ce>(t);
            std::memcpy(ptr, reinterpret_cast<char *>(&tmp) + adjustment, size);
        }

        static inline void readFrom(T& t, const void* ptr) {
            t = 0;
            std::memcpy(reinterpret_cast<char *>(&t) + adjustment, ptr, size);

            t = endian::swab<T, ce>(t);
        }
    };

} // namespace Meta
} // namespace encoded_value
} // namespace mongo
