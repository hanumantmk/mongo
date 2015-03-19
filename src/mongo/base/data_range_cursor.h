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

#include <cstddef>
#include <cstring>
#include <limits>

#include "mongo/base/data_type.h"
#include "mongo/base/data_range.h"
#include "mongo/platform/endian.h"

namespace mongo {

    class ConstDataRangeCursor : public ConstDataRange {
    public:

        ConstDataRangeCursor(const char* begin, const char *end)
            : ConstDataRange(begin, end) {
        }

        ConstDataRangeCursor(ConstDataRange cdr)
            : ConstDataRange(cdr) {
        }

        Status advance(size_t advance) {
            if (advance <= length()) {
                _begin += advance;

                return Status::OK();
            } else {
                return Status(ErrorCodes::BadValue, "Out of Range");
            }
        }

        template <typename T>
        Status skip() {
            size_t advanced = 0;

            Status x = DataType<T>::load(nullptr, _begin, _end - _begin, &advanced);

            if (x.isOK()) {
                _begin += advanced;
            }

            return x;
        }

        template <typename T>
        Status readNativeAndAdvance(T* t) {
            size_t advanced = 0;

            Status x = DataType<T>::load(t, _begin, _end - _begin, &advanced);

            if (x.isOK()) {
                _begin += advanced;
            }

            return x;
        }

        template <typename T>
        StatusWith<T> readNativeAndAdvance() {
            T out{};
            Status x = readNativeAndAdvance(&out);

            if (x.isOK()) {
                return StatusWith<T>(out);
            } else {
                return StatusWith<T>(x);
            }
        }

        template <typename T>
        StatusWith<T> readLEAndAdvance() {
            auto x = readNativeAndAdvance<T>();

            if (x.isOK()) {
                return StatusWith<T>(endian::littleToNative(x.getValue()));
            } else {
                return x;
            }
        }

        template <typename T>
        StatusWith<T> readBEAndAdvance() {
            auto x = readNativeAndAdvance<T>();

            if (x.isOK()) {
                return StatusWith<T>(endian::bigToNative(x.getValue()));
            } else {
                return x;
            }
        }

    };

    class DataRangeCursor : public DataRange {
    public:

        DataRangeCursor(char *begin, char *end)
            : DataRange(begin, end) {}

        DataRangeCursor(DataRange range)
            : DataRange(range) {}

        operator ConstDataRangeCursor() const {
            return ConstDataRangeCursor(ConstDataRange(_begin, _end));
        }

        Status advance(size_t advance) {
            if (advance <= length()) {
                _begin += advance;

                return Status::OK();
            } else {
                return Status(ErrorCodes::BadValue, "Out of Range");
            }
        }

        template <typename T>
        Status skip() {
            size_t advanced = 0;

            Status x = DataType<T>::load(nullptr, _begin, _end - _begin, &advanced);

            if (x.isOK()) {
                _begin += advanced;
            }

            return x;
        }

        template <typename T>
        Status readNativeAndAdvance(T* t) {
            size_t advanced = 0;

            Status x = DataType<T>::load(t, _begin, _end - _begin, &advanced);

            if (x.isOK()) {
                _begin += advanced;
            }

            return x;
        }

        template <typename T>
        StatusWith<T> readNativeAndAdvance() {
            T out{};
            Status x = readNativeAndAdvance(&out);

            if (x.isOK()) {
                return StatusWith<T>(out);
            } else {
                return StatusWith<T>(x);
            }
        }

        template <typename T>
        StatusWith<T> readLEAndAdvance() {
            auto x = readNativeAndAdvance<T>();

            if (x.isOK()) {
                return StatusWith<T>(endian::littleToNative(x.getValue()));
            } else {
                return x;
            }
        }

        template <typename T>
        StatusWith<T> readBEAndAdvance() {
            auto x = readNativeAndAdvance<T>();

            if (x.isOK()) {
                return StatusWith<T>(endian::bigToNative(x.getValue()));
            } else {
                return x;
            }
        }

        template <typename T>
        Status writeNativeAndAdvance(const T& value) {
            size_t advanced = 0;

            Status x = DataType<T>::store(value, const_cast<char *>(_begin), _end - _begin, &advanced);

            if (x.isOK()) {
                _begin += advanced;
            }

            return x;
        }

        template <typename T>
        Status writeLEAndAdvance(const T& value) {
            return writeNativeAndAdvance(endian::nativeToLittle(value));
        }

        template <typename T>
        Status writeBEAndAdvance(const T& value) {
            return writeNativeAndAdvance(endian::nativeToBig(value));
        }
    };

} // namespace mongo
