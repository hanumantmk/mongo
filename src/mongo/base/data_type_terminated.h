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

namespace mongo {

    template <char byte>
    struct Terminated {
        const char* ptr;
        size_t len;
    };

    template <char byte>
    struct DataType<Terminated<byte>> {
        static Status load(Terminated<byte>* t, const char *ptr, size_t length, size_t *advanced = nullptr)
        {
            const char* x = static_cast<const char *>(std::memchr(ptr, byte, length));

            if (! x) {
                return Status(ErrorCodes::BadValue, "Out of Range");
            }

            size_t t_len = x - ptr;

            if (t) {
                *t = Terminated<byte>{ptr, t_len};
            }

            if (advanced) {
                *advanced = t_len + 1;
            }

            return Status::OK();
        }

        static Status store(const Terminated<byte>& t, char *ptr, size_t length, size_t *advanced = nullptr)
        {
            if (t.len + 1 > length) {
                return Status(ErrorCodes::BadValue, "Out of Range");
            }

            if (ptr) {
                std::memcpy(ptr, t.ptr, t.len);
                ptr[t.len] = byte;
            }

            if (advanced) {
                *advanced = t.len + 1;
            }

            return Status::OK();
        }
    };

} // namespace mongo
