// Sorting: single-column, the sort_by alias, multi-column tie-breaking, and the parallel variant.
// Each asserts that physical reordering keeps every row_id valid and every value retrievable.

#include <gtest/gtest.h>

#include <functional>
#include <utility>
#include <vector>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::DemoTable;
using soatable_test::Name;

namespace {

DemoTable make_people() {
    DemoTable table;
    const auto id1 = table.insert();
    table.assign<Name>(id1, "Alice");
    table.assign<Age>(id1, 30);

    const auto id2 = table.insert();
    table.assign<Name>(id2, "Bob");
    table.assign<Age>(id2, 20);

    const auto id3 = table.insert();
    table.assign<Name>(id3, "Alice");
    table.assign<Age>(id3, 25);
    return table;
}

std::vector<int> ages_in_order(DemoTable& table) {
    std::vector<int> ages;
    for (auto [id, age] : table.select<Age>()) {
        static_cast<void>(id);
        ages.push_back(age.get().value);
    }
    return ages;
}

}  // namespace

TEST(SortTest, SingleColumnAscending) {
    DemoTable table = make_people();
    table.sort_by_column<Age>([](const Age& a, const Age& b) { return a.value < b.value; });
    EXPECT_EQ(ages_in_order(table), (std::vector<int> {20, 25, 30}));
}

TEST(SortTest, SortByAliasMatchesSortByColumn) {
    DemoTable table = make_people();
    table.sort_by<Age>([](const Age& a, const Age& b) { return a.value > b.value; });
    EXPECT_EQ(ages_in_order(table), (std::vector<int> {30, 25, 20}));
}

TEST(SortTest, MultiColumnTieBreak) {
    DemoTable table = make_people();
    table.sort_by_multi(
        std::pair<Name, std::function<bool(const Name&, const Name&)>> {
            {}, [](const Name& a, const Name& b) { return a.value < b.value; }
        },
        std::pair<Age, std::function<bool(const Age&, const Age&)>> {
            {}, [](const Age& a, const Age& b) { return a.value < b.value; }
        }
    );

    std::vector<std::pair<std::string, int>> ordered;
    for (auto [id, name, age] : table.select<Name, Age>()) {
        static_cast<void>(id);
        ordered.emplace_back(name.get().value, age.get().value);
    }

    ASSERT_EQ(ordered.size(), 3U);
    EXPECT_EQ(ordered[0], (std::pair<std::string, int> {"Alice", 25}));
    EXPECT_EQ(ordered[1], (std::pair<std::string, int> {"Alice", 30}));
    EXPECT_EQ(ordered[2], (std::pair<std::string, int> {"Bob", 20}));
}

TEST(SortTest, ParallelSortMatchesSerialResult) {
    DemoTable table = make_people();
    table.sort_by_column_parallel<Age>([](const Age& a, const Age& b) { return a.value < b.value; });

    EXPECT_EQ(ages_in_order(table), (std::vector<int> {20, 25, 30}));

    // Every row must remain individually retrievable by its still-valid handle after reordering.
    int matched = 0;
    for (auto [id, name, age] : table.select<Name, Age>()) {
        EXPECT_TRUE(table.is_valid(id));
        EXPECT_EQ(table.get<Age>(id).value, age.get().value);
        static_cast<void>(name);
        ++matched;
    }
    EXPECT_EQ(matched, 3);
}

TEST(SortTest, ParallelSortHandlesSingleColumnTable) {
    soatable::soa_table<Age> table;
    const auto id1 = table.insert();
    table.assign<Age>(id1, 3);
    const auto id2 = table.insert();
    table.assign<Age>(id2, 1);

    table.sort_by_column_parallel<Age>([](const Age& a, const Age& b) { return a.value < b.value; });

    std::vector<int> ages;
    for (auto [id, age] : table.select<Age>()) {
        static_cast<void>(id);
        ages.push_back(age.get().value);
    }
    EXPECT_EQ(ages, (std::vector<int> {1, 3}));
}

TEST(SortTest, ParallelSortLargeTableExercisesConcurrentPath) {
    // Exceed parallel_sort_threshold so the genuine multi-column concurrent reorder runs, and verify
    // it produces a fully sorted, internally consistent table.
    constexpr int row_count = 20000;
    DemoTable     table;
    table.reserve(row_count);

    for (int i = 0; i < row_count; ++i) {
        const auto id = table.insert();
        table.assign<Age>(id, (row_count - i) % 997);  // Deterministic, non-monotonic.
        table.assign<Name>(id, "n");
    }

    table.sort_by_column_parallel<Age>([](const Age& a, const Age& b) { return a.value < b.value; });

    int  previous = -1;
    int  seen     = 0;
    for (auto [id, age, name] : table.select<Age, Name>()) {
        EXPECT_TRUE(table.is_valid(id));
        EXPECT_GE(age.get().value, previous);  // Non-decreasing => sorted.
        EXPECT_EQ(name.get().value, "n");
        previous = age.get().value;
        ++seen;
    }
    EXPECT_EQ(seen, row_count);
}
