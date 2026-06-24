/// @file table_test.cpp
/// @brief Core table lifecycle: insert/erase/assign/get/contains/unassign, handle validity,
/// free-list recycling under churn, ABA generation behaviour, and empty/large tables.
/// @author Bertin Balouki SIMYELI

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::DemoTable;
using soatable_test::Name;
using soatable_test::Score;

TEST(TableTest, BasicInsertAssignAndGet) {
    DemoTable table;
    const auto id = table.insert();
    EXPECT_TRUE(table.is_valid(id));

    table.assign<Name>(id, "Alice");
    table.assign<Age>(id, 30);

    EXPECT_EQ(table.get<Name>(id).value, "Alice");
    EXPECT_EQ(table.get<Age>(id).value, 30);
    EXPECT_FALSE(table.contains<Score>(id));
    EXPECT_EQ(table.size(), 1U);
}

TEST(TableTest, TryGetReturnsNullForAbsentColumn) {
    DemoTable table;
    const auto id = table.insert();
    table.assign<Name>(id, "Bob");

    EXPECT_NE(table.try_get<Name>(id), nullptr);
    EXPECT_EQ(table.try_get<Age>(id), nullptr);
}

TEST(TableTest, GetThrowsForAbsentColumn) {
    DemoTable table;
    const auto id = table.insert();
    EXPECT_THROW(static_cast<void>(table.get<Age>(id)), std::out_of_range);
}

TEST(TableTest, AssignThrowsForInvalidRow) {
    DemoTable table;
    const auto id = table.insert();
    table.erase(id);
    EXPECT_THROW(table.assign<Age>(id, 1), std::out_of_range);
}

TEST(TableTest, UnassignRemovesColumnButKeepsRow) {
    DemoTable table;
    const auto id = table.insert();
    table.assign<Age>(id, 7);
    ASSERT_TRUE(table.contains<Age>(id));

    table.unassign<Age>(id);
    EXPECT_FALSE(table.contains<Age>(id));
    EXPECT_TRUE(table.is_valid(id));
}

TEST(TableTest, EraseInvalidatesHandleAndBumpsGeneration) {
    DemoTable table;
    const auto id = table.insert();
    table.erase(id);
    EXPECT_FALSE(table.is_valid(id));

    const auto reused = table.insert();
    EXPECT_EQ(id.index, reused.index);
    EXPECT_NE(id.generation, reused.generation);
    EXPECT_FALSE(table.is_valid(id));
    EXPECT_TRUE(table.is_valid(reused));
}

TEST(TableTest, StaleHandleNeverAccessesRecycledRow) {
    DemoTable table;
    const auto first = table.insert();
    table.assign<Age>(first, 1);
    table.erase(first);

    const auto second = table.insert();
    table.assign<Age>(second, 2);

    // The stale handle must not read the recycled slot's new value.
    EXPECT_EQ(table.try_get<Age>(first), nullptr);
    EXPECT_EQ(table.get<Age>(second).value, 2);
}

TEST(TableTest, FreeListRecyclesSlotsUnderChurn) {
    DemoTable table;

    // Repeated insert/erase of single rows must reuse the same slot rather than growing storage.
    for (int iteration = 0; iteration < 1000; ++iteration) {
        const auto id = table.insert();
        table.assign<Age>(id, iteration);
        EXPECT_EQ(table.get<Age>(id).value, iteration);
        table.erase(id);
        EXPECT_FALSE(table.is_valid(id));
    }

    EXPECT_EQ(table.size(), 0U);
    EXPECT_LE(table.row_slots(), 2U);
}

TEST(TableTest, InterleavedChurnKeepsSizeConsistent) {
    DemoTable table;
    std::vector<soatable::row_id> live;

    for (int iteration = 0; iteration < 500; ++iteration) {
        const auto id = table.insert();
        table.assign<Age>(id, iteration);
        live.push_back(id);

        // Erase every third row to exercise mid-array swap-removal in the columns.
        if (iteration % 3 == 0 && !live.empty()) {
            const auto victim = live.front();
            live.erase(live.begin());
            table.erase(victim);
        }
    }

    EXPECT_EQ(table.size(), live.size());
    for (const auto id : live) {
        ASSERT_TRUE(table.is_valid(id));
        EXPECT_TRUE(table.contains<Age>(id));
    }
}

TEST(TableTest, EmptyTableReportsEmpty) {
    DemoTable table;
    EXPECT_TRUE(table.empty());
    EXPECT_EQ(table.size(), 0U);

    int visited = 0;
    table.for_each_row([&](soatable::row_id) { ++visited; });
    EXPECT_EQ(visited, 0);
}

TEST(TableTest, ClearResetsRowsButPreservesGenerationMonotonicity) {
    DemoTable table;
    const auto id = table.insert();
    table.assign<Age>(id, 5);
    table.clear();

    EXPECT_TRUE(table.empty());
    EXPECT_FALSE(table.is_valid(id));
}

TEST(TableTest, ReserveAndShrinkPreserveData) {
    DemoTable table;
    table.reserve(256);

    const auto id = table.insert();
    table.assign<Name>(id, "Carol");
    table.shrink_to_fit();

    EXPECT_EQ(table.get<Name>(id).value, "Carol");
}

TEST(TableTest, LargeTableRetainsEveryRow) {
    DemoTable table;
    constexpr int row_count = 50000;

    std::vector<soatable::row_id> ids;
    ids.reserve(row_count);
    for (int i = 0; i < row_count; ++i) {
        const auto id = table.insert();
        table.assign<Age>(id, i);
        ids.push_back(id);
    }

    ASSERT_EQ(table.size(), static_cast<std::size_t>(row_count));
    for (int i = 0; i < row_count; ++i) {
        EXPECT_EQ(table.get<Age>(ids[static_cast<std::size_t>(i)]).value, i);
    }
}
