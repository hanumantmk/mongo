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
#include <tuple>

#include "mongo/base/data_type.h"
#include "mongo/base/error_codes.h"
#include "mongo/base/status_with.h"

namespace mongo {

    template <typename... Args>
    struct DataType::Handler<std::tuple<Args...>> {
        using tuple_type = std::tuple<Args...>;

        static Status load(tuple_type* t, const char *ptr, size_t length, size_t *advanced,
                           std::ptrdiff_t debug_offset)
        {
            size_t tmp = 0;

            Status x = _load<0>(t, ptr, length, &tmp, debug_offset);

            if (advanced && x.isOK()) {
                *advanced = tmp;
            }

            return x;
        }

        static Status store(const tuple_type& t, char *ptr, size_t length, size_t *advanced,
                            std::ptrdiff_t debug_offset)
        {
            size_t tmp = 0;

            Status x = _store<0>(t, ptr, length, &tmp, debug_offset);

            if (advanced && x.isOK()) {
                *advanced = tmp;
            }

            return x;
        }

    private:

        template <size_t N> static
        typename std::enable_if<N != sizeof...(Args), Status>::type
        _load(tuple_type* t, const char *ptr, size_t length, size_t *advanced,
              std::ptrdiff_t debug_offset)
        {
            size_t local_advanced = 0;
            auto t_ptr = t ? &std::get<N>(*t) : nullptr;

            Status x = DataType::load(t_ptr, ptr + *advanced, length - *advanced, &local_advanced,
                                      debug_offset);

            if (x.isOK()) {
                *advanced += local_advanced;

                x = _load<N+1>(t, ptr, length, advanced, debug_offset + local_advanced);
            }

            return x;
        }

        template <size_t N> static
        typename std::enable_if<N != sizeof...(Args), Status>::type
        _store(const tuple_type& t, char *ptr, size_t length, size_t *advanced,
               std::ptrdiff_t debug_offset)
        {
            size_t local_advanced = 0;
            char *adjusted_ptr = ptr ? ptr + *advanced : nullptr;

            Status x = DataType::store(std::get<N>(t), adjusted_ptr, length - *advanced,
                                       &local_advanced, debug_offset);

            if (x.isOK()) {
                *advanced += local_advanced;

                x = _store<N+1>(t, ptr, length, advanced, debug_offset + local_advanced);
            }

            return x;
        }

        template <size_t N> static
        typename std::enable_if<N == sizeof...(Args), Status>::type
        _load(tuple_type* t, const char *ptr, size_t length, size_t *advanced,
              std::ptrdiff_t debug_offset)
        {
            return Status::OK();
        }

        template <size_t N> static
        typename std::enable_if<N == sizeof...(Args), Status>::type
        _store(const tuple_type& t, char *ptr, size_t length, size_t *advanced,
               std::ptrdiff_t debug_offset)
        {
            return Status::OK();
        }
    };

} // namespace mongo
