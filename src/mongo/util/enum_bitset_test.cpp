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

#include "mongo/platform/basic.h"

#include "mongo/unittest/unittest.h"

#include <tuple>

#include "mongo/util/enum_bitset.h"

namespace mongo {
namespace {

enum class BasicEnumClass {
    Foo,
    Bar,
    Baz,
};
ENUM_BITSET_GENERATE_CLASS(BasicEnumClassBitset, BasicEnumClass, 3)

enum class FullRestrictionEnumClass {
    Foo,
    Bar,
    Baz,
};
ENUM_BITSET_GENERATE_CLASS(FullRestrictionEnumClassBitset,
                           FullRestrictionEnumClass,
                           3,
                           FullRestrictionEnumClass::Foo,
                           FullRestrictionEnumClass::Bar,
                           FullRestrictionEnumClass::Baz)

enum class PartialRestrictionEnumClass {
    Foo = 0,
    Bar = 3,
    Baz = 7,
};
ENUM_BITSET_GENERATE_CLASS(PartialRestrictionEnumClassBitset,
                           PartialRestrictionEnumClass,
                           8,
                           PartialRestrictionEnumClass::Foo,
                           PartialRestrictionEnumClass::Bar,
                           PartialRestrictionEnumClass::Baz)

struct Basic {
    enum Enum {
        Foo,
        Bar,
        Baz,
    };
};
ENUM_BITSET_GENERATE_CLASS(BasicEnumBitset, Basic::Enum, 3)

enum class Unannotated {
    Foo,
    Bar,
    Baz,
};

// Utility type for guiding overload resolution in disambiguating between sfinae'd functions.
template <size_t N>
struct Rank : Rank<N - 1> {};
template <>
struct Rank<0> {};

// Generates an expression sfinae trait
#define ENUM_BITSET_CAN_EVAL(name, ...)                                     \
    template <typename Lhs, typename Rhs>                                   \
    constexpr auto name##EvalHelper(Lhs lhs, Rhs rhs, Rank<1>)              \
        ->decltype(__VA_ARGS__, std::true_type{}) {                         \
        return {};                                                          \
    };                                                                      \
    template <typename Lhs, typename Rhs>                                   \
    constexpr std::false_type name##EvalHelper(Lhs lhs, Rhs rhs, Rank<0>) { \
        return {};                                                          \
    };                                                                      \
    template <typename Lhs, typename Rhs>                                   \
    constexpr auto name(Lhs lhs, Rhs rhs) {                                 \
        return name##EvalHelper(lhs, rhs, Rank<2>{});                       \
    };

ENUM_BITSET_CAN_EVAL(canConstructFrom, Lhs(rhs))

ENUM_BITSET_CAN_EVAL(canBitOps, lhs | rhs, lhs& rhs, lhs ^ rhs)

ENUM_BITSET_CAN_EVAL(
    canInvokeWith, lhs[rhs], lhs.test(rhs), lhs.set(rhs), lhs.reset(rhs), lhs.flip(rhs))

// Tests for some (C)ontaing type and an (E)num
template <typename C, typename EBitSet, typename OtherC>
void testEB() {
    using Configuration = typename EBitSet::Configuration;

    static_assert(canBitOps(C::Foo, C::Bar), "Can bit op annotated");
    static_assert(!canBitOps(C::Foo, OtherC::Foo), "Can't bit ops unrelated");

    static_assert(canConstructFrom(EBitSet{}, C::Foo), "Can construct from related");
    static_assert(!canConstructFrom(EBitSet{}, 1ul), "Can't construct from non-enum");
    static_assert(!canConstructFrom(EBitSet{}, OtherC::Foo), "Can't construct from unrelated");

    static_assert(canInvokeWith(EBitSet{}, C::Foo), "Can invoke related");
    static_assert(!canInvokeWith(EBitSet{}, 1ul), "Can't invoke non-enum");
    static_assert(!canInvokeWith(EBitSet{}, OtherC::Foo), "Can't invoke unrelated");

    const size_t foo = 1 << static_cast<size_t>(C::Foo);
    const size_t bar = 1 << static_cast<size_t>(C::Bar);
    const size_t baz = 1 << static_cast<size_t>(C::Baz);

    ASSERT_EQUALS((C::Foo | C::Bar).to_ulong(), (foo | bar));
    ASSERT_EQUALS((C::Foo & C::Bar).to_ulong(), (foo & bar));
    ASSERT_EQUALS((C::Foo ^ C::Bar).to_ulong(), (foo ^ bar));
    ASSERT_EQUALS((EBitSet{C::Foo} | C::Bar).to_ulong(), (foo | bar));
    ASSERT_EQUALS((EBitSet{C::Foo} & C::Bar).to_ulong(), (foo & bar));
    ASSERT_EQUALS((EBitSet{C::Foo} ^ C::Bar).to_ulong(), (foo ^ bar));
    ASSERT_EQUALS((C::Foo | EBitSet{C::Bar}).to_ulong(), (foo | bar));
    ASSERT_EQUALS((C::Foo & EBitSet{C::Bar}).to_ulong(), (foo & bar));
    ASSERT_EQUALS((C::Foo ^ EBitSet{C::Bar}).to_ulong(), (foo ^ bar));
    ASSERT_EQUALS((EBitSet{C::Foo} | EBitSet{C::Bar}).to_ulong(), (foo | bar));
    ASSERT_EQUALS((EBitSet{C::Foo} & EBitSet{C::Bar}).to_ulong(), (foo & bar));
    ASSERT_EQUALS((EBitSet{C::Foo} ^ EBitSet{C::Bar}).to_ulong(), (foo ^ bar));
    ASSERT_EQUALS(
        EBitSet(typename EBitSet::FromRawBytes((1ull << 50) | foo | bar | baz)).to_ulong(),
        (foo | bar | baz));

    ASSERT(EBitSet{C::Foo} == EBitSet{C::Foo});
    ASSERT_FALSE(EBitSet{C::Foo} == EBitSet{C::Bar});
    ASSERT(EBitSet{C::Foo}[C::Foo]);
    {
        EBitSet ebitset{};
        ebitset[C::Foo] = true;
        ASSERT(ebitset[C::Foo]);
        ASSERT(ebitset.test(C::Foo));
        ebitset[C::Foo] = false;
        ASSERT_FALSE(ebitset[C::Foo]);
        ASSERT_FALSE(ebitset.test(C::Foo));
    }

    ASSERT(EBitSet{C::Foo | C::Bar | C::Baz}.all());
    ASSERT_FALSE(EBitSet{C::Foo | C::Bar}.all());

    ASSERT(EBitSet{C::Foo}.any());
    ASSERT_FALSE(EBitSet{}.any());

    ASSERT(EBitSet{}.none());
    ASSERT_FALSE(EBitSet{C::Foo}.none());

    ASSERT_EQUALS(EBitSet{}.count(), 0u);
    ASSERT_EQUALS(EBitSet{C::Foo}.count(), foo);
    ASSERT_EQUALS(EBitSet{C::Foo | C::Bar | C::Baz}.count(), 3ul);

    ASSERT_EQUALS(EBitSet{}.size(), Configuration::size);

    ASSERT_EQUALS((EBitSet{C::Foo} |= EBitSet{C::Bar}).to_ulong(), (foo | bar));
    ASSERT_EQUALS((EBitSet{C::Foo} &= EBitSet{C::Bar}).to_ulong(), (foo & bar));
    ASSERT_EQUALS((EBitSet{C::Foo} ^= EBitSet{C::Bar}).to_ulong(), (foo ^ bar));
    ASSERT_EQUALS((~EBitSet{C::Foo | C::Bar}).to_ulong(), baz);

    ASSERT_EQUALS(EBitSet{}.set(C::Foo).to_ulong(), foo);
    ASSERT_EQUALS(EBitSet{}.set(C::Foo).set(C::Foo, false).to_ulong(), 0u);
    ASSERT_EQUALS(EBitSet{}.set(C::Foo).reset().to_ulong(), 0u);
    ASSERT_EQUALS(EBitSet{}.set(C::Foo).reset(C::Foo).to_ulong(), 0u);
    ASSERT_EQUALS(EBitSet{C::Foo | C::Bar}.flip().to_ulong(), baz);
    ASSERT_EQUALS(EBitSet{C::Foo | C::Bar}.flip(C::Baz).to_ulong(), (foo | bar | baz));

    ASSERT_EQUALS(EBitSet{C::Foo}.to_ulong(), foo);
    ASSERT_EQUALS(EBitSet{C::Foo}.to_ullong(), foo);
    ASSERT_EQUALS(EBitSet{C::Foo}.to_string(), std::string(Configuration::size - 1, '0') + "1");
}

TEST(EnumBitset, BasicEnumClass) {
    static_assert(!canBitOps(BasicEnumClass::Foo, 1ul), "Can't bit ops non-enum");
    testEB<BasicEnumClass, BasicEnumClassBitset, Basic>();
}

TEST(EnumBitset, FullRestrictionEnumClass) {
    static_assert(!canBitOps(FullRestrictionEnumClass::Foo, 1ul), "Can't bit ops non-enum");
    testEB<FullRestrictionEnumClass, FullRestrictionEnumClassBitset, Basic>();
}

TEST(EnumBitset, PartialRestrictionEnumClass) {
    static_assert(!canBitOps(PartialRestrictionEnumClass::Foo, 1ul), "Can't bit ops non-enum");
    testEB<PartialRestrictionEnumClass, PartialRestrictionEnumClassBitset, Basic>();
    using EbitSet = PartialRestrictionEnumClassBitset;
    ASSERT_EQUALS(EbitSet(EbitSet::FromRawBytes(2ul)).to_ulong(), 0ull);
}

TEST(EnumBitset, BasicEnum) {
    // Not defending against decay of legacy enum to int
    static_assert(canBitOps(Basic::Foo, 1ul), "Can bit ops non-enum");
    testEB<Basic, BasicEnumBitset, BasicEnumClass>();
}

TEST(EnumBitset, InversionsOfExpressionSFINAE) {
    static_assert(!canBitOps(Unannotated::Foo, Unannotated::Bar), "Can't bit ops unannotated");
    static_assert(canConstructFrom(std::bitset<3>{}, 1ul), "Can construct from non-enum");
    static_assert(canInvokeWith(std::bitset<3>{}, 1ul), "Can invoke annotated");
}

}  // namespace
}  // namespace mongo
