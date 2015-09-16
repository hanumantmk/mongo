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

namespace mongo {

/**
 * Implements a stack on top of a contiguous allocation.
 *
 * This is useful for manipulating stacks of types which are non-movable and
 * non-copyable (and thus cannot be put into standard containers).
 *
 * It offers specialization through custom allocators to allow for specialized
 * use (like on stack allocation) in addition to the standard heap.
 */
template <typename T, class Allocator = std::allocator<T>>
class ContiguousStack {
public:
    // Boiler plate typedefs
    using value_type = T;
    using allocator_type = Allocator;
    using size_type = std::size_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = const typename std::allocator_traits<Allocator>::pointer&;
    using const_pointer = const typename std::allocator_traits<Allocator>::const_pointer&;

    /**
     * A default constructed stack has the default allocator and no allocation.
     * It's size and capacity are 0, so most operations are invalid.
     */
    ContiguousStack() = default;

    /**
     * Creates a stack that can support a maximum of n elements
     */
    ContiguousStack(size_type n, const Allocator& allocator = Allocator())
        : _allocator(allocator),
          _data(std::allocator_traits<Allocator>::allocate(_allocator, n)),
          _reserved(n) {}

    ~ContiguousStack() {
        cleanup();
    }

    /**
     * Stacks are non-copyable
     */
    ContiguousStack(const ContiguousStack&) = delete;
    ContiguousStack& operator=(const ContiguousStack&) = delete;

    /**
     * Stacks are movable
     */
    ContiguousStack(ContiguousStack&& other) {
        *this = std::move(other);
    }

    ContiguousStack& operator=(ContiguousStack&& other) {
        cleanup();

        _allocator = other._allocator;
        _data = other._data;
        _reserved = other._reserved;
        _size = other._size;

        other._data = nullptr;
        other._reserved = nullptr;
        other._size = nullptr;
    }

    allocator_type get_allocator() const {
        return _allocator;
    }

    template <typename... Args>
    void emplace(Args&&... args) {
        invariant(_size <= _reserved);
        _size++;

        std::allocator_traits<Allocator>::construct(
            _allocator, &top(), std::forward<Args>(args)...);
    }

    void pop() {
        invariant(_size > 0);
        std::allocator_traits<Allocator>::destroy(_allocator, &top());
        _size--;
    }

    const_reference top() const {
        invariant(_size > 0);
        return _data[_size - 1];
    }

    reference top() {
        invariant(_size > 0);
        return _data[_size - 1];
    }

    size_type size() const {
        return _size;
    }

    bool empty() const {
        return _size == 0;
    }

    size_type capacity() const {
        return _reserved;
    }

    void swap(ContiguousStack& other) {
        using std::swap;

        swap(_allocator, other._allocator);
        swap(_data, other._data);
        swap(_reserved, other._reserved);
        swap(_size, other._size);
    }

private:
    void cleanup() {
        while (size()) {
            pop();
        }

        if (_data)
            std::allocator_traits<Allocator>::deallocate(_allocator, _data, _reserved);

        _data = nullptr;
        _reserved = 0;
        _size = 0;
    }

private:
    Allocator _allocator;
    T* _data = nullptr;
    size_type _reserved = 0;
    size_type _size = 0;
};

template <typename T, typename Allocator>
void swap(ContiguousStack<T, Allocator>& lhs, ContiguousStack<T, Allocator>& rhs) {
    lhs.swap(rhs);
}

}  // namespace mongo
