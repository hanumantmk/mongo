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

#include "mongo/config.h"

#include <cstring>

// We use ssteam rather than mongoutils::str::stream because str::stream relies
// on bson/util/builder.h, which relies on... this file
#include <sstream>

#include <type_traits>

#include "mongo/base/error_codes.h"
#include "mongo/base/status_with.h"

namespace mongo {

    template <typename T>
    struct DataType {

        static void unsafe_load(T* t, const char* ptr, size_t* advanced)
        {
#if MONGO_HAVE_STD_IS_TRIVIALLY_COPYABLE
            static_assert(std::is_trivially_copyable<T>::value,
                          "The generic DataType implementation requires values "
                          "to be trivially copyable. You may specialize the "
                          "template to use it with other types.");
#endif

            if (t) {
                std::memcpy(t, ptr, sizeof(T));
            }

            if (advanced) {
                *advanced = sizeof(T);
            }
        }

        static Status load(T* t, const char* ptr, size_t length, size_t* advanced,
                           std::ptrdiff_t debug_offset)
        {
            if (sizeof(T) > length) {
                std::stringstream ss;
                ss << "buffer size too small to read (" << sizeof(T) << ") bytes out of buffer["
                   << length << "] at offset: " << debug_offset;
                return Status(ErrorCodes::Overflow, ss.str());
            }

            unsafe_load(t, ptr, advanced);

            return Status::OK();
        }

        static void unsafe_store(const T& t, char* ptr, size_t* advanced)
        {
#if MONGO_HAVE_STD_IS_TRIVIALLY_COPYABLE
            static_assert(std::is_trivially_copyable<T>::value,
                          "The generic DataType implementation requires values "
                          "to be trivially copyable.  You may specialize the "
                          "template to use it with other types.");
#endif

            if (ptr) {
                std::memcpy(ptr, &t, sizeof(T));
            }

            if (advanced) {
                *advanced = sizeof(T);
            }
        }

        static Status store(const T& t, char* ptr, size_t length, size_t* advanced,
                            std::ptrdiff_t debug_offset)
        {
            if (sizeof(T) > length) {
                std::stringstream ss;
                ss << "buffer size too small to write (" << sizeof(T) << ") bytes into buffer["
                   << length << "] at offset: " << debug_offset;
                return Status(ErrorCodes::Overflow, ss.str());
            }

            unsafe_store(t, ptr, advanced);

            return Status::OK();
        }

        // It may be useful to specialize this for types that aren't natively
        // default constructible. Otherwise there's no way for us to support
        // that body of types (other than wrapping them with another tagged
        // type). Also, this guarantees value/aggregate initialization, which
        // guarantees no uninitialized memory leaks from load's, which gcc
        // otherwise can't seem to see.
        static T default_construct()
        {
            return T{};
        }

    };

    // The following dispatch functions don't just save typing, they also work
    // around what seems like template type deduction bugs (for template
    // specializations) in gcc.  I.e. for sufficiently complicated workflows (a
    // specialization for tuple), going through dispatch functions compiles on
    // gcc 4.9 and using DataType<T> does not.

    // We return a status and take an out pointer so that we can:
    //
    // 1. Run a load without returning a value (I.e. skip / validate)
    // 2. Load directly into a remote structure, rather than forcing moves of
    //    possibly large objects
    template <typename T>
    Status data_type_load(T* t, const char* ptr, size_t length, size_t* advanced,
                          std::ptrdiff_t debug_offset) {
        return DataType<T>::load(t, ptr, length, advanced, debug_offset);
    }

    template <typename T>
    Status data_type_store(const T& t, char* ptr, size_t length, size_t* advanced,
                           std::ptrdiff_t debug_offset) {
        return DataType<T>::store(t, ptr, length, advanced, debug_offset);
    }

    template <typename T>
    void data_type_unsafe_load(T* t, const char* ptr, size_t* advanced) {
        DataType<T>::unsafe_load(t, ptr, advanced);
    }

    template <typename T>
    void data_type_unsafe_store(const T& t, char* ptr, size_t* advanced) {
        DataType<T>::unsafe_store(t, ptr, advanced);
    }

    template <typename T>
    T data_type_default_construct() {
        return DataType<T>::default_construct();
    }

} // namespace mongo
