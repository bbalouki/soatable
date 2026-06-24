// get_expected (Phase 2.3): the non-throwing accessor returns the value on success and a typed
// access_error otherwise, in both the mutable and const overloads.

#include <gtest/gtest.h>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::DemoTable;

TEST(ExpectedTest, ReturnsReferenceOnSuccess) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Age>(id, 5);

    auto result = table.get_expected<Age>(id);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get().value, 5);

    result->get().value = 6;  // The wrapped reference is mutable.
    EXPECT_EQ(table.get<Age>(id).value, 6);
}

TEST(ExpectedTest, ReportsMissingColumn) {
    DemoTable  table;
    const auto id = table.insert();

    const auto result = table.get_expected<Age>(id);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), soatable::access_error::missing_column);
}

TEST(ExpectedTest, ReportsInvalidRow) {
    DemoTable  table;
    const auto id = table.insert();
    table.erase(id);

    const auto result = table.get_expected<Age>(id);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), soatable::access_error::invalid_row);
}

TEST(ExpectedTest, ConstAccessorReturnsConstReference) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Age>(id, 9);

    const DemoTable& const_table = table;
    const auto       result      = const_table.get_expected<Age>(id);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->get().value, 9);
}
