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
#include <iterator>
#include <type_traits>

#include "mongo/base/data_range.h"
#include "mongo/base/data_range_cursor.h"
#include "mongo/base/error_codes.h"
#include "mongo/base/status_with.h"
#include "mongo/platform/endian.h"

namespace mongo {

    template <typename T>
    class DataTypeRange;

    template <typename T>
    class ConstDataTypeRange : public ConstDataRange {
        friend class DataTypeRange<T>;

    public:
        class iterator : public std::iterator<
            std::forward_iterator_tag, T, std::ptrdiff_t, const T*, const T&> {
        public:
            iterator()
                : _cdtr(nullptr),
                  _cdrc(nullptr, nullptr),
                  _validated_elements(0),
                  _t(DataType::defaultConstruct<T>()) {
            }

            explicit iterator(ConstDataTypeRange* cdtr)
                : _cdtr(cdtr),
                  _cdrc(*_cdtr),
                  _validated_elements(0),
                  _t(DataType::defaultConstruct<T>()) {
                ++(*this);
            }

            const T& operator*() {
                return _t;
            }

            const T* operator->() {
                return &_t;
            }

            iterator& operator++() {
                if ((_cdrc.length() == 0) ||
                    (_cdtr->_elements && _validated_elements == _cdtr->_elements)) {

                    _cdtr->_safely_exhausted = true;
                    _cdtr = nullptr;
                }
                else {
                    auto status = _cdrc.readAndAdvance(&_t);

                    if (!status.isOK()) {
                        _cdtr = nullptr;
                        uassertStatusOK(status);
                    } else {
                        _cdtr->_validated_bytes = std::max(
                            _cdtr->_validated_bytes,
                            _cdtr->length() - _cdrc.length());

                        _cdtr->_validated_elements = std::max(
                            _cdtr->_validated_elements,
                            _validated_elements);

                        _validated_elements++;
                    }
                }

                return *this;
            }

            void operator++(int) {
                ++(*this);
            }

            friend bool operator==(const iterator& lhs, const iterator& rhs) {
                return lhs._cdtr == rhs._cdtr;
            }

            friend bool operator!=(const iterator& lhs, const iterator& rhs) {
                return !(lhs == rhs);
            }

        private:
            ConstDataTypeRange* _cdtr;
            ConstDataRangeCursor _cdrc;
            std::size_t _validated_elements;
            T _t;
        };

        friend class iterator;

        ConstDataTypeRange(ConstDataRange cdr, std::size_t elements = 0)
            : ConstDataRange(cdr),
              _validated_bytes(0),
              _validated_elements(0),
              _elements(elements),
              _safely_exhausted(false) {
        }

        iterator begin() {
            return iterator(this);
        }

        iterator end() {
            return iterator();
        }

        std::size_t validated_bytes() const {
            return _validated_bytes;
        }

        std::size_t validated_elements() const {
            return _validated_elements;
        }

        bool safely_exhausted() const {
            return _safely_exhausted;
        }

        ConstDataRange unvalidated() const {
            return ConstDataRange(_begin + _validated_bytes, _end);
        }

        template <typename U>
        ConstDataTypeRange<U> cast_unvalidated() const {
            return ConstDataRange(_begin + validated_bytes(), _end);
        }

        template <typename U>
        ConstDataTypeRange<U> cast() const {
            return ConstDataRange(_begin, _end);
        }

    private:
        std::size_t _validated_bytes;
        std::size_t _validated_elements;
        std::size_t _elements;
        bool _safely_exhausted;
    };

    template <typename T>
    class DataTypeRange : public DataRange {

    public:
        using iterator = typename ConstDataTypeRange<T>::iterator;
        using value_type = T;

        DataTypeRange(DataRange dr, std::size_t elements = 0)
            : DataRange(dr),
              _cdtr(dr, elements) {
        }

        iterator begin() {
            return _cdtr.begin();
        }

        iterator end() {
            return _cdtr.end();
        }

        std::size_t validated_bytes() const {
            return _cdtr.validated_bytes();
        }

        std::size_t validated_elements() const {
            return _cdtr.validated_elements();
        }

        bool safely_exhausted() const {
            return _cdtr.safely_exhausted();
        }

        DataRange unvalidated() const {
            return DataRange(const_cast<char*>(_begin) + validated_bytes(),
                             const_cast<char*>(_end));
        }

        template <typename U>
        DataTypeRange<U> cast_unvalidated() const {
            return DataRange(const_cast<char*>(_begin) + validated_bytes(),
                             const_cast<char*>(_end));
        }

        template <typename U>
        DataTypeRange<U> cast() const {
            return DataRange(const_cast<char*>(_begin),
                             const_cast<char*>(_end));
        }

        void push_back(const T& value) {
            if (_cdtr._safely_exhausted) {
                return;
            }

            DataRangeCursor drc(*this);

            uassertStatusOK(drc.advance(validated_bytes()));
            uassertStatusOK(drc.writeAndAdvance(value));

            _cdtr._validated_bytes = (drc.data() - data());
            _cdtr._validated_elements++;

            if (_cdtr._elements && _cdtr._validated_elements == _cdtr._elements) {
                _cdtr._safely_exhausted = true;
            }
        }

    private:
        ConstDataTypeRange<T> _cdtr;
    };
} // namespace mongo
