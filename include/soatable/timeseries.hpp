#pragma once

// Opt-in time-series helpers: rolling-window aggregates and successive deltas over a column's
// values, plus dirty-region scans for incremental recompute. The rolling/delta helpers operate on a
// column's dense order, which is meaningful once the table is sorted by its time key, so sort first.

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include "soatable/soatable.hpp"

namespace soatable::timeseries {

/// @brief Trailing rolling sum: out[i] is the sum of the up-to-`window` values ending at i.
/// @param input The ordered input values.
/// @param window The window length (a window of 0 is treated as 1).
/// @return A vector the same length as input.
template <typename T>
[[nodiscard]] std::vector<T> rolling_sum(std::span<const T> input, std::size_t window) {
    const std::size_t effective = window == 0 ? 1 : window;
    std::vector<T>    output(input.size());
    T                 running {};
    for (std::size_t i = 0; i < input.size(); ++i) {
        running += input[i];
        if (i >= effective) {
            running -= input[i - effective];
        }
        output[i] = running;
    }
    return output;
}

/// @brief Trailing rolling mean of the up-to-`window` values ending at each position.
/// @param input The ordered input values (convertible to double).
/// @param window The window length (a window of 0 is treated as 1).
/// @return A vector of means the same length as input.
template <typename T>
[[nodiscard]] std::vector<double> rolling_mean(std::span<const T> input, std::size_t window) {
    const std::size_t   effective = window == 0 ? 1 : window;
    std::vector<double> output(input.size());
    double              running = 0.0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        running += static_cast<double>(input[i]);
        if (i >= effective) {
            running -= static_cast<double>(input[i - effective]);
        }
        const std::size_t count = std::min(i + 1, effective);
        output[i]               = running / static_cast<double>(count);
    }
    return output;
}

/// @brief Successive differences: out[0] is value-initialized; out[i] = input[i] - input[i - 1].
/// @param input The ordered input values.
/// @return A vector the same length as input.
template <typename T>
[[nodiscard]] std::vector<T> deltas(std::span<const T> input) {
    std::vector<T> output(input.size());
    for (std::size_t i = 1; i < input.size(); ++i) {
        output[i] = static_cast<T>(input[i] - input[i - 1]);
    }
    return output;
}

/// @brief Rolling mean of a column, projected to double in the column's dense order.
/// @tparam Column The column type.
/// @param table The table (sort by the time key first for meaningful windows).
/// @param projection A callable mapping const Column& to a number.
/// @param window The window length.
/// @return The rolling means in dense order.
template <typename Column, typename Storage, typename... Columns, typename Projection>
[[nodiscard]] std::vector<double> rolling_mean_column(
    const basic_soa_table<Storage, Columns...>& table, Projection projection, std::size_t window
) {
    std::vector<double> values;
    for (auto [id, value] : table.template select<Column>()) {
        static_cast<void>(id);
        values.push_back(static_cast<double>(projection(value.get())));
    }
    return rolling_mean(std::span<const double>(values.data(), values.size()), window);
}

/// @brief Invoke a callback for each row whose dirty-mask column reports any dirty flag.
/// @tparam MaskColumn A column type exposing is_any_dirty() (e.g. a dirty_mask used as a column).
/// @param table The table.
/// @param func A callable taking (row_id, MaskColumn&); it may clear the mask for incremental work.
template <typename MaskColumn, typename Storage, typename... Columns, typename Func>
void for_each_dirty(basic_soa_table<Storage, Columns...>& table, Func func) {
    for (auto [id, mask] : table.template select<MaskColumn>()) {
        if (mask.get().is_any_dirty()) {
            func(id, mask.get());
        }
    }
}

}  // namespace soatable::timeseries
