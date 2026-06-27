/// @file compute_test.cpp
/// @brief vectorized column ops: single-column span ufuncs, table column helpers across SoA and
/// AoSoA storage, and cross-column row-wise assign_from.
/// @author Bertin Balouki SIMYELI

#include "soatable/compute.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "soatable/soatable.hpp"

namespace {
struct Price {
    double value = 0.0;
};
struct Qty {
    int value = 0;
};
struct Pnl {
    double value = 0.0;
};
using Book      = soatable::soa_table<Price, Qty, Pnl>;
using TiledBook = soatable::aosoa_table<4, Price, Qty, Pnl>;
}  // namespace

TEST(ComputeTest, SpanTransformReduceScanCount) {
    std::vector<int> data = {1, 2, 3, 4};
    soatable::compute::transform(std::span<int>(data), [](int x) { return x * 10; });
    EXPECT_EQ(data, (std::vector<int> {10, 20, 30, 40}));

    const int sum = soatable::compute::reduce(std::span<int>(data), 0);
    EXPECT_EQ(sum, 100);

    std::vector<int> scanned(data.size());
    soatable::compute::inclusive_scan(std::span<int>(data), std::span<int>(scanned));
    EXPECT_EQ(scanned, (std::vector<int> {10, 30, 60, 100}));

    EXPECT_EQ(soatable::compute::count_if(std::span<int>(data), [](int x) { return x >= 30; }), 2U);
}

TEST(ComputeTest, TransformColumnInPlaceSoa) {
    Book book;
    for (int i = 0; i < 5; ++i) {
        const auto id = book.insert();
        book.assign<Price>(id, Price {static_cast<double>(i)});
    }
    soatable::compute::transform_column<Price>(book, [](Price p) { return Price {p.value + 1.0}; });

    const double total = soatable::compute::reduce_column<Price>(
        book, 0.0, [](double acc, const Price& p) { return acc + p.value; }
    );
    EXPECT_DOUBLE_EQ(total, 1.0 + 2.0 + 3.0 + 4.0 + 5.0);
}

TEST(ComputeTest, ColumnOpsWorkOnTiledStorage) {
    TiledBook book;
    for (int i = 0; i < 10; ++i) {  // Spans multiple tiles of 4.
        const auto id = book.insert();
        book.assign<Qty>(id, Qty {i});
    }
    soatable::compute::transform_column<Qty>(book, [](Qty q) { return Qty {q.value * 2}; });

    const int sum = soatable::compute::reduce_column<Qty>(book, 0, [](int acc, const Qty& q) {
        return acc + q.value;
    });
    EXPECT_EQ(sum, 2 * (0 + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9));

    const auto big =
        soatable::compute::count_column_if<Qty>(book, [](const Qty& q) { return q.value >= 10; });
    EXPECT_EQ(big, 5U);  // doubled values 10,12,14,16,18
}

TEST(ComputeTest, TransformColumnParallelMatchesSerial) {
    // Exceed parallel_compute_threshold so the concurrent chunk path runs.
    soatable::chunked_soa_table<1024, Qty> chunked;
    constexpr int                          row_count = 40000;
    for (int i = 0; i < row_count; ++i) {
        const auto id = chunked.insert();
        chunked.assign<Qty>(id, Qty {i});
    }
    soatable::compute::transform_column_parallel<Qty>(chunked, [](Qty q) {
        return Qty {q.value + 1};
    });

    long long sum = 0;
    for (const auto chunk : chunked.column_tiles<Qty>()) {
        for (const Qty& q : chunk) {
            sum += q.value;
        }
    }
    // Each of row_count values incremented by 1: sum = (0+...+(N-1)) + N.
    const long long base = static_cast<long long>(row_count) * (row_count - 1) / 2;
    EXPECT_EQ(sum, base + row_count);
}

TEST(ComputeTest, ForEachChunkVisitsEveryValue) {
    soatable::chunked_soa_table<8, Qty> chunked;
    for (int i = 0; i < 20; ++i) {
        const auto id = chunked.insert();
        chunked.assign<Qty>(id, Qty {1});
    }
    int total = 0;
    soatable::compute::for_each_chunk<Qty>(chunked, [&](std::span<Qty> chunk) {
        for (const Qty& q : chunk) {
            total += q.value;
        }
    });
    EXPECT_EQ(total, 20);
}

