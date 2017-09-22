/**
 * Copyright (C) 2017 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include <bitset>
#include <type_traits>

#include "mongo/stdx/type_traits.h"

#pragma once

#define ENUM_BITSET_GENERATE_CLASS(name, ...)                          \
    using name = EnumBitset<enum_bitset_details::Config<__VA_ARGS__>>; \
    name operator|(typename name::Configuration::type lhs,             \
                   typename name::Configuration::type rhs) {           \
        return name(lhs) | name(rhs);                                  \
    }                                                                  \
    name operator&(typename name::Configuration::type lhs,             \
                   typename name::Configuration::type rhs) {           \
        return name(lhs) & name(rhs);                                  \
    }                                                                  \
    name operator^(typename name::Configuration::type lhs,             \
                   typename name::Configuration::type rhs) {           \
        return name(lhs) ^ name(rhs);                                  \
    }

namespace mongo {

namespace enum_bitset_details {

template <typename Enum, Enum v>
struct Wrapper {
    constexpr static size_t value = static_cast<size_t>(v);
};

template <typename Head, typename... Tail>
struct FoldBitOrHelper {
    constexpr static size_t value = (1ull << Head::value) | FoldBitOrHelper<Tail...>::value;
};

template <>
struct FoldBitOrHelper<void> {
    constexpr static size_t value = 0;
};

template <typename Enum, Enum... Enums>
using FoldBitOr = FoldBitOrHelper<Wrapper<Enum, Enums>..., void>;

template <typename Enum, size_t Size, Enum... restrictTo>
struct Config : public std::true_type {
    using type = Enum;
    constexpr static size_t size = Size;
    constexpr static size_t restrictionMask =
        sizeof...(restrictTo) ? FoldBitOr<Enum, restrictTo...>::value : ~0ull;
};

template <typename Enum, size_t Size, Enum... restrictTo>
constexpr size_t Config<Enum, Size, restrictTo...>::size;

template <typename Enum, size_t Size, Enum... restrictTo>
constexpr size_t Config<Enum, Size, restrictTo...>::restrictionMask;

}  // namespace enum_bitset_details

/**
 * Provides a std::bitset style interface where the index keys are enum values instead of regular
 * integers.  This provides a type safe way to do bitwise operations amongst that set of offsets.
 *
 * Use of this type requires the existence of a free function enumBitsetSize(Enum), which returns a
 * config trait (an appropriate type can be made with the enum_bitset_details::Config helper).  This
 * free function will be engaged via ADL.
 */
template <typename Configuration_>
class EnumBitset : private std::bitset<Configuration_::size> {
    using T = typename Configuration_::type;
    static_assert((0 < Configuration_::size) && (Configuration_::size <= 64),
                  "EnumBitsets must have between 0 and 64 members");
    static_assert(std::is_enum<T>::value, "EnumBitset is only compatible with enums");

    using Super = std::bitset<Configuration_::size>;
    using Underlying = std::underlying_type_t<T>;
    static constexpr Super allRestrictedOff = Super{~Configuration_::restrictionMask};

    static constexpr Underlying fromEnum(T t) {
        return static_cast<Underlying>(t);
    }

public:
    using Configuration = Configuration_;
    using typename Super::reference;

    /**
     * Special tag type which allows construction of an EnumBitset from a raw value.  Useful for
     * loading from networking/disk.
     */
    struct FromRawBytes {
        constexpr explicit FromRawBytes(unsigned long long val) : val(val) {}
        unsigned long long val;
    };

    constexpr EnumBitset() = default;

    explicit constexpr EnumBitset(FromRawBytes rawBytes)
        : Super(rawBytes.val & Configuration::restrictionMask) {}

    constexpr EnumBitset(T pos) : EnumBitset(FromRawBytes{1ull << fromEnum(pos)}) {}

    friend bool operator==(const EnumBitset& lhs, const EnumBitset& rhs) {
        return static_cast<const Super&>(lhs) == static_cast<const Super&>(rhs);
    };

    friend bool operator!=(const EnumBitset& lhs, const EnumBitset& rhs) {
        return static_cast<const Super&>(lhs) != static_cast<const Super&>(rhs);
    };

    constexpr bool operator[](T pos) const {
        return Super::operator[](fromEnum(pos));
    }

    reference operator[](T pos) {
        return Super::operator[](fromEnum(pos));
    }

    bool test(T pos) const {
        return Super::test(fromEnum(pos));
    }

    bool all() const {
        return (allRestrictedOff | *this).Super::all();
    }

    using Super::any;
    using Super::none;
    using Super::count;
    using Super::size;

    EnumBitset& operator&=(const EnumBitset& other) {
        static_cast<Super&>(*this) &= static_cast<const Super&>(other);
        return *this;
    }

    EnumBitset& operator|=(const EnumBitset& other) {
        static_cast<Super&>(*this) |= static_cast<const Super&>(other);
        return *this;
    }

    EnumBitset& operator^=(const EnumBitset& other) {
        static_cast<Super&>(*this) ^= static_cast<const Super&>(other);
        return *this;
    }

    EnumBitset operator~() const {
        return EnumBitset(*this).flip();
    }

    EnumBitset& set() noexcept {
        Super::set();
        return *this;
    }

    EnumBitset& set(T pos, bool value = true) {
        Super::set(fromEnum(pos), value);
        return *this;
    }

    EnumBitset& reset() noexcept {
        Super::reset();
        return *this;
    }

    EnumBitset& reset(T pos) noexcept {
        Super::reset(fromEnum(pos));
        return *this;
    }

    EnumBitset& flip() noexcept {
        static_cast<Super&>(*this) |= allRestrictedOff;
        Super::flip();
        return *this;
    }

    EnumBitset& flip(T pos) noexcept {
        Super::flip(fromEnum(pos));
        return *this;
    }

    friend EnumBitset operator&(const EnumBitset& lhs, const EnumBitset& rhs) noexcept {
        return EnumBitset(lhs) &= rhs;
    }

    friend EnumBitset operator|(const EnumBitset& lhs, const EnumBitset& rhs) noexcept {
        return EnumBitset(lhs) |= rhs;
    }

    friend EnumBitset operator^(const EnumBitset& lhs, const EnumBitset& rhs) noexcept {
        return EnumBitset(lhs) ^= rhs;
    }

    using Super::to_string;
    using Super::to_ulong;
    using Super::to_ullong;
};

template <typename Configuration>
constexpr std::bitset<Configuration::size> EnumBitset<Configuration>::allRestrictedOff;

}  // namespace mongo
