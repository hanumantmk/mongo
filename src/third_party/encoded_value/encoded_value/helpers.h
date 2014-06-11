#pragma once

#include "encoded_value/meta/memcpy.h"
#include "encoded_value/meta/bitfield.h"
#include "encoded_value/pointer.h"
#include "encoded_value/meta/ev.h"

namespace encoded_value {

/* There's a ton of duplication here, and it's pretty hideous.  I haven't been
 * able to find a sane way to avoid the duplication at this point.
 *
 * The basic idea is that each Impl::Pointer needs a meta class, a reference
 * type (which has the same template parameters) and const or not.  See
 * pointer.h or reference.h for a little more depth.
 *
 * */

template <class T, enum endian::ConvertEndian ce = endian::gDefault>
class Pointer : public Impl::Pointer<Meta::Memcpy<T, ce>, Impl::Reference<Meta::Memcpy<T, ce>, char *>, char * > {
public:
    Pointer(char * in) : Impl::Pointer<Meta::Memcpy<T, ce>, Impl::Reference<Meta::Memcpy<T, ce>, char *>, char * >(in) {}
};

template <class T, enum endian::ConvertEndian ce = endian::gDefault>
class CPointer : public Impl::Pointer<Meta::Memcpy<T, ce>, Impl::Reference<Meta::Memcpy<T, ce>, const char *>, const char * > {
public:
    CPointer(const char * in) : Impl::Pointer<Meta::Memcpy<T, ce>, Impl::Reference<Meta::Memcpy<T, ce>, const char *>, const char * >(in) {}
};

template <class T, enum endian::ConvertEndian ce = endian::gDefault>
class Reference : public Impl::Reference<Meta::Memcpy<T, ce>, char * > {
public:
    Reference(char * in) : Impl::Reference<Meta::Memcpy<T, ce>, char * >(in) {}

    Reference& operator=(const T& t) {
        Impl::Reference<Meta::Memcpy<T, ce>, char * >::operator=(t);
        return *this;
    };
};

template <class T, enum endian::ConvertEndian ce = endian::gDefault>
class CReference : public Impl::Reference<Meta::Memcpy<T, ce>, const char * > {
public:
    CReference(const char * in) : Impl::Reference<Meta::Memcpy<T, ce>, const char * >(in) {}
};

namespace BitField {

template <typename T, typename Base, int offset, int bits, enum endian::ConvertEndian ce = endian::gDefault>
class Pointer : public Impl::Pointer<Meta::BitField<T, Base, offset, bits, ce>, Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, char *>, char * > {
public:
    Pointer(char * in) : Impl::Pointer<Meta::BitField<T, Base, offset, bits, ce>, Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, char *>, char * >(in) {}
};

template <typename T, typename Base, int offset, int bits, enum endian::ConvertEndian ce = endian::gDefault>
class CPointer : public Impl::Pointer<Meta::BitField<T, Base, offset, bits, ce>, Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, const char *>, const char * > {
public:
    CPointer(const char * in) : Impl::Pointer<Meta::BitField<T, Base, offset, bits, ce>, Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, const char *>, const char * >(in) {}
};

template <typename T, typename Base, int offset, int bits, enum endian::ConvertEndian ce = endian::gDefault>
class Reference : public Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, char * > {
public:
    Reference(char * in) : Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, char * >(in) {}

    Reference& operator=(const T& t) {
        Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, char * >::operator=(t);
        return *this;
    };
};

template <typename T, typename Base, int offset, int bits, enum endian::ConvertEndian ce = endian::gDefault>
class CReference : public Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, const char * > {
public:
    CReference(const char * in) : Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, const char * >(in) {}
};

}

}
