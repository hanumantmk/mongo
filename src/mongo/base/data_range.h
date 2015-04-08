/*    Copyright 2015 MongoDB Inc.
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
#include "mongo/util/mongoutils/str.h"

namespace mongo {

    class ConstDataRange {

    public:
        typedef const char* bytes_type;

        // begin and end should point to the first and one past last bytes in
        // the range you wish to view.
        //
        // debug_offset provides a way to indicate that the ConstDataRange is
        // located at an offset into some larger logical buffer. By setting it
        // to a non-zero value, you'll change the Status messages that are
        // returned on failure to be offset by the amount passed to this
        // constructor.
        ConstDataRange(bytes_type begin, bytes_type end, std::ptrdiff_t debug_offset = 0)
            : _begin(begin), _end(end), _debug_offset(debug_offset) {
        }

        StatusWith<bytes_type> view(std::size_t offset = 0) const {
            if (_begin + offset > _end) {
                mongoutils::str::stream ss;
                ss << "Invalid view(" << offset << ") past end of buffer[" << length()
                   << "] at offset: " << _debug_offset;

                return StatusWith<bytes_type>(ErrorCodes::Overflow, ss);
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
        Status read(T* t, size_t offset = 0) const {
            if (offset > length()) {
                mongoutils::str::stream ss;
                ss << "Invalid offset(" << offset << ") past end of buffer[" << length()
                   << "] at offset: " << _debug_offset;

                Status(ErrorCodes::Overflow, ss);
            }

            return DataType::load(t, _begin + offset, length() - offset, nullptr,
                                  offset + _debug_offset);
        }

        template<typename T>
        StatusWith<T> read(std::size_t offset = 0) const {
            T t(DataType::defaultConstruct<T>());
            Status s = read(&t, offset);

            if (s.isOK()) {
                return StatusWith<T>(std::move(t));
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
        std::ptrdiff_t _debug_offset;

    };

    class DataRange : public ConstDataRange {

    public:
        typedef char* bytes_type;

        DataRange(bytes_type begin, bytes_type end, std::ptrdiff_t debug_offset = 0)
            : ConstDataRange(begin, end, debug_offset) {
        }

        StatusWith<bytes_type> view(std::size_t offset = 0) const {
            // It is safe to cast away const here since the pointer stored in
            // our base class was originally non-const by way of our
            // constructor.
            
            if (_begin + offset > _end) {
                mongoutils::str::stream ss;
                ss << "Invalid view(" << offset << ") past end of buffer[" << length()
                   << "] at offset: " << _debug_offset;

                return StatusWith<bytes_type>(ErrorCodes::Overflow, ss);
            }

            return StatusWith<bytes_type>(const_cast<bytes_type>(_begin) + offset);
        }

        template<typename T>
        size_t additionalBytesNeeded(const T& value, std::size_t offset = 0) {
            size_t advance;

            DataType::store(value, nullptr, std::numeric_limits<size_t>::max(), &advance,
                            offset + _debug_offset);

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
        Status write(const T& value, std::size_t offset = 0) {
            if (offset > length()) {
                mongoutils::str::stream ss;
                ss << "Invalid offset(" << offset << ") past end of buffer[" << length()
                   << "] at offset: " << _debug_offset;

                return Status(ErrorCodes::Overflow, ss);
            }

            return DataType::store(value, const_cast<char *>(_begin + offset), length() - offset,
                                   nullptr, offset + _debug_offset);
        }

    };

} // namespace mongo
