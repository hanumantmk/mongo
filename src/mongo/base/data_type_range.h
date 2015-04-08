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
    class ConstDataTypeRange {

    public:
        class iterator : public std::iterator<
            std::input_iterator_tag, T, std::ptrdiff_t, const T*, const T&> {
        public:
            iterator()
                : _cdtr(nullptr) {
            }

            explicit iterator(ConstDataTypeRange* cdtr)
                : _cdtr(cdtr) {
                ++(*this);
            }

            const T& operator*() {
                return _t;
            }

            const T* operator->() {
                return &_t;
            }

            iterator& operator++() {
                auto status = _cdtr->_cdrc->readAndAdvance(&_t);

                if (! status.isOK()) {
                    _cdtr->_status = std::move(status);
                    _cdtr = nullptr;
                }

                return *this;
            }

            void operator++(int) {
                ++(*this);
            }

            friend bool operator==(const iterator& lhs, const iterator& rhs) {
                if (lhs._cdtr && rhs._cdtr) {
                    return ((lhs._cdtr == rhs._cdtr) || (*lhs._cdtr == *rhs._cdtr));
                } else {
                    return lhs._cdtr == rhs._cdtr;
                }
            }

            friend bool operator!=(const iterator& lhs, const iterator& rhs) {
                return !(lhs == rhs);
            }

        private:
            ConstDataTypeRange* _cdtr;
            T _t;
        };

        ConstDataTypeRange(ConstDataRangeCursor* cdrc)
            : _cdrc(cdrc), _status(Status::OK()) {
        }

        iterator begin() {
            return iterator(this);
        }

        iterator end() {
            return iterator();
        }

        Status& status() {
            return _status;
        }

        const Status& status() const {
            return _status;
        }

        friend bool operator==(const ConstDataTypeRange& lhs, const ConstDataTypeRange& rhs) {
            if (lhs._cdrc && rhs._cdrc) {
                return ((lhs._cdrc == rhs._cdrc) || (*lhs._cdrc == *rhs._cdrc));
            } else {
                return lhs._cdrc == rhs._cdrc;
            }
        }

        friend bool operator!=(const ConstDataTypeRange& lhs, const ConstDataTypeRange& rhs) {
            return !(lhs == rhs);
        }

    private:
        ConstDataRangeCursor* _cdrc;
        Status _status;
    };

} // namespace mongo
