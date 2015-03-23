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
#include "mongo/base/status.h"

namespace mongo {

    template <typename T>
    struct StatusOr {
        StatusOr(T& t, Status s) : value(t), status(s) {}

        T& value;
        Status status;
    };

    template <typename T>
    struct DataType<StatusOr<T>> {
        static Status load(StatusOr<T>* t, const char *ptr, size_t length, size_t *advanced = nullptr)
        {
            if (t) {
                Status x = data_type_load(&t->value, ptr, length, advanced);

                if (! x.isOK()) {
                    return t->status;
                }

                return x;
            } else {
                return data_type_load(decltype(&t->value){nullptr}, ptr, length, advanced);
            }
        }

        static Status store(const StatusOr<T>& t, char *ptr, size_t length, size_t *advanced = nullptr)
        {
            Status x = data_type_store(t.value, ptr, length, advanced);

            if (! x.isOK()) {
                return t.status;
            }

            return x;
        }

    };

} // namespace mongo
