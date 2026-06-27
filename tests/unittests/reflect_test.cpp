/// @file reflect_test.cpp
/// @brief reflection path: the portable fallback (columns_of specialization -> table_for) works
/// today and resolves to the same type as the explicit type-list API.
/// @author Bertin Balouki SIMYELI

#include "soatable/reflect.hpp"

#include <gtest/gtest.h>

#include <type_traits>

#include "soatable/soatable.hpp"

namespace {
// A tag struct identifying a schema, plus its column types.
struct Particle {};
struct PosX {
    float value = 0.0F;
};
struct PosY {
    float value = 0.0F;
};
struct Mass {
    float value = 0.0F;
};
}  // namespace

// Portable fallback: declare the schema's columns. Unnecessary once C++26 reflection ships.
template <>
struct soatable::columns_of<Particle> : soatable::column_list<PosX, PosY, Mass> {};

TEST(ReflectTest, TableForResolvesToExplicitTypeList) {
    static_assert(
        std::is_same_v<soatable::table_for<Particle>, soatable::soa_table<PosX, PosY, Mass>>
    );
}

TEST(ReflectTest, TableForIsUsable) {
    soatable::table_for<Particle> table;
    const auto                    id = table.insert();
    table.assign<PosX>(id, PosX {1.0F});
    table.assign<PosY>(id, PosY {2.0F});
    table.assign<Mass>(id, Mass {3.0F});

    EXPECT_FLOAT_EQ(table.get<PosX>(id).value, 1.0F);
    EXPECT_FLOAT_EQ(table.get<Mass>(id).value, 3.0F);
    EXPECT_EQ(table.size(), 1U);
}

TEST(ReflectTest, FeatureMacroIsDefined) {
    // The feature macro is always defined (0 until a toolchain implements reflection).
    EXPECT_GE(SOATABLE_HAS_REFLECTION, 0);
}
