/// @file span_test.cpp
/// @brief A.1 zero-copy column spans: column<T>() exposes dense values contiguously,
/// row_indices<T>() maps them back to rows, and mutations through the span are visible in the
/// table.
/// @author Bertin Balouki SIMYELI

#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <span>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::DemoTable;
using soatable_test::Name;

TEST(SpanTest, ColumnSpanCoversDenseValues) {
    DemoTable  table;
    const auto a = table.insert();
    table.assign<Age>(a, 1);
    const auto b = table.insert();
    table.assign<Age>(b, 2);
    const auto c = table.insert();  // No Age, so excluded from the span.
    static_cast<void>(c);

    const std::span<Age> ages = table.column<Age>();
    EXPECT_EQ(ages.size(), 2U);

    const int sum = std::accumulate(ages.begin(), ages.end(), 0, [](int acc, const Age& age) {
        return acc + age.value;
    });
    EXPECT_EQ(sum, 3);
}

TEST(SpanTest, MutationThroughSpanIsVisibleInTable) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Age>(id, 10);

    std::span<Age> ages = table.column<Age>();
    ASSERT_EQ(ages.size(), 1U);
    ages[0].value = 99;

    EXPECT_EQ(table.get<Age>(id).value, 99);
}

TEST(SpanTest, RowIndicesMapValuesBackToHandles) {
    DemoTable  table;
    const auto a = table.insert();
    table.assign<Age>(a, 5);
    const auto b = table.insert();
    table.assign<Age>(b, 6);

    const std::span<const Age>           ages    = table.column<Age>();
    const std::span<const std::uint32_t> indices = table.row_indices<Age>();
    ASSERT_EQ(ages.size(), indices.size());

    for (std::size_t i = 0; i < ages.size(); ++i) {
        const soatable::row_id id = table.make_row_id(indices[i]);
        ASSERT_TRUE(table.is_valid(id));
        EXPECT_EQ(table.get<Age>(id).value, ages[i].value);
    }
}

TEST(SpanTest, EmptyColumnYieldsEmptySpan) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Name>(id, "x");

    EXPECT_TRUE(table.column<Age>().empty());
    EXPECT_TRUE(table.row_indices<Age>().empty());
}

TEST(SpanTest, ConstTableYieldsConstSpan) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Age>(id, 7);

    const DemoTable&           const_table = table;
    const std::span<const Age> ages        = const_table.column<Age>();
    ASSERT_EQ(ages.size(), 1U);
    EXPECT_EQ(ages[0].value, 7);
}

TEST(SpanTest, ColumnStorageIsSimdOverAligned) {
    // A.2: column storage is over-aligned (Arrow's 64-byte recommendation) for aligned SIMD loads.
    DemoTable table;
    for (int i = 0; i < 100; ++i) {
        const auto id = table.insert();
        table.assign<Age>(id, i);
    }

    const auto address = reinterpret_cast<std::uintptr_t>(table.column<Age>().data());
    EXPECT_EQ(address % soatable::simd_alignment, 0U);
    EXPECT_GE(soatable::simd_alignment, 64U);
}

TEST(SpanTest, MakeRowIdRejectsDeadIndex) {
    DemoTable  table;
    const auto id = table.insert();
    table.erase(id);

    EXPECT_FALSE(table.is_valid(table.make_row_id(id.index)));
}
