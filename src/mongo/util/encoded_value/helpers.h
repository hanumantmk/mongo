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

#include "mongo/util/encoded_value/meta/memcpy.h"
#include "mongo/util/encoded_value/meta/shortint.h"
#include "mongo/util/encoded_value/endian.h"
#include "mongo/util/encoded_value/meta/bitfield.h"
#include "mongo/util/encoded_value/pointer.h"
#include "mongo/util/encoded_value/meta/ev.h"

namespace mongo {
namespace encoded_value {
    /* There's a ton of duplication here, and it's pretty hideous.  I haven't been
    * able to find a sane way to avoid the duplication at this point.
    *
    * The basic idea is that each Impl::Pointer needs a meta class, a reference
    * type (which has the same template parameters) and const or not.  See
    * pointer.h or reference.h for a little more depth.
    *
    **/

    template <class T, enum endian::ConvertEndian ce = endian::kDefault>
    class Pointer :
        public Impl::Pointer<
            Meta::Memcpy<T, ce>,
            Impl::Reference<Meta::Memcpy<T, ce>, char*>,
            char*
        > {
    public:
        Pointer(char* in) :
            Impl::Pointer<
                Meta::Memcpy<T, ce>,
                Impl::Reference<Meta::Memcpy<T, ce>, char*>,
                char*
            >(in)
        {}
    };

    template <class T, enum endian::ConvertEndian ce = endian::kDefault>
    class CPointer :
        public Impl::Pointer<
            Meta::Memcpy<T, ce>,
            Impl::Reference<Meta::Memcpy<T, ce>, const char*>,
            const char*
        > {
    public:
        CPointer(const char* in) :
            Impl::Pointer<
                Meta::Memcpy<T, ce>,
                Impl::Reference<Meta::Memcpy<T, ce>, const char*>,
                const char*
            >(in)
        {}

        CPointer(Pointer<T, ce> in) :
            Impl::Pointer<
                Meta::Memcpy<T, ce>,
                Impl::Reference<Meta::Memcpy<T, ce>, const char*>,
                const char*
            >(in.ptr())
        {}
    };

    template <class T, enum endian::ConvertEndian ce = endian::kDefault>
    class Reference :
        public Impl::Reference<
            Meta::Memcpy<T, ce>,
            char*
        > {
    public:
        Reference(char* in) :
            Impl::Reference<
                Meta::Memcpy<T, ce>,
                char*
            >(in)
        {}

        Reference& operator=(const T& t) {
            Impl::Reference<
                Meta::Memcpy<T, ce>,
                char*
            >::operator=(t);

            return *this;
        };
    };

    template <class T, enum endian::ConvertEndian ce = endian::kDefault>
    class CReference :
        public Impl::Reference<
            Meta::Memcpy<T, ce>,
            const char*
        > {
    public:
        CReference(const char* in) :
            Impl::Reference<
                Meta::Memcpy<T, ce>,
                const char*
            >(in)
        {}

        CReference(Reference<T, ce> in) :
            Impl::Reference<
                Meta::Memcpy<T, ce>,
                const char*
            >(in.ptr())
        {}
    };

    namespace BitField {

        template <
            typename T, typename Base, int offset, int bits,
            enum endian::ConvertEndian ce = endian::kDefault
        >
        class Pointer :
            public Impl::Pointer<
                Meta::BitField<T, Base, offset, bits, ce>,
                Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, char*>,
                char*
            > {
        public:
            Pointer(char* in) :
                Impl::Pointer<
                    Meta::BitField<T, Base, offset, bits, ce>,
                    Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, char*>,
                    char*
                >(in)
            {}
        };

        template <
            typename T, typename Base, int offset, int bits,
            enum endian::ConvertEndian ce = endian::kDefault
        >
        class CPointer :
            public Impl::Pointer<
                Meta::BitField<T, Base, offset, bits, ce>,
                Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, const char*>,
                const char*
            > {
        public:
            CPointer(const char* in) :
                Impl::Pointer<
                    Meta::BitField<T, Base, offset, bits, ce>,
                    Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, const char*>,
                    const char*
                >(in)
            {}

            CPointer(Pointer<T, Base, offset, bits, ce> in) :
                Impl::Pointer<
                    Meta::BitField<T, Base, offset, bits, ce>,
                    Impl::Reference<Meta::BitField<T, Base, offset, bits, ce>, const char*>,
                    const char*
                >(in.ptr())
            {}
        };

