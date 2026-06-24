// B.2 query DSL: select_where predicate filtering and group_by aggregation (group_reduce/sum/count).

#include <gtest/gtest.h>

#include <algorithm>
#include <unordered_map>

#include "soatable/query.hpp"
#include "soatable/soatable.hpp"

namespace {
struct Region {
    int id = 0;
};
struct Pnl {
    double value = 0.0;
};
struct Active {
    bool on = false;
};
using Trades = soatable::soa_table<Region, Pnl, Active>;

Trades make_trades() {
    Trades table;
    for (int i = 0; i < 9; ++i) {
        const auto id = table.insert();
        table.assign<Region>(id, Region {i % 3});
        table.assign<Pnl>(id, Pnl {static_cast<double>(i)});
        if (i % 2 == 0) {
            table.assign<Active>(id, Active {true});
        }
    }
    return table;
}
}  // namespace

TEST(QueryTest, SelectWhereFiltersRows) {
    Trades table = make_trades();
    int    matches = 0;
    for (auto row : soatable::query::select_where<Pnl, Region>(table, [](auto r) {
             return r.template get<Pnl>().value >= 5.0;
         })) {
        EXPECT_GE(row.template get<Pnl>().value, 5.0);
        ++matches;
    }
    EXPECT_EQ(matches, 4);  // pnl 5,6,7,8
}

TEST(QueryTest, SelectWhereYieldsMutableReferences) {
    Trades table = make_trades();
    for (auto row : soatable::query::select_where<Pnl>(table, [](auto r) {
             return r.template get<Pnl>().value == 0.0;
         })) {
        row.template get<Pnl>().value = 100.0;
    }

    int updated = 0;
    for (auto [id, pnl] : table.select<Pnl>()) {
        static_cast<void>(id);
        if (pnl.get().value == 100.0) {
            ++updated;
        }
    }
    EXPECT_EQ(updated, 1);
}

TEST(QueryTest, GroupSumByRegion) {
    Trades     table = make_trades();
    const auto sums  = soatable::query::group_sum<Region, Pnl>(
        table, [](const Region& r) { return r.id; }, [](const Pnl& p) { return p.value; }
    );

    // Region 0: rows 0,3,6 -> 9; Region 1: 1,4,7 -> 12; Region 2: 2,5,8 -> 15.
    ASSERT_EQ(sums.size(), 3U);
    EXPECT_DOUBLE_EQ(sums.at(0), 9.0);
    EXPECT_DOUBLE_EQ(sums.at(1), 12.0);
    EXPECT_DOUBLE_EQ(sums.at(2), 15.0);
}

TEST(QueryTest, GroupCountByRegion) {
    Trades     table  = make_trades();
    const auto counts = soatable::query::group_count<Region>(table, [](const Region& r) {
        return r.id;
    });
    ASSERT_EQ(counts.size(), 3U);
    EXPECT_EQ(counts.at(0), 3U);
    EXPECT_EQ(counts.at(1), 3U);
    EXPECT_EQ(counts.at(2), 3U);
}

TEST(QueryTest, GroupReduceComputesPerGroupMax) {
    Trades     table = make_trades();
    const auto maxima =
        soatable::query::group_reduce<Region, Pnl>(
            table, [](const Region& r) { return r.id; }, 0.0,
            [](double acc, const Pnl& p) { return std::max(acc, p.value); }
        );
    EXPECT_DOUBLE_EQ(maxima.at(0), 6.0);
    EXPECT_DOUBLE_EQ(maxima.at(1), 7.0);
    EXPECT_DOUBLE_EQ(maxima.at(2), 8.0);
}
