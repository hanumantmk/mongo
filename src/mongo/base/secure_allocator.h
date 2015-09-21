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

#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

namespace mongo {

namespace secure_allocator_details {

void* allocate(std::size_t bytes);
void deallocate(void* ptr, std::size_t bytes);

}  // namespace secure_allocator_details

/**
 * Provides a secure allocator for trivially copyable types. By secure we mean
 * memory that will be zeroed on free and locked out of paging while in memory
 * (to prevent it from being written to disk).
 *
 * While this type can be used with any allocator aware container, it should be
 * considered whether either of the two named specializations below are
 * sufficient (a string and a vector). The allocations out of this container
 * are quite expensive, so one should endeavor to use containers which make
 * few, contiguous allocations where possible.
 */
template <typename T>
struct SecureAllocator {
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

/**
 * We only support trivially copyable types to avoid situations where the
 * SecureAllocator is used in containers with complex types that do their
 * own allocation. I.e. one could otherwise have a:
 *
 * std::vector<std::string, SecureAllocator<std::string>>
 *
 * where the vectors were stored securely, but the strings spilled to the
 * heap
*/
#ifdef MONGO_CONFIG_HAVE_STD_IS_TRIVIALLY_COPYABLE
    static_assert(std::is_trivially_copyable<T>::value,
                  "SecureAllocator can only be used with trivially copyable types");
#endif

    template <typename U>
    struct rebind {
        using other = SecureAllocator<U>;
    };

    SecureAllocator() = default;

    template <typename U>
    SecureAllocator(const SecureAllocator<U>& other) {}

    pointer allocate(size_type n) {
        return static_cast<pointer>(secure_allocator_details::allocate(sizeof(value_type) * n));
    }

    void deallocate(pointer ptr, size_type n) {
        return secure_allocator_details::deallocate(static_cast<void*>(ptr),
                                                    sizeof(value_type) * n);
    }
};

template <typename T, typename U>
bool operator==(const SecureAllocator<T>& lhs, const SecureAllocator<U>& rhs) {
    return true;
}

template <typename T, typename U>
bool operator!=(const SecureAllocator<T>& lhs, const SecureAllocator<U>& rhs) {
    return !(lhs == rhs);
}

template <typename T>
using SecureVector = std::vector<T, SecureAllocator<T>>;

using SecureString = std::basic_string<char, std::char_traits<char>, SecureAllocator<char>>;

}  // namespace mongo
