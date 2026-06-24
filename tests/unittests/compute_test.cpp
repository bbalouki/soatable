// B.1 vectorized column ops: single-column span ufuncs, table column helpers across SoA and AoSoA
// storage, and cross-column row-wise assign_from.

#include <gtest/gtest.h>

#include <span>
#include <vector>

#include "soatable/compute.hpp"
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

    const double total =
        soatable::compute::reduce_column<Price>(book, 0.0, [](double acc, const Price& p) {
            return acc + p.value;
        });
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

    const auto big = soatable::compute::count_column_if<Qty>(book, [](const Qty& q) {
        return q.value >= 10;
    });
    EXPECT_EQ(big, 5U);  // doubled values 10,12,14,16,18
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

TEST(ComputeTest, AssignFromOnlyTouchesRowsWithAllInputs) {
    Book book;
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
