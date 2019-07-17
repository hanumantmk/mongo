#include <cstdint>
#include <cstring>

#pragma once

namespace cexpr {

class data_view {
public:
    CONSTEXPR data_view(uint8_t* b) : bytes(b) {}

    CONSTEXPR void store_le_uint16(uint16_t v) {
        bytes[0] = v & 0xFF;
        bytes[1] = (v >> 8) & 0xFF;
    }

    CONSTEXPR void store_le_uint32(uint32_t v) {
        bytes[0] = v & 0xFF;
        bytes[1] = (v >> 8) & 0xFF;
        bytes[2] = (v >> 16) & 0xFF;
        bytes[3] = (v >> 24) & 0xFF;
    }

    CONSTEXPR void store_le_uint64(uint64_t v) {
        bytes[0] = v & 0xFFul;
        bytes[1] = (v >> 8) & 0xFFul;
        bytes[2] = (v >> 16) & 0xFFul;
        bytes[3] = (v >> 24) & 0xFFul;
        bytes[4] = (v >> 32) & 0xFFul;
        bytes[5] = (v >> 40) & 0xFFul;
        bytes[6] = (v >> 48) & 0xFFul;
        bytes[7] = (v >> 56) & 0xFFul;
    }

    CONSTEXPR void store_le_int16(int16_t v) {
        uint16_t b = 0;

        if (v > 0) {
            b = v;
        } else {
            b = 0xFFFF + v + 1;
        }

        store_le_uint16(b);
    }

    CONSTEXPR void store_le_int32(int32_t v) {
        uint32_t b = 0;

        if (v > 0) {
            b = v;
        } else {
            b = 0xFFFFFFFF + v + 1;
        }

        store_le_uint32(b);
    }

    CONSTEXPR void store_le_int64(int64_t v) {
        uint64_t b = 0;

        if (v > 0) {
            b = v;
        } else {
            b = 0xFFFFFFFFFFFFFFFFUL + v + 1;
        }

        store_le_uint64(b);
    }

    template <typename Float, typename Int, typename UInt, std::size_t exp_bits>
    CONSTEXPR UInt store_le_impl(Float v) {
        std::size_t precision = (sizeof(Float) * 8) - (1 + exp_bits);

        UInt bytes = 0;

        if (v != 0.0) {
            bool is_negative = v < 0;

            if (is_negative) {
                v = -v;
            }

            UInt mantissa = 0;
            {
                Float tmp = v;

                while (Float(Int(tmp)) != tmp) {
                    tmp *= 2;
                }

                while (Float(Int(tmp)) == tmp) {
                    tmp /= 2;
                }
                tmp *= 2;

                mantissa = tmp;
            }

            UInt exp = (1ul << (exp_bits - 1)) - 1;
            {
                Float tmp = v;

                while (tmp > 1) {
                    tmp /= 2;
                    exp++;
                }

                while (tmp < 1) {
                    tmp *= 2;
                    exp--;
                }
            }

            int mantissa_bits = 0;
            {
                UInt tmp = mantissa;

                while (tmp) {
                    mantissa_bits++;
                    tmp >>= 1;
                }
            }

            mantissa_bits--;

            bytes = mantissa - (1ul << mantissa_bits);

            bytes <<= (precision - mantissa_bits);

            exp <<= precision;

            bytes |= exp;

            if (is_negative) {
                bytes |= 1ul << ((sizeof(Float) * 8) - 1);
            }
        }

        return bytes;
    }

    CONSTEXPR void store_le_double(double d) {
        store_le_uint64(store_le_impl<double, int64_t, uint64_t, 11>(d));
    }

    CONSTEXPR void store_le_float(float d) {
        store_le_uint32(store_le_impl<float, int32_t, uint32_t, 8>(d));
    }

private:
    uint8_t* bytes;
};
}
