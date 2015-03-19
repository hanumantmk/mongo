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

#include "mongo/base/error_codes.h"
#include "mongo/base/status_with.h"

namespace mongo {

    template <typename T>
    struct DataType {
        static Status load(T* t, const char *ptr, size_t length, size_t *advanced = nullptr)
        {
#if MONGO_HAVE_STD_IS_TRIVIALLY_COPYABLE
            static_assert(std::is_trivially_copyable<T>::value,
                          "Type for read must be trivially copyable");
#endif

            if (sizeof (T) > length) {
                return Status(ErrorCodes::BadValue, "Out of Range");
            }

            if (t) {
                std::memcpy(t, ptr, sizeof (T));
            }

            if (advanced) {
                *advanced = sizeof (T);
            }

            return Status::OK();
        }

        static Status store(const T& t, char *ptr, size_t length, size_t *advanced = nullptr)
        {
#if MONGO_HAVE_STD_IS_TRIVIALLY_COPYABLE
            static_assert(std::is_trivially_copyable<T>::value,
                          "Type for write must be trivially copyable");
#endif

            if (sizeof (T) > length) {
                return Status(ErrorCodes::BadValue, "Out of Range");
            }

            if (ptr) {
                std::memcpy(ptr, &t, sizeof (T));
            }

            if (advanced) {
                *advanced = sizeof (T);
            }

            return Status::OK();
        }

    };

} // namespace mongo
