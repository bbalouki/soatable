// E.4 time-series helpers: rolling sum/mean, successive deltas, projected column rolling mean, and
// dirty-region scans for incremental recompute.

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "soatable/soatable.hpp"
#include "soatable/timeseries.hpp"

namespace {
struct Price {
    double value = 0.0;
};
enum class Field : std::uint32_t {
    recompute = 1U << 0U,
};
using TsTable = soatable::soa_table<Price, soatable::dirty_mask<Field>>;
}  // namespace

TEST(TimeSeriesTest, RollingSumTrailingWindow) {
    const std::vector<int> data = {1, 2, 3, 4, 5};
    const auto sums = soatable::timeseries::rolling_sum(std::span<const int>(data), 3);
    EXPECT_EQ(sums, (std::vector<int> {1, 3, 6, 9, 12}));  // windows: 1,1+2,1+2+3,2+3+4,3+4+5
}

TEST(TimeSeriesTest, RollingMeanTrailingWindow) {
    const std::vector<double> data = {2.0, 4.0, 6.0, 8.0};
    const auto means = soatable::timeseries::rolling_mean(std::span<const double>(data), 2);
    ASSERT_EQ(means.size(), 4U);
    EXPECT_DOUBLE_EQ(means[0], 2.0);  // mean(2)
    EXPECT_DOUBLE_EQ(means[1], 3.0);  // mean(2,4)
    EXPECT_DOUBLE_EQ(means[2], 5.0);  // mean(4,6)
    EXPECT_DOUBLE_EQ(means[3], 7.0);  // mean(6,8)
}

TEST(TimeSeriesTest, DeltasAreSuccessiveDifferences) {
    const std::vector<int> data = {10, 13, 9, 20};
    const auto diffs = soatable::timeseries::deltas(std::span<const int>(data));
    EXPECT_EQ(diffs, (std::vector<int> {0, 3, -4, 11}));
}

TEST(TimeSeriesTest, RollingMeanColumnProjectsValues) {
    TsTable table;
    for (int i = 1; i <= 4; ++i) {
        const auto id = table.insert();
        table.assign<Price>(id, Price {static_cast<double>(i)});
    }
    const auto means = soatable::timeseries::rolling_mean_column<Price>(
        table, [](const Price& p) { return p.value; }, 2
    );
    ASSERT_EQ(means.size(), 4U);
    EXPECT_DOUBLE_EQ(means[0], 1.0);
    EXPECT_DOUBLE_EQ(means[3], 3.5);  // mean(3,4)
}

TEST(TimeSeriesTest, ForEachDirtyVisitsAndClearsFlaggedRows) {
    TsTable table;
    for (int i = 0; i < 5; ++i) {
        const auto              id = table.insert();
        soatable::dirty_mask<Field> mask;
        if (i % 2 == 0) {
            mask.mark_dirty(Field::recompute);
        }
        table.assign<soatable::dirty_mask<Field>>(id, mask);
    }

    int visited = 0;
    soatable::timeseries::for_each_dirty<soatable::dirty_mask<Field>>(
        table, [&](soatable::row_id, soatable::dirty_mask<Field>& mask) {
            ++visited;
            mask.reset();  // Mark handled.
        }
    );
    EXPECT_EQ(visited, 3);  // rows 0, 2, 4

    // A second pass finds nothing, since the masks were cleared.
    int second_pass = 0;
    soatable::timeseries::for_each_dirty<soatable::dirty_mask<Field>>(
        table, [&](soatable::row_id, soatable::dirty_mask<Field>&) { ++second_pass; }
    );
    EXPECT_EQ(second_pass, 0);
}
