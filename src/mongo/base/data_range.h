/*    Copyright 2014 MongoDB Inc.
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

#pragma once

#include <cstring>
#include <tuple>
#include <type_traits>

#include "mongo/base/data_type.h"
#include "mongo/base/error_codes.h"
#include "mongo/base/status_with.h"
#include "mongo/platform/endian.h"

namespace mongo {

    class ConstDataRange {

    public:
        typedef const char* bytes_type;

        ConstDataRange(bytes_type begin, bytes_type end)
            : _begin(begin), _end(end) {
        }

        StatusWith<bytes_type> view(std::size_t offset = 0) const {
            if (_begin + offset > _end) {
                return StatusWith<bytes_type>(ErrorCodes::BadValue, "Out of range");
            }

            return StatusWith<bytes_type>(_begin + offset);
        }

        bytes_type data() const {
            return _begin;
        }

        size_t length() const {
            return _end - _begin;
        }

        template<typename T>
        Status readNative(T* t, size_t offset = 0) const {
            if (offset > length()) {
                Status(ErrorCodes::BadValue, "Out of range");
            }

            return DataType<T>::load(t, _begin + offset, length() - offset);
        }

        template<typename T>
        StatusWith<T> readNative(std::size_t offset = 0) const {
            T t{};
            Status s = readNative(&t, offset);

            if (s.isOK()) {
                return StatusWith<T>(t);
            } else {
                return StatusWith<T>(std::move(s));
            }
        }

        template<typename T>
        StatusWith<T> readLE(std::size_t offset = 0) const {
            T t{};
            Status s = readNative(&t, offset);

            if (s.isOK()) {
                t = endian::littleToNative(t);

                return StatusWith<T>(t);
            } else {
                return StatusWith<T>(std::move(s));
            }
        }

        template<typename T>
        StatusWith<T> readBE(std::size_t offset = 0) const {
            T t{};
            Status s = readNative(&t, offset);

            if (s.isOK()) {
                t = endian::bigToNative(t);

                return StatusWith<T>(t);
            } else {
                return StatusWith<T>(std::move(s));
            }
        }

        friend bool operator==(const ConstDataRange& lhs, const ConstDataRange& rhs) {
            return std::tie(lhs._begin, lhs._end) == std::tie(rhs._begin, rhs._end);
        }

        friend bool operator!=(const ConstDataRange& lhs, const ConstDataRange& rhs) {
            return !(lhs == rhs);
        }


    protected:
        bytes_type _begin;
        bytes_type _end;
    };

    class DataRange : public ConstDataRange {

    public:
        typedef char* bytes_type;

        DataRange(bytes_type begin, bytes_type end)
            : ConstDataRange(begin, end) {
        }

        StatusWith<bytes_type> view(std::size_t offset = 0) const {
            // It is safe to cast away const here since the pointer stored in our base class was
            // originally non-const by way of our constructor.
            
            if (_begin + offset > _end) {
                return StatusWith<bytes_type>(ErrorCodes::BadValue, "Out of range");
            }

            return StatusWith<bytes_type>(const_cast<bytes_type>(_begin) + offset);
        }

        template<typename T>
        size_t additionalBytesNeeded(const T& value, std::size_t offset = 0) {
            size_t advance;

            DataType<T>::store(value, nullptr, std::numeric_limits<size_t>::max(), &advance);

            // if there are bytes left in the range
            if (length() > offset) {

                // If the size of the object is larger than the remaining bytes
                if (advance > length() - offset) {

                    // subtract the remaining bytes from the object size
                    return advance - (length() - (offset + 1));
                } else {

                    // object fits, just put it in
                    return 0;
                }
            } else {
                // no bytes left in the range, we'll need more than the object size

                return advance + (offset - length());
            }
        }

        template<typename T>
        Status writeNative(const T& value, std::size_t offset = 0) {
            if (offset > length()) {
                return Status(ErrorCodes::BadValue, "Out of range");
            }

            return DataType<T>::store(value, const_cast<char *>(_begin + offset), length() - offset);
        }

        template<typename T>
        Status writeLE(const T& value, std::size_t offset = 0) {
            return writeNative(endian::nativeToLittle(value), offset);
        }

        template<typename T>
        Status writeBE(const T& value, std::size_t offset = 0) {
            return writeNative(endian::nativeToBig(value), offset);
        }
    };

} // namespace mongo
