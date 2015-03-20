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

#include <iostream>

#include <cstddef>
#include <cstring>

#include "mongo/base/data_range_cursor.h"
#include "mongo/platform/endian.h"
#include "mongo/util/allocator.h"

namespace mongo {

    class DataBuilder {
    public:

        DataBuilder()
            : _drc(nullptr, nullptr) {
            _nullout();
        }

        DataBuilder(size_t bytes)
            : _buf(static_cast<char *>(mongoMalloc(bytes))),
              _reserved(bytes),
              _drc(_buf, _buf + bytes) {
        }

        DataBuilder(const DataBuilder& db) : _buf(nullptr), _drc(nullptr, nullptr) {
            *this = db;
        }

        DataBuilder& operator=(const DataBuilder& db) {
            resize(db._reserved);
            std::memcpy(_buf, db._buf, db.size());
            _drc = DataRangeCursor(_buf + db.size(), _buf + _reserved);

            return *this;
        }

        DataBuilder(DataBuilder&& db) : _buf(nullptr), _drc(nullptr, nullptr) {
            *this = std::move(db);
        }

        DataBuilder& operator=(DataBuilder&& db) {
            if (_buf) {
                std::free(_buf);
            }

            _buf = db._buf;
            _reserved = db._reserved;
            _drc = db._drc;

            db._nullout();

            return *this;
        }

        ~DataBuilder() {
            if (_buf) {
                std::free(_buf);
            }
        }

        template<typename T>
        Status writeNative(const T& value, std::size_t offset = 0) {
            if (! _buf) {
                resize(1);
            }

            auto x = _drc.writeNative(value, offset);

            if (! x.isOK()) {
                reserve(_drc.additionalBytesNeeded(value, offset));
                x = _drc.writeNative(value, offset);
            }

            return x;
        }

        template<typename T>
        Status writeLE(const T& value, std::size_t offset = 0) {
            return writeNative(endian::nativeToLittle(value), offset);
        }

        template<typename T>
        Status writeBE(const T& value, std::size_t offset = 0) {
            return writeNative(endian::nativeToBig(value), offset);
        }
        template <typename T>
        Status writeNativeAndAdvance(const T& value) {
            if (! _buf) {
                resize(1);
            }

            auto x = _drc.writeNativeAndAdvance(value);

            if (! x.isOK()) {
                reserve(_drc.additionalBytesNeeded(value));
                x = _drc.writeNativeAndAdvance(value);
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

        DataRangeCursor data_range_cursor() {
            return DataRangeCursor(_buf, _buf + size());
        }

        ConstDataRangeCursor data_range_cursor() const {
            return ConstDataRangeCursor(_buf, _buf + size());
        }

        size_t size() const {
            if (! _buf) {
                return 0;
            }

            return _reserved - _drc.length();
        }

        size_t reserved() const {
            return _reserved;
        }

        void resize(size_t new_size) {
            size_t old_size = size();

            _reserved = new_size;

            _buf = static_cast<char *>(mongoRealloc(_buf, _reserved));

            if (old_size > _reserved) {
                _drc = DataRangeCursor(_buf + _reserved, _buf + _reserved);
            } else {
                _drc = DataRangeCursor(_buf + old_size, _buf + _reserved);
            }
        }

        void reserve(size_t needed) {
            size_t old_size = size();
            size_t new_size = _reserved ? _reserved : 1;

            while (new_size < old_size || new_size - old_size < needed) {
                new_size *= 2;
            }

            resize(new_size);
        }

        void clear() {
            _drc = DataRangeCursor(_buf, _buf + _reserved);
        }

        const char* data() const {
            return _buf;
        }

    private:
        void _nullout() {
            _buf = nullptr;
            _reserved = 0;
            _drc = DataRangeCursor(nullptr, nullptr);
        }

        char *_buf;
        size_t _reserved;
        DataRangeCursor _drc;
    };

} // namespace mongo
