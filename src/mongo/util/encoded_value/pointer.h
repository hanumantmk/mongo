/**
 *    Copyright (C) 2014 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

/* Pointer provides a synthetic api that mimics regular pointer semantics, but
 * derferences to a special Reference<> type with special read/write semantics.
 *
 * The template parameters are used as follows:
 *
 * M - The meta class parameter wraps up all the needed metadata to actually
 *     read and write through a char *.  This encludes endian conversions
 *
 * R - The reference type to decay to.  This can't be easily included in the
 *     meta class both because we need to templatize on it and because the meta
 *     classes are orthogonal to constness
 *
 * S - The storage for the pointer.  In practice this must be char * or const
 *     char *.  I.e. a const pointer or not
 *
 * */

#include <cstddef>

#include "mongo/stdx/addressof.h"
#include "mongo/util/encoded_value/utils.h"

namespace mongo {
namespace encoded_value {
namespace Impl {

    template<class M, class R, typename S>
    class Pointer {
        S _ptr;

    public:
        typedef R Reference;

        /* We need this shim to manufacture an actual reference for
         * Pointer::operator->().  This looks sketchy, but it's actually safe.
         *
         * operator->() gets called and returns temporaries with expression
         * lifetime until a bare pointer is produced, at which point it's invoked
         * at the original level of the call stack. */
        class ReferencePointer {
        public:
            Reference r;

            ReferencePointer(S ptr) : r(ptr) {}

            Reference* operator->() {
                return stdx::addressof(r);
            }
        };

        Pointer(S ptr) : _ptr(ptr) { }

        S ptr() const {
            return _ptr;
        }

        /* We provide all of the standard pointerish integer operations */
        Pointer operator+(std::ptrdiff_t s) const {
            return Pointer(_ptr + (M::size * s));
        }

        Pointer& operator+=(std::ptrdiff_t s) {
            _ptr += (M::size * s);
            return *this;
        }

        Pointer operator-(std::ptrdiff_t s) const {
            return Pointer(_ptr - (M::size * s));
        }

        Pointer& operator-=(std::ptrdiff_t s) {
            _ptr -= (M::size * s);
            return *this;
        }

        Pointer& operator++() {
            _ptr += M::size;
            return *this;
        }

        Pointer operator++(int) {
            Pointer tmp(_ptr);
            operator++();
            return tmp;
        }

        Pointer& operator--() {
            _ptr -= M::size;
            return *this;
        }

        Pointer operator--(int) {
            Pointer tmp(_ptr);
            operator--();
            return tmp;
        }

        Pointer & operator=(const Pointer & other) {
            _ptr = other._ptr;

            return *this;
        }

        Reference operator[](std::ptrdiff_t x) const {
            return Reference(_ptr + (x * M::size));
        }

        Reference operator*() const {
            return Reference(_ptr);
        }

        std::ptrdiff_t operator-(const Pointer& right) const {
            return (this->ptr() - right->ptr()) / M::size;
        }

        /* See the shim above for how this works */
        ReferencePointer operator->() const {
            return ReferencePointer(_ptr);
        }

        operator void *() const {
            return (void *)ptr();
        }
    };

    /* we need all these paremeters to allow for comparisons between pointers with
     * varying constness.  The LR and RR types are often related, but actually
     * aren't for encoded_value generated classes, so we do need them as separate
     * parameters */
    template <class M, class LR, class RR, typename LS, typename RS>
    inline bool operator<(const Pointer<M, LR, LS>& left, const Pointer<M, RR, RS>& right) {
        return left.ptr() < right.ptr();
    }

    template <class M, class LR, class RR, typename LS, typename RS>
    inline bool operator>(const Pointer<M, LR, LS>& left, const Pointer<M, RR, RS>& right) {
        return left.ptr() > right.ptr();
    }

    template <class M, class LR, class RR, typename LS, typename RS>
    inline bool operator<=(const Pointer<M, LR, LS>& left, const Pointer<M, RR, RS>& right) {
        return left.ptr() <= right.ptr();
    }

    template <class M, class LR, class RR, typename LS, typename RS>
    inline bool operator>=(const Pointer<M, LR, LS>& left, const Pointer<M, RR, RS>& right) {
        return left.ptr() >= right.ptr();
    }

    template <class M, class LR, class RR, typename LS, typename RS>
    inline bool operator==(const Pointer<M, LR, LS>& left, const Pointer<M, RR, RS>& right) {
        return left.ptr() == right.ptr();
    }

    template <class M, class LR, class RR, typename LS, typename RS>
    inline bool operator!=(const Pointer<M, LR, LS>& left, const Pointer<M, RR, RS>& right) {
        return left.ptr() != right.ptr();
    }

    /* These comparison templates allow for comparison with null, I.e. == 0 or ==
     * NULL. We use some typedef magic (static asserts) to prevent comparisons in
     * other integer contexts.  There's no real way to avoid SFINAE, so the
     * messages aren't the most helpful, but at least broken callers won't compile
     * */
    template <class M, class R, typename S, int i>
    inline bool operator==(const Pointer<M, R, S>& left, int right) {
        typedef char only_specialize_null[right ? -1 : 1];

        return !left.ptr();
    }

    template <class M, class R, typename S, int i>
    inline bool operator!=(const Pointer<M, R, S>& left, int right) {
        typedef char only_specialize_null[right ? -1 : 1];

        return left.ptr();
    }

    template <class M, class R, typename S, int i>
    inline bool operator==(int left, const Pointer<M, R, S>& right) {
        typedef char only_specialize_null[left ? -1 : 1];

        return !right.ptr();
    }

    template <class M, class R, typename S, int i>
    inline bool operator!=(int left, const Pointer<M, R, S>& right) {
        typedef char only_specialize_null[left ? -1 : 1];

        return right.ptr();
    }

} // namespace Impl
} // namespace encoded_value
} // namespace mongo
