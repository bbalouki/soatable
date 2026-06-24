/// @file mmap_test.cpp
/// @brief D.3 memory-mapped columns: mmap_soa_table backs column storage with demand-paged virtual
/// memory.
/// @author Bertin Balouki SIMYELI
///
/// Verifies a large column round-trips, the mapping is page-aligned (SIMD-friendly), and the normal
/// table API (sort/erase) works over it.

#include <gtest/gtest.h>

#include <cstdint>

#include "soatable/mmap.hpp"
#include "soatable/soatable.hpp"

namespace {
struct Sample {
    double value = 0.0;
};
using MappedTable = soatable::mmap_soa_table<Sample>;
}  // namespace

TEST(MmapTest, LargeColumnRoundTrips) {
    MappedTable   table;
    constexpr int row_count = 50000;
    table.reserve(row_count);

    for (int i = 0; i < row_count; ++i) {
        const auto id = table.insert();
        table.assign<Sample>(id, Sample {static_cast<double>(i)});
    }

    ASSERT_EQ(table.size(), static_cast<std::size_t>(row_count));
    EXPECT_DOUBLE_EQ(table.get<Sample>(table.make_row_id(0)).value, 0.0);
    EXPECT_DOUBLE_EQ(table.get<Sample>(table.make_row_id(row_count - 1)).value, row_count - 1);

    double sum = 0.0;
    for (const Sample& sample : table.column<Sample>()) {
        sum += sample.value;
    }
    EXPECT_DOUBLE_EQ(sum, static_cast<double>(row_count) * (row_count - 1) / 2.0);
}

TEST(MmapTest, StorageIsPageAligned) {
    MappedTable table;
    table.reserve(1024);
    for (int i = 0; i < 100; ++i) {
        const auto id = table.insert();
        table.assign<Sample>(id, Sample {static_cast<double>(i)});
    }
    const auto address = reinterpret_cast<std::uintptr_t>(table.column<Sample>().data());
    EXPECT_EQ(address % soatable::simd_alignment, 0U);
}

TEST(MmapTest, SupportsSortAndErase) {
    MappedTable table;
    table.reserve(8);
    const auto a = table.insert();
    table.assign<Sample>(a, Sample {3.0});
    const auto b = table.insert();
    table.assign<Sample>(b, Sample {1.0});
    const auto c = table.insert();
    table.assign<Sample>(c, Sample {2.0});

    table.erase(a);
    table.sort_by_column<Sample>([](const Sample& l, const Sample& r) { return l.value < r.value; });

    const auto column = table.column<Sample>();
    ASSERT_EQ(column.size(), 2U);
    EXPECT_DOUBLE_EQ(column[0].value, 1.0);
    EXPECT_DOUBLE_EQ(column[1].value, 2.0);
}
