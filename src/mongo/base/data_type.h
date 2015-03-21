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
#include "mongo/platform/endian.h"

namespace mongo {

    template <typename T>
    Status data_type_load(T* t, const char *ptr, size_t length, size_t *advanced);

    template <typename T>
    Status data_type_store(const T& t, char *ptr, size_t length, size_t *advanced);

    template <typename T>
    struct DataType {
        static Status load(T* t, const char *ptr, size_t length, size_t *advanced = nullptr)
        {
#if MONGO_HAVE_STD_IS_TRIVIALLY_COPYABLE
            static_assert(std::is_trivially_copyable<T>::value,
                          "The generic DataType implementation requires values "
                          "to be trivially copiable.  You may specialize the "
                          "template to use it with other types.");
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
                          "The generic DataType implementation requires values "
                          "to be trivially copiable.  You may specialize the "
                          "template to use it with other types.");
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

    template <typename T>
    struct BigEndian {
        T value;
    };

    template <typename T>
    struct LittleEndian {
        T value;
    };

    template <typename T>
    struct DataType<BigEndian<T>> {
        static Status load(BigEndian<T>* t, const char *ptr, size_t length, size_t *advanced = nullptr)
        {
            if (t) {
                Status x = data_type_load(&t->value, ptr, length, advanced);

                if (x.isOK()) {
                    t->value = endian::bigToNative(t->value);
                }

                return x;
            } else {
                return data_type_load(decltype(&t->value){nullptr}, ptr, length, advanced);
            }
        }

        static Status store(const BigEndian<T>& t, char *ptr, size_t length, size_t *advanced = nullptr)
        {
            return data_type_store(endian::nativeToBig(t.value), ptr, length, advanced);
        }

    };

    template <typename T>
    struct DataType<LittleEndian<T>> {
        static Status load(LittleEndian<T>* t, const char *ptr, size_t length, size_t *advanced = nullptr)
        {
            if (t) {
                Status x = data_type_load(&t->value, ptr, length, advanced);

                if (x.isOK()) {
                    t->value = endian::littleToNative(t->value);
                }

                return x;
            } else {
                return data_type_load(decltype(&t->value){nullptr}, ptr, length, advanced);
            }
        }

        static Status store(const LittleEndian<T>& t, char *ptr, size_t length, size_t *advanced = nullptr)
        {
            return data_type_store(endian::nativeToLittle(t.value), ptr, length, advanced);
        }

    };

    template <typename... Args>
    struct DataType<std::tuple<Args...>> {
        using tuple_type = std::tuple<Args...>;

        static Status load(tuple_type* t, const char *ptr, size_t length, size_t *advanced = nullptr)
        {
            size_t tmp = 0;

            Status x = _load<0>(t, ptr, length, &tmp);

            if (advanced && x.isOK()) {
                *advanced = tmp;
            }

            return x;
        }

        static Status store(const tuple_type& t, char *ptr, size_t length, size_t *advanced = nullptr)
        {
            size_t tmp = 0;

            Status x = _store<0>(t, ptr, length, &tmp);

            if (advanced && x.isOK()) {
                *advanced = tmp;
            }

            return x;
        }

    private:

        template <size_t N> static
        typename std::enable_if<N != sizeof...(Args), Status>::type
        _load(tuple_type* t, const char *ptr, size_t length, size_t *advanced)
        {
            size_t local_advanced;
            auto t_ptr = t ? &std::get<N>(*t) : nullptr;

            Status x = data_type_load(t_ptr, ptr + *advanced, length - *advanced, &local_advanced);

            if (x.isOK()) {
                *advanced += local_advanced;

                x = _load<N+1>(t, ptr, length, advanced);
            }

            return x;
        }

        template <size_t N> static
        typename std::enable_if<N != sizeof...(Args), Status>::type
        _store(const tuple_type& t, char *ptr, size_t length, size_t *advanced)
        {
            size_t local_advanced;
            char *adjusted_ptr = ptr ? ptr + *advanced : nullptr;

            Status x = data_type_store(std::get<N>(t), adjusted_ptr, length - *advanced, &local_advanced);

            if (x.isOK()) {
                *advanced += local_advanced;

                x = _store<N+1>(t, ptr, length, advanced);
            }

            return x;
        }

        template <size_t N> static
        typename std::enable_if<N == sizeof...(Args), Status>::type
        _load(tuple_type* t, const char *ptr, size_t length, size_t *advanced)
        {
            return Status::OK();
        }

        template <size_t N> static
        typename std::enable_if<N == sizeof...(Args), Status>::type
        _store(const tuple_type& t, char *ptr, size_t length, size_t *advanced)
        {
            return Status::OK();
        }
    };

    template <typename T>
    Status data_type_load(T* t, const char *ptr, size_t length, size_t *advanced)
    {
        return DataType<T>::load(t, ptr, length, advanced);
    }

    template <typename T>
    Status data_type_store(const T& t, char *ptr, size_t length, size_t *advanced)
    {
        return DataType<T>::store(t, ptr, length, advanced);
    }

} // namespace mongo
