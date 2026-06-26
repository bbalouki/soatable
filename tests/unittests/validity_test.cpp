/// @file validity_test.cpp
/// @brief A.3 validity bitmaps: validity<T>() reports per-row column presence, composes with
/// make_row_id, and drives branchless masked iteration via for_each_set.
/// @author Bertin Balouki SIMYELI

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::DemoTable;
using soatable_test::Name;

TEST(ValidityTest, BitmapMarksRowsThatHaveColumn) {
    DemoTable  table;
    const auto a = table.insert();
    table.assign<Age>(a, 1);
    const auto b = table.insert();  // No Age.
    const auto c = table.insert();
    table.assign<Age>(c, 3);

    const soatable::bitmap mask = table.validity<Age>();
    EXPECT_EQ(mask.size(), table.row_slots());
    EXPECT_EQ(mask.count(), 2U);
    EXPECT_TRUE(mask.test(a.index));
    EXPECT_FALSE(mask.test(b.index));
    EXPECT_TRUE(mask.test(c.index));
}

TEST(ValidityTest, ErasedRowsAreNotValid) {
    DemoTable  table;
    const auto a = table.insert();
    table.assign<Age>(a, 1);
    const auto b = table.insert();
    table.assign<Age>(b, 2);
    table.erase(a);

    const soatable::bitmap mask = table.validity<Age>();
    EXPECT_FALSE(mask.test(a.index));
    EXPECT_TRUE(mask.test(b.index));
    EXPECT_EQ(mask.count(), 1U);
}

TEST(ValidityTest, ForEachSetVisitsPresentRowsInOrder) {
    DemoTable                  table;
    std::vector<std::uint32_t> expected;
    for (int i = 0; i < 5; ++i) {
        const auto id = table.insert();
        if (i % 2 == 0) {
            table.assign<Age>(id, i);
            expected.push_back(id.index);
        }
    }

    std::vector<std::uint32_t> visited;
    table.validity<Age>().for_each_set([&](std::size_t bit) {
        visited.push_back(static_cast<std::uint32_t>(bit));
    });
    EXPECT_EQ(visited, expected);
}

TEST(ValidityTest, BitmapComposesWithMakeRowId) {
    DemoTable  table;
    const auto a = table.insert();
    table.assign<Age>(a, 42);

    int matched = 0;
    table.validity<Age>().for_each_set([&](std::size_t bit) {
        const auto id = table.make_row_id(static_cast<std::uint32_t>(bit));
        ASSERT_TRUE(table.is_valid(id));
        EXPECT_EQ(table.get<Age>(id).value, 42);
        ++matched;
    });
    EXPECT_EQ(matched, 1);
}

TEST(ValidityTest, EmptyColumnHasZeroCount) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Name>(id, "x");
    EXPECT_EQ(table.validity<Age>().count(), 0U);
}
