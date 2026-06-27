/// @file units_test.cpp
/// @brief units: compile-time dimensional analysis.
/// @author Bertin Balouki SIMYELI
///
/// Arithmetic combines dimensions correctly, and a quantity is usable as a SoaTable column.

#include "soatable/units.hpp"

#include <gtest/gtest.h>

#include <type_traits>

#include "soatable/soatable.hpp"

namespace u = soatable::units;

TEST(UnitsTest, DivisionProducesVelocityDimension) {
    const u::length<>   distance {100.0};
    const u::duration<> elapsed {4.0};
    const auto          speed = distance / elapsed;

    static_assert(std::is_same_v<decltype(speed)::dimension_type, u::velocity_dimension>);
    EXPECT_DOUBLE_EQ(speed.value(), 25.0);
}

TEST(UnitsTest, MultiplicationCombinesDimensions) {
    const u::velocity<> speed {10.0};
    const u::duration<> elapsed {3.0};
    const auto          distance = speed * elapsed;

    static_assert(std::is_same_v<decltype(distance)::dimension_type, u::length_dimension>);
    EXPECT_DOUBLE_EQ(distance.value(), 30.0);
}

TEST(UnitsTest, SameDimensionAddAndSubtract) {
    const u::length<> a {5.0};
    const u::length<> b {2.0};
    EXPECT_DOUBLE_EQ((a + b).value(), 7.0);
    EXPECT_DOUBLE_EQ((a - b).value(), 3.0);
}

TEST(UnitsTest, ScalarScaling) {
    const u::force<> thrust {1000.0};
    EXPECT_DOUBLE_EQ((thrust * 2.0).value(), 2000.0);
}

TEST(UnitsTest, UsableAsColumnType) {
    soatable::soa_table<u::length<>, u::velocity<>> table;
    const auto                                      id = table.insert();
    table.assign<u::length<>>(id, u::length<> {12.5});
    table.assign<u::velocity<>>(id, u::velocity<> {3.0});

    EXPECT_DOUBLE_EQ(table.get<u::length<>>(id).value(), 12.5);
    EXPECT_DOUBLE_EQ(table.get<u::velocity<>>(id).value(), 3.0);
}
