#pragma once

#include <algorithm>
#include <stdint.h>

/* These should get pulled out into some host provided defines */

/* mongo specific for now */

#define ENCODED_VALUE_ENDIAN_HOST 1234
#define ENCODED_VALUE_ENDIAN_DEFAULT encoded_value::endian::Little

namespace encoded_value {
namespace endian {

enum ConvertEndian {
    Noop = 0,
    Big = 1,
    Little = 2,
};

#ifdef ENCODED_VALUE_ENDIAN_DEFAULT
const enum ConvertEndian gDefault = ENCODED_VALUE_ENDIAN_DEFAULT;
#else
const enum ConvertEndian gDefault = Noop;
#endif

/* basically, needsSwab<T, ce>::result == true for integers and floats that are
 * opposite endian, assuming you actually care about their endianness (a
 * ConvertEndian other than Noop was provided) */

template<typename T, enum ConvertEndian e>
class needsSwab {
public:
    static const bool result = false;
};

#pragma push_macro("NEEDS_SWAB")

#if ENCODED_VALUE_ENDIAN_HOST == 4321
#define NEEDS_SWAB(type) \
    template<> \
    class needsSwab<type, Little> { \
    public: \
        static const bool result = true; \
    };
#elif ENCODED_VALUE_ENDIAN_HOST == 1234
#define NEEDS_SWAB(type) \
    template<> \
    class needsSwab<type, Big> { \
    public: \
        static const bool result = true; \
    };
#else
#  error "Unknown host endianness"
#endif

NEEDS_SWAB(int16_t)
NEEDS_SWAB(uint16_t)
NEEDS_SWAB(int32_t)
NEEDS_SWAB(uint32_t)
NEEDS_SWAB(int64_t)
NEEDS_SWAB(uint64_t)
NEEDS_SWAB(double)
NEEDS_SWAB(float)

#undef NEEDS_SWAB
#pragma pop_macro("NEEDS_SWAB")

/* only providing a simplistic non-high performance swab at this point */

template<typename T, enum ConvertEndian ce>
inline T swab(T t) {
    char * front, * back;

    if (! needsSwab<T, ce>::result) {
        return t;
    }

    front = (char *)&t;
    back = front + sizeof(T) - 1;

    std::reverse(front, back);

    return t;
}

}
}