TEST(ComputeTest, AssignFromComputesCrossColumn) {
    Book book;
    for (int i = 1; i <= 4; ++i) {
        const auto id = book.insert();
        book.assign<Price>(id, Price {static_cast<double>(i)});
        book.assign<Qty>(id, Qty {i});
    }

    soatable::compute::assign_from<Pnl, Price, Qty>(book, [](const Price& p, const Qty& q) {
        return Pnl {p.value * q.value};
    });

    double sum_pnl = 0.0;
    for (auto [id, pnl] : book.select<Pnl>()) {
        static_cast<void>(id);
        sum_pnl += pnl.get().value;
    }
    EXPECT_DOUBLE_EQ(sum_pnl, 1.0 + 4.0 + 9.0 + 16.0);
}

TEST(ComputeTest, BroadcastAddAndMultiplyScalar) {
    std::vector<double> data = {1.0, 2.0, 3.0};
    soatable::compute::add_scalar(std::span<double>(data), 10.0);
    EXPECT_EQ(data, (std::vector<double> {11.0, 12.0, 13.0}));
    soatable::compute::multiply_scalar(std::span<double>(data), 2.0);
    EXPECT_EQ(data, (std::vector<double> {22.0, 24.0, 26.0}));
}

TEST(ComputeTest, TransformIfOnlyFlaggedElements) {
    std::vector<int> data = {1, 6, 3, 8, 4};
    soatable::compute::transform_if(
        std::span<int>(data), [](int x) { return x > 4; }, [](int x) { return x * 10; }
    );
    EXPECT_EQ(data, (std::vector<int> {1, 60, 3, 80, 4}));
}

TEST(ComputeTest, TransformMaskedByParallelMask) {
    std::vector<int>          data = {1, 2, 3, 4, 5};
    std::vector<std::uint8_t> mask = {1, 0, 1, 0, 1};
    soatable::compute::transform_masked(
        std::span<int>(data), std::span<const std::uint8_t>(mask), [](int x) { return -x; }
    );
    EXPECT_EQ(data, (std::vector<int> {-1, 2, -3, 4, -5}));
}

TEST(ComputeTest, TransformStridedEveryNth) {
    std::vector<int> data = {1, 1, 1, 1, 1, 1};
    soatable::compute::transform_strided(std::span<int>(data), 2, [](int x) { return x + 9; });
    EXPECT_EQ(data, (std::vector<int> {10, 1, 10, 1, 10, 1}));
}

TEST(ComputeTest, BroadcastColumnBiasAcrossTiles) {
    TiledBook book;
    for (int i = 0; i < 10; ++i) {
        const auto id = book.insert();
        book.assign<Price>(id, Price {static_cast<double>(i)});
    }
    soatable::compute::broadcast_column<Price>(book, 100.0, [](Price p, double s) {
        return Price {p.value + s};
    });

    const double total = soatable::compute::reduce_column<Price>(
        book, 0.0, [](double acc, const Price& p) { return acc + p.value; }
    );
    EXPECT_DOUBLE_EQ(total, (0 + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9) + 10 * 100.0);
}

TEST(ComputeTest, TransformColumnIfAcrossTiles) {
    TiledBook book;
    for (int i = 0; i < 10; ++i) {
        const auto id = book.insert();
        book.assign<Qty>(id, Qty {i});
    }
    soatable::compute::transform_column_if<Qty>(
        book,
        [](const Qty& q) { return q.value % 2 == 0; },
        [](Qty q) { return Qty {q.value + 100}; }
    );

    const auto biased =
        soatable::compute::count_column_if<Qty>(book, [](const Qty& q) { return q.value >= 100; });
    EXPECT_EQ(biased, 5U);  // even values 0,2,4,6,8
}

TEST(ComputeTest, AssignFromOnlyTouchesRowsWithAllInputs) {
    Book       book;
    const auto both = book.insert();
    book.assign<Price>(both, Price {2.0});
    book.assign<Qty>(both, Qty {3});
    const auto price_only = book.insert();
    book.assign<Price>(price_only, Price {5.0});  // No Qty: excluded.

    soatable::compute::assign_from<Pnl, Price, Qty>(book, [](const Price& p, const Qty& q) {
        return Pnl {p.value * q.value};
    });

    EXPECT_TRUE(book.contains<Pnl>(both));
    EXPECT_DOUBLE_EQ(book.get<Pnl>(both).value, 6.0);
    EXPECT_FALSE(book.contains<Pnl>(price_only));
}
