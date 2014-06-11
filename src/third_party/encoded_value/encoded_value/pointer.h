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
#include "encoded_value/utils.h"

namespace encoded_value {
namespace Impl {

template<class M, class R, typename S>
class Pointer {
    S _ptr;

    /* This stuff takes care of providing the safe bool idiom for pointers.
     * I.e. usefulness in if (ptr) without automatic upgrade into other
     * integral contexts.  Basically, you provide an operator T() overload for
     * a really specific pointer type as an alternative to operator bool() or
     * operator void *() */
    typedef void (Pointer::*bool_type)() const;
    void _magic_bool_func() const {}

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

            ReferencePointer(char * const ptr) : r(ptr) {}

            const Reference * operator->() const {
                return addressof(r);
            }

            Reference * operator->() {
                return addressof(r);
            }
    };

    Pointer(S ptr) : _ptr(ptr) { }

    S ptr() const {
        return _ptr;
    }

    /* We provide all of the standard pointerish integer operations */
    Pointer operator+(std::size_t s) const {
        return Pointer(_ptr + (M::size * s));
    }

    Pointer& operator+=(std::size_t s) {
        _ptr += (M::size * s);
        return *this;
    }

    Pointer operator-(std::size_t s) const {
        return Pointer(_ptr - (M::size * s));
    }

    Pointer& operator-=(std::size_t s) {
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

    Reference operator[](int x) {
        return Reference(_ptr + (x * M::size));
    }

    const Reference operator[](int x) const {
        return Reference(_ptr + (x * M::size));
    }

    Reference operator*() {
        return Reference(_ptr);
    }

    const Reference operator*() const {
        return Reference(_ptr);
    }

    /* See the shim above for how this works */
    ReferencePointer operator->() {
        return ReferencePointer(_ptr);
    }

    const ReferencePointer operator->() const {
        return ReferencePointer(_ptr);
    }

    /* here's the safe bool magic */
    operator bool_type() const {
        return ptr() ? &Pointer::_magic_bool_func : 0;
    }
};

/* we need all these paremeters to allow for comparisons between pointers with
 * varying constness.  The LR and RR types are often related, but actually
 * aren't for encoded_value generated classes, so we do need them as separate
 * parameters */
template <class M, class LR, class RR, typename LP, typename RP>
inline bool operator<(const Pointer<M, LR, LP> & left, const Pointer<M, RR, RP> & right) {
    return left.ptr() < right.ptr();
}

template <class M, class LR, class RR, typename LP, typename RP>
inline bool operator>(const Pointer<M, LR, LP> & left, const Pointer<M, RR, RP> & right) {
    return left.ptr() > right.ptr();
}

template <class M, class LR, class RR, typename LP, typename RP>
inline bool operator<=(const Pointer<M, LR, LP> & left, const Pointer<M, RR, RP> & right) {
    return left.ptr() <= right.ptr();
}

template <class M, class LR, class RR, typename LP, typename RP>
inline bool operator>=(const Pointer<M, LR, LP> & left, const Pointer<M, RR, RP> & right) {
    return left.ptr() >= right.ptr();
}

template <class M, class LR, class RR, typename LP, typename RP>
inline bool operator==(const Pointer<M, LR, LP> & left, const Pointer<M, RR, RP> & right) {
    return left.ptr() == right.ptr();
}

template <class M, class LR, class RR, typename LP, typename RP>
inline bool operator!=(const Pointer<M, LR, LP> & left, const Pointer<M, RR, RP> & right) {
    return left.ptr() != right.ptr();
}

/* These comparison templates allow for comparison with null, I.e. == 0 or ==
 * NULL. We use some typedef magic (static asserts) to prevent comparisons in
 * other integer contexts.  There's no real way to avoid SFINAE, so the
 * messages aren't the most helpful, but at least broken callers won't compile
 * */
template <class M, class R, typename S, int i>
inline bool operator==(const Pointer<M, R, S> & left, int right) {
    typedef char only_specialize_null[right ? -1 : 1];

    return !left.ptr();
}

template <class M, class R, typename S, int i>
inline bool operator!=(const Pointer<M, R, S> & left, int right) {
    typedef char only_specialize_null[right ? -1 : 1];

    return left.ptr();
}

template <class M, class R, typename S, int i>
inline bool operator==(int left, const Pointer<M, R, S> & right) {
    typedef char only_specialize_null[left ? -1 : 1];

    return !right.ptr();
}

template <class M, class R, typename S, int i>
inline bool operator!=(int left, const Pointer<M, R, S> & right) {
    typedef char only_specialize_null[left ? -1 : 1];

    return right.ptr();
}

}
}
