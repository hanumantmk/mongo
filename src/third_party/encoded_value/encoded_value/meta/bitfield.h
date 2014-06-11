#pragma once

/* BitField provides safe access to bitfields by memcpy'ing a base type, then
 * doing the bit math for you
 *
 * Template params are as follows:
 *
 * T - The type to return
 *
 * Base - The underlying integer type we're reading in and out of
 * 
 * offset - the number of bits we have to shift to line up the T value in the
 *          lower order bits of Base
 *
 * bits - the number of bits we'll represent T with
 *
 * ce - endian conversion
 *
 * */

#include "encoded_value/reference.h"
#include "encoded_value/endian.h"
#include <cstring>

namespace encoded_value {
namespace Meta {

template <typename T, typename Base, int offset, int bits, enum endian::ConvertEndian ce>
class BitField {
public:
    static const std::size_t size = sizeof(Base);
    typedef T type;

    /* write a t to some Base that's in memory.  This operation only touches
     * the parts of Base refered to by offset and bits */
    static inline void writeTo(const T& t, void * ptr) {
        /* grab b out of memory and swab it if needed */
        Base b;
        std::memcpy(&b, ptr, sizeof(Base));

        if (endian::needsSwab<Base, ce>::result) {
            b = endian::swab<Base, ce>(b);
        }

        /* mask out the bits we're responsible for */
        b &= ~(((1 << bits) - 1) << offset);

        /* or in t */
        b |= t << offset;

        /* write everything back to memory */
        if (endian::needsSwab<Base, ce>::result) {
            b = endian::swab<Base, ce>(b);
        }

        std::memcpy(ptr, &b, sizeof(Base));
    }

    /* copy from some Base in memory and take the relevant bits and assign them
     * to t */
    static inline void readFrom(T& t, const void * ptr) {
        Base b;

        /* grab and swab Base from memory */
        std::memcpy(&b, ptr, sizeof(Base));
        if (endian::needsSwab<Base, ce>::result) {
            b = endian::swab<Base, ce>(b);
        }

        /* push the relevant bits into t */
        t = (b >> offset) & ((1 << bits) - 1);
    }
};

}
}