        template <
            typename T, typename Base, int offset, int bits,
            enum endian::ConvertEndian ce = endian::kDefault
        >
        class Reference :
            public Impl::Reference<
                Meta::BitField<T, Base, offset, bits, ce>,
                char*
            > {
        public:
            Reference(char* in) :
                Impl::Reference<
                    Meta::BitField<T, Base, offset, bits, ce>,
                    char*
                >(in)
            {}

            Reference& operator=(const T& t) {
                Impl::Reference<
                    Meta::BitField<T, Base, offset, bits, ce>,
                    char*
                >::operator=(t);

                return *this;
            };
        };

        template <
            typename T, typename Base, int offset, int bits,
            enum endian::ConvertEndian ce = endian::kDefault
        >
        class CReference :
            public Impl::Reference<
                Meta::BitField<T, Base, offset, bits, ce>,
                const char*
            > {
        public:
            CReference(const char* in) :
                Impl::Reference<
                    Meta::BitField<T, Base, offset, bits, ce>,
                    const char*
                >(in)
            {}

            CReference(Reference<T, Base, offset, bits, ce> in) :
                Impl::Reference<
                    Meta::BitField<T, Base, offset, bits, ce>,
                    const char*
                >(in.ptr())
            {}
        };

    } // namespace BitField

    namespace ShortInt {

        template <
            typename T, int bytes,
            enum endian::ConvertEndian ce = endian::kDefault
        >
        class Pointer :
            public Impl::Pointer<
                Meta::ShortInt<T, bytes, ce>,
                Impl::Reference<Meta::ShortInt<T, bytes, ce>, char*>,
                char*
            > {
        public:
            Pointer(char* in) :
                Impl::Pointer<
                    Meta::ShortInt<T, bytes, ce>,
                    Impl::Reference<Meta::ShortInt<T, bytes, ce>, char*>,
                    char*
                >(in)
            {}
        };

        template <
            typename T, int bytes,
            enum endian::ConvertEndian ce = endian::kDefault
        >
        class CPointer :
            public Impl::Pointer<
                Meta::ShortInt<T, bytes, ce>,
                Impl::Reference<Meta::ShortInt<T, bytes, ce>, const char*>,
                const char*
            > {
        public:
            CPointer(const char* in) :
                Impl::Pointer<
                    Meta::ShortInt<T, bytes, ce>,
                    Impl::Reference<Meta::ShortInt<T, bytes, ce>, const char*>,
                    const char*
                >(in)
            {}

            CPointer(Pointer<T, bytes, ce> in) :
                Impl::Pointer<
                    Meta::ShortInt<T, bytes, ce>,
                    Impl::Reference<Meta::ShortInt<T, bytes, ce>, const char*>,
                    const char*
                >(in.ptr())
            {}
        };

        template <
            typename T, int bytes,
            enum endian::ConvertEndian ce = endian::kDefault
        >
        class Reference :
            public Impl::Reference<
                Meta::ShortInt<T, bytes, ce>,
                char*
            > {
        public:
            Reference(char* in) :
                Impl::Reference<
                    Meta::ShortInt<T, bytes, ce>,
                    char*
                >(in)
            {}

            Reference& operator=(const T& t) {
                Impl::Reference<
                    Meta::ShortInt<T, bytes, ce>,
                    char*
                >::operator=(t);

                return *this;
            };
        };

        template <
            typename T, int bytes,
            enum endian::ConvertEndian ce = endian::kDefault
        >
        class CReference :
            public Impl::Reference<
                Meta::ShortInt<T, bytes, ce>,
                const char*
            > {
        public:
            CReference(const char* in) :
                Impl::Reference<
                    Meta::ShortInt<T, bytes, ce>,
                    const char*
                >(in)
            {}

            CReference(Reference<T, bytes, ce> in) :
                Impl::Reference<
                    Meta::ShortInt<T, bytes, ce>,
                    const char*
                >(in.ptr())
            {}
        };

    } // namespace ShortInt

    namespace EV {

        template <class T, enum endian::ConvertEndian ce = endian::kDefault>
        class Pointer :
            public Impl::Pointer<
                Meta::EV<T, ce>,
                typename T::Reference,
                char*
            > {
        public:
            Pointer(char* in) :
                Impl::Pointer<
                    Meta::EV<T, ce>,
                    typename T::Reference,
                    char*
                >(in)
            {}
        };

        template <class T, enum endian::ConvertEndian ce = endian::kDefault>
        class CPointer :
            public Impl::Pointer<
                Meta::EV<T, ce>,
                typename T::CReference,
                const char*
            > {
        public:
            CPointer(const char* in) :
                Impl::Pointer<
                    Meta::EV<T, ce>,
                    typename T::CReference,
                    const char*
                >(in)
            {}

            CPointer(Pointer<T, ce> in) :
                Impl::Pointer<
                    Meta::EV<T, ce>,
                    typename T::CReference,
                    const char*
                >(in.ptr())
            {}
        };

    }

} // namespace encoded_value
} // namespace mongo
