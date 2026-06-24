// Exercises the documented exception-safety guarantees with a move-only column type and a column
// whose constructor throws on a chosen insertion.

#include <gtest/gtest.h>

#include <stdexcept>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::MoveOnly;
using soatable_test::ThrowOnNth;

TEST(ExceptionSafetyTest, MoveOnlyColumnSupportsFullLifecycle) {
    soatable::SoaTable<MoveOnly> table;

    const auto first = table.insert();
    table.assign<MoveOnly>(first, 5);
    EXPECT_EQ(table.get<MoveOnly>(first).value, 5);

    const auto second = table.insert();
    table.assign<MoveOnly>(second, 7);

    // Erasing the first row swap-removes it in the column, moving the last element into its slot.
    table.erase(first);
    EXPECT_EQ(table.get<MoveOnly>(second).value, 7);
}

TEST(ExceptionSafetyTest, MoveOnlyColumnSurvivesReorder) {
    soatable::SoaTable<MoveOnly> table;
    const auto a = table.insert();
    table.assign<MoveOnly>(a, 3);
    const auto b = table.insert();
    table.assign<MoveOnly>(b, 1);
    const auto c = table.insert();
    table.assign<MoveOnly>(c, 2);

    table.sort_by_column<MoveOnly>(
        [](const MoveOnly& lhs, const MoveOnly& rhs) { return lhs.value < rhs.value; }
    );

    EXPECT_EQ(table.get<MoveOnly>(a).value, 3);
    EXPECT_EQ(table.get<MoveOnly>(b).value, 1);
    EXPECT_EQ(table.get<MoveOnly>(c).value, 2);
}

TEST(ExceptionSafetyTest, FailedAssignLeavesColumnConsistent) {
    soatable::SoaTable<ThrowOnNth> table;
    const auto id0 = table.insert();
    const auto id1 = table.insert();
    const auto id2 = table.insert();

    ThrowOnNth::construct_budget = 2;  // Third construction throws.
    table.assign<ThrowOnNth>(id0, 10);
    table.assign<ThrowOnNth>(id1, 20);
    EXPECT_THROW(table.assign<ThrowOnNth>(id2, 30), std::runtime_error);
    ThrowOnNth::reset_budget();

    // Strong guarantee: the throwing assign added no phantom dense entry.
    EXPECT_FALSE(table.contains<ThrowOnNth>(id2));
    EXPECT_EQ(table.get<ThrowOnNth>(id0).value, 10);
    EXPECT_EQ(table.get<ThrowOnNth>(id1).value, 20);

    int present = 0;
    for (auto [id, value] : table.select<ThrowOnNth>()) {
        static_cast<void>(id);
        static_cast<void>(value);
        ++present;
    }
    EXPECT_EQ(present, 2);

    // The column remains usable after recovery.
    table.assign<ThrowOnNth>(id2, 30);
    EXPECT_EQ(table.get<ThrowOnNth>(id2).value, 30);
}
