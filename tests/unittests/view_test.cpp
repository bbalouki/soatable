/// @file view_test.cpp
/// @brief row_view proxy (Phase 2.1): structured bindings yield real references, get<T>() accessor,
/// the required-columns filter, and the const overload yielding readable values.
/// @author Bertin Balouki SIMYELI

#include <gtest/gtest.h>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::DemoTable;
using soatable_test::Name;

TEST(ViewTest, StructuredBindingsYieldMutableReferences) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Age>(id, 10);
    table.assign<Name>(id, "Eve");

    for (auto [row, age, name] : table.view<Age, Name>()) {
        EXPECT_EQ(row, id);
        age.value += 5;  // Writes straight through to the column.
        EXPECT_EQ(name.value, "Eve");
    }
    EXPECT_EQ(table.get<Age>(id).value, 15);
}

TEST(ViewTest, GetByTypeAccessorMutatesColumn) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Age>(id, 1);

    for (auto row : table.view<Age>()) {
        EXPECT_EQ(row.id(), id);
        row.get<Age>().value = 42;
    }
    EXPECT_EQ(table.get<Age>(id).value, 42);
}

TEST(ViewTest, OnlyVisitsRowsWithEveryRequestedColumn) {
    DemoTable  table;
    const auto full = table.insert();
    table.assign<Age>(full, 1);
    table.assign<Name>(full, "Full");
    const auto partial = table.insert();
    table.assign<Age>(partial, 2);  // No Name, so excluded.

    int visited = 0;
    for (auto [row, age, name] : table.view<Age, Name>()) {
        static_cast<void>(row);
        static_cast<void>(age);
        static_cast<void>(name);
        ++visited;
    }
    EXPECT_EQ(visited, 1);
}

TEST(ViewTest, ConstViewYieldsReadableValues) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Age>(id, 7);

    const DemoTable& const_table = table;
    int              sum         = 0;
    for (auto [row, age] : const_table.view<Age>()) {
        static_cast<void>(row);
        sum += age.value;
    }
    EXPECT_EQ(sum, 7);
}
