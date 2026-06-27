/// @file dynamic_test.cpp
/// @brief runtime schema evolution: dynamic_table adds/removes typed columns at runtime,
/// type-checks access, stores sparse cells, and carries per-column metadata.
/// @author Bertin Balouki SIMYELI

#include "soatable/dynamic.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

TEST(DynamicTest, AddSetGetTypedColumns) {
    soatable::dynamic_table table;
    ASSERT_TRUE(table.add_column<int>("age"));
    ASSERT_TRUE(table.add_column<std::string>("name"));
    EXPECT_FALSE(table.add_column<int>("age"));  // Duplicate.

    const auto row = table.insert_row();
    table.set<int>(row, "age", 41);
    table.set<std::string>(row, "name", "Grace");

    ASSERT_NE(table.get<int>(row, "age"), nullptr);
    EXPECT_EQ(*table.get<int>(row, "age"), 41);
    EXPECT_EQ(*table.get<std::string>(row, "name"), "Grace");
    EXPECT_EQ(table.column_count(), 2U);
    EXPECT_EQ(table.size(), 1U);
}

TEST(DynamicTest, SparseCellsReturnNullWhenUnset) {
    soatable::dynamic_table table;
    table.add_column<int>("x");
    const auto row = table.insert_row();
    EXPECT_EQ(table.get<int>(row, "x"), nullptr);  // Column exists but cell unset.
    EXPECT_EQ(table.get<int>(row, "missing"), nullptr);
}

TEST(DynamicTest, TypeMismatchIsRejected) {
    soatable::dynamic_table table;
    table.add_column<int>("age");
    const auto row = table.insert_row();
    table.set<int>(row, "age", 5);

    EXPECT_EQ(table.get<double>(row, "age"), nullptr);  // Wrong type: null.
    EXPECT_THROW(table.set<double>(row, "age", 1.0), std::invalid_argument);
    EXPECT_THROW(table.set<int>(row, "nope", 1), std::out_of_range);
}

TEST(DynamicTest, RemoveColumnDropsCells) {
    soatable::dynamic_table table;
    table.add_column<int>("temp");
    const auto row = table.insert_row();
    table.set<int>(row, "temp", 100);

    EXPECT_TRUE(table.remove_column("temp"));
    EXPECT_FALSE(table.has_column("temp"));
    EXPECT_EQ(table.get<int>(row, "temp"), nullptr);
    EXPECT_FALSE(table.remove_column("temp"));  // Already gone.
}

TEST(DynamicTest, EraseRowClearsAllColumns) {
    soatable::dynamic_table table;
    table.add_column<int>("a");
    table.add_column<int>("b");
    const auto row = table.insert_row();
    table.set<int>(row, "a", 1);
    table.set<int>(row, "b", 2);

    table.erase_row(row);
    EXPECT_FALSE(table.is_alive(row));
    EXPECT_EQ(table.size(), 0U);
    EXPECT_EQ(table.get<int>(row, "a"), nullptr);
}

TEST(DynamicTest, ColumnMetadata) {
    soatable::dynamic_table table;
    table.add_column<double>("altitude");
    table.set_metadata("altitude", "unit", "metres");

    ASSERT_NE(table.get_metadata("altitude", "unit"), nullptr);
    EXPECT_EQ(*table.get_metadata("altitude", "unit"), "metres");
    EXPECT_EQ(table.get_metadata("altitude", "absent"), nullptr);
    EXPECT_THROW(table.set_metadata("nope", "k", "v"), std::out_of_range);
}
