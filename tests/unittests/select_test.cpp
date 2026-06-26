/// @file select_test.cpp
/// @brief select<>() projections: required-only, mixed optional, zero-required (all-optional)
/// driver, all-absent driver column, and the rows()/for_each_row iteration helpers.
/// @author Bertin Balouki SIMYELI

#include <gtest/gtest.h>

#include <optional>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::DemoTable;
using soatable_test::Name;
using soatable_test::Score;

TEST(SelectTest, RequiredColumnsYieldOnlyFullyPopulatedRows) {
    DemoTable  table;
    const auto id1 = table.insert();
    table.assign<Name>(id1, "Alice");
    table.assign<Age>(id1, 30);

    const auto id2 = table.insert();
    table.assign<Name>(id2, "Bob");  // No Age, so excluded from select<Name, Age>.

    int count = 0;
    for (auto [id, name, age] : table.select<Name, Age>()) {
        EXPECT_EQ(id, id1);
        EXPECT_EQ(name.get().value, "Alice");
        EXPECT_EQ(age.get().value, 30);
        ++count;
    }
    EXPECT_EQ(count, 1);
}

TEST(SelectTest, OptionalColumnsAreIncludedWhenAbsent) {
    DemoTable  table;
    const auto id1 = table.insert();
    table.assign<Name>(id1, "Alice");
    table.assign<Age>(id1, 30);

    const auto id2 = table.insert();
    table.assign<Name>(id2, "Bob");

    int seen = 0;
    for (auto [id, name, age_opt] : table.select<Name, std::optional<Age>>()) {
        if (name.get().value == "Alice") {
            ASSERT_TRUE(age_opt.has_value());
            EXPECT_EQ(age_opt->get().value, 30);
        } else {
            EXPECT_EQ(name.get().value, "Bob");
            EXPECT_FALSE(age_opt.has_value());
        }
        ++seen;
    }
    EXPECT_EQ(seen, 2);
}

TEST(SelectTest, AllOptionalSelectIteratesEveryAliveRow) {
    DemoTable  table;
    const auto id1 = table.insert();
    table.assign<Age>(id1, 1);
    const auto id2 = table.insert();  // No columns assigned at all.
    static_cast<void>(id2);

    int rows = 0;
    for (auto [id, age_opt] : table.select<std::optional<Age>>()) {
        static_cast<void>(id);
        static_cast<void>(age_opt);
        ++rows;
    }
    EXPECT_EQ(rows, 2);
}

TEST(SelectTest, AbsentDriverColumnYieldsNothing) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Name>(id, "Alice");

    int count = 0;
    for (auto [row, score] : table.select<Score>()) {
        static_cast<void>(row);
        static_cast<void>(score);
        ++count;
    }
    EXPECT_EQ(count, 0);
}

TEST(SelectTest, SelectMutatesUnderlyingColumn) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Age>(id, 10);

    for (auto [row, age] : table.select<Age>()) {
        static_cast<void>(row);
        age.get().value += 5;
    }
    EXPECT_EQ(table.get<Age>(id).value, 15);
}

TEST(SelectTest, ConstSelectYieldsConstReferences) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Age>(id, 42);

    const DemoTable& const_table = table;
    int              sum         = 0;
    for (auto [row, age] : const_table.select<Age>()) {
        static_cast<void>(row);
        sum += age.get().value;
    }
    EXPECT_EQ(sum, 42);
}

TEST(SelectTest, RowsIterationVisitsEveryAliveRow) {
    DemoTable  table;
    const auto id1 = table.insert();
    const auto id2 = table.insert();
    table.erase(id1);

    int visited = 0;
    for (const auto id : table.rows()) {
        EXPECT_EQ(id, id2);
        ++visited;
    }
    EXPECT_EQ(visited, 1);
}
