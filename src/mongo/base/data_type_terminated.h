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
#include <type_traits>

#include "mongo/base/data_type.h"
#include "mongo/base/error_codes.h"
#include "mongo/base/status_with.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

    template <char C, typename T>
    struct Terminated {
        Terminated() : t(DataType::defaultConstruct<T>()) {}
        Terminated(T t) : t(t) {}

        T t;
    };

    template <char C, typename T>
    struct DataType::Handler<Terminated<C, T>> {
        using TerminatedType = Terminated<C, T>;

        static Status load(TerminatedType* tt, const char *ptr, size_t length, size_t *advanced, std::ptrdiff_t debug_offset)
        {
            const void *end = std::memchr(ptr, C, length);

            if (! end) {
                mongoutils::str::stream ss;
                ss << "couldn't locate terminal char (" << C << ") in buffer[" << length
                   << "] at offset: " << debug_offset;
                return Status(ErrorCodes::Overflow, ss);
            }

            auto status = DataType::load(tt ? &tt->t : nullptr, ptr, static_cast<const char*>(end) - ptr, advanced, debug_offset);

            if (advanced) {
                (*advanced)++;
            }

            return status;
        }

        static Status store(const TerminatedType& tt, char *ptr, size_t length, size_t *advanced, std::ptrdiff_t debug_offset)
        {
            size_t local_advanced = 0;

            auto status = DataType::store(tt.t, ptr, length, &local_advanced, debug_offset);

            if (! status.isOK()) {
                return status;
            }

            if (length - local_advanced < 1) {
                mongoutils::str::stream ss;
                ss << "couldn't write terminal char (" << C << ") in buffer[" << length
                   << "] at offset: " << debug_offset + local_advanced;
                return Status(ErrorCodes::Overflow, ss);
            }

            ptr[local_advanced] = C;

            if (advanced) {
                *advanced = local_advanced + 1;
            }

            return Status::OK();
        }

        static TerminatedType defaultConstruct() {
            return TerminatedType();
        }

    };

} // namespace mongo
