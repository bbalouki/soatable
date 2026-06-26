/// @file batch_test.cpp
/// @brief Batch insertion and single-column batch assignment, including the skip-invalid-rows path.
/// @author Bertin Balouki SIMYELI

#include <gtest/gtest.h>

#include <vector>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::DemoTable;

TEST(BatchTest, InsertBatchCreatesDistinctRows) {
    DemoTable  table;
    const auto ids = table.insert_batch(4);

    ASSERT_EQ(ids.size(), 4U);
    EXPECT_EQ(table.size(), 4U);
    for (const auto id : ids) {
        EXPECT_TRUE(table.is_valid(id));
    }
}

TEST(BatchTest, AssignBatchFillsColumn) {
    DemoTable              table;
    const auto             ids  = table.insert_batch(3);
    const std::vector<Age> ages = {Age {10}, Age {20}, Age {30}};
    table.assign_batch<Age>(ids, ages.begin());

    EXPECT_EQ(table.get<Age>(ids[0]).value, 10);
    EXPECT_EQ(table.get<Age>(ids[1]).value, 20);
    EXPECT_EQ(table.get<Age>(ids[2]).value, 30);
}

TEST(BatchTest, AssignBatchSkipsInvalidRowsButConsumesInput) {
    DemoTable table;
    auto      ids = table.insert_batch(3);
    table.erase(ids[1]);  // Middle row invalid; its input value must be consumed but not applied.

    const std::vector<Age> ages = {Age {1}, Age {2}, Age {3}};
    table.assign_batch<Age>(ids, ages.begin());

    EXPECT_EQ(table.get<Age>(ids[0]).value, 1);
    EXPECT_FALSE(table.is_valid(ids[1]));
    EXPECT_EQ(table.get<Age>(ids[2]).value, 3);
}

TEST(BatchTest, InsertBatchOfZeroIsNoOp) {
    DemoTable  table;
    const auto ids = table.insert_batch(0);
    EXPECT_TRUE(ids.empty());
    EXPECT_TRUE(table.empty());
}
