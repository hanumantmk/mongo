#pragma once

/* Reference provides a synthetic api that mimics regular reference semantics, but
 * has a overloads for assignment and can decay to a real underlying type.  The
 * basic idea is that you can use one of these just like the base type, but
 * reads and writes will be strategized appropriately.
 *
 * The template parameters are used as follows:
 * 
 * M - The meta class parameter wraps up all the needed metadata to actually
 *     read and write through a char *.  This encludes endian conversions
 *
 * S - The storage for the pointer.  In practice this must be char * or const
 *     char *.  I.e. a const pointer or not
 *
 * */

namespace encoded_value {
namespace Impl {

template <class M, class R, typename S> class Pointer;

template <class M, typename S>
class Reference {
typedef typename M::type T;
public:

    /* Entry point for all writes */
    Reference& operator=( const T& other ) {
        M::writeTo(other, _ptr);
        
        return *this;
    }

    /* all reads */
    operator T() const {
        T t;
        M::readFrom(t, _ptr);

        return t;
    }

    /* here come all the interesting user overloadable assignments... */
    Reference& operator+=(const T& other) {
        operator=(operator T() + other);

        return *this;
    }

    Reference& operator-=(const T& other) {
        operator=(operator T() - other);

        return *this;
    }

    Reference& operator*=(const T& other) {
        operator=(operator T() * other);

        return *this;
    }

    Reference& operator/=(const T& other) {
        operator=(operator T() / other);

        return *this;
    }

    Reference& operator%=(const T& other) {
        operator=(operator T() % other);

        return *this;
    }

    Reference& operator&=(const T& other) {
        operator=(operator T() & other);

        return *this;
    }

    Reference& operator|=(const T& other) {
        operator=(operator T() | other);

        return *this;
    }

    Reference& operator^=(const T& other) {
        operator=(operator T() ^ other);

        return *this;
    }

    Reference& operator<<=(const T& other) {
        operator=(operator T() << other);

        return *this;
    }

    Reference& operator>>=(const T& other) {
        operator=(operator T() >> other);

        return *this;
    }

    Reference& operator++() {
        operator+=(1);

        return *this;
    }

    T operator++(int) {
        T tmp = operator T();
        operator++();
        return tmp;
    }

    Reference& operator--() {
        operator-=(1);

        return *this;
    }

    T operator--(int) {
        T tmp = operator T();
        operator--();
        return tmp;
    }

    S ptr() const {
        return _ptr;
    }

    Reference(const Reference& other) {
        _ptr = other._ptr;
    }

    Reference(S ptr) : _ptr(ptr) { }

    /* This is a little sketchy, but it allows for offseting in generated
     * structs, or into arrays of Ref's.  There's a util::addressof to work
     * around it as necessary */
    Impl::Pointer<M, Reference, S> operator&() const {
        return Impl::Pointer<M, Reference, S>(_ptr);
    }

private:
    Reference() {}
    Reference& operator=(const Reference& ) {}

    S _ptr;
};

}
}
