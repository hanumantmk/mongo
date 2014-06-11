#pragma once

/* Memcpy provides safe access to values by memcpy'ing them
 *
 * Template params are as follows:
 *
 * T - The type to return
 *
 * ce - endian conversion
 *
 * */

#include "encoded_value/reference.h"
#include "encoded_value/endian.h"
#include <cstring>

namespace encoded_value {
namespace Meta {

template <typename T, enum endian::ConvertEndian ce>
class Memcpy {
public:
    static const std::size_t size = sizeof(T);
    typedef T type;

    static inline void writeTo(const T& t, void * ptr) {
        if (endian::needsSwab<T, ce>::result) {
            T tmp = endian::swab<T, ce>(t);
            std::memcpy(ptr, &tmp, size);
        } else {
            std::memcpy(ptr, &t, size);
        }
    }

    static inline void readFrom(T& t, const void * ptr) {
        std::memcpy(&t, ptr, size);

        if (endian::needsSwab<T, ce>::result) {
            t = endian::swab<T, ce>(t);
        }
    }
};

}
}
