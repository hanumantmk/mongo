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

#include <cstddef>
#include <memory>

#include "mongo/base/data_range.h"
#include "mongo/base/data_range_cursor.h"

namespace mongo {

/**
 * The backing memory for a AllocOnlyPoolAllocator.
 *
 * The Pool holds a data range of bytes which it hands out through allocate.
 * Memory once removed can only be reclaimed via clear(), which assumes that
 * all previous allocations have already been destroyed.
 */
class AllocOnlyPoolAllocatorPool {
public:
    AllocOnlyPoolAllocatorPool(DataRange dr) : _storage(dr) {
        clear();
    }

    /**
     * Allocate aligned memory for n T's
     */
    template <typename T>
    T* allocate(std::size_t n) {
        if (std::align(std::alignment_of<T>::value, sizeof(T) * n, _ptr, _remaining)) {
            auto result = reinterpret_cast<T*>(_ptr);
            _ptr = static_cast<char*>(_ptr) + sizeof(T) * n;
            _remaining -= sizeof(T) * n;

            return result;
        }

        throw std::bad_alloc();
    }

    /**
     * Clear the pool, making allocate available to return previously allocated memory again
     */
    void clear() {
        _ptr = static_cast<void*>(const_cast<char*>(_storage.data()));
        _remaining = _storage.length();
    }

    /**
     * The remaining number of bytes in the pool
     */
    std::size_t remaining() const {
        return _remaining;
    }

private:
    DataRange _storage;

    void* _ptr;
    std::size_t _remaining;
};

/**
 * A minimal allocator which can be used with standard containers.
 *
 * It hands out new memory from a linear range owned by the pool it is
 * constructed with. Deallocation does nothing, so this is useful only for
 * objects with one global point of destruction.
 */
template <typename T>
struct AllocOnlyPoolAllocator {
    using value_type = T;

    AllocOnlyPoolAllocator() = default;

    AllocOnlyPoolAllocator(AllocOnlyPoolAllocatorPool* pool) : _pool(pool) {}

    template <typename U>
    AllocOnlyPoolAllocator(const AllocOnlyPoolAllocator<U>& other)
        : _pool(other._pool) {}

    T* allocate(std::size_t n) {
        invariant(_pool);
        return _pool->allocate<T>(n);
    }

    void deallocate(T* p, std::size_t n) {}

    AllocOnlyPoolAllocatorPool* _pool = nullptr;
};

template <typename T, typename U>
bool operator==(const AllocOnlyPoolAllocator<T>& lhs, const AllocOnlyPoolAllocator<U>& rhs) {
    return lhs._pool == rhs._pool;
}

template <typename T, typename U>
bool operator!=(const AllocOnlyPoolAllocator<T>& lhs, const AllocOnlyPoolAllocator<U>& rhs) {
    !(lhs == rhs);
}

}  // namespace mongo
