#pragma once

// Opt-in vectorized column operations ("the numpy part") layered on the zero-copy column spans from
// soatable.hpp. Single-column ufuncs operate on std::span and rely on compiler auto-vectorization
// over the 64-byte-aligned column storage; table-level helpers apply them through column_tiles<T>(),
// so they work uniformly for the contiguous (SoA) and tiled (AoSoA) storage policies. Cross-column
// (row-wise) computation goes through the select/view join, since independent sparse columns are not
// row-aligned in storage.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

#include "soatable/soatable.hpp"

namespace soatable::compute {

// =========================
// Single-column span ufuncs
// =========================

/// @brief Transform a column span in place.
/// @param values The span to modify.
/// @param op A callable mapping each element to its new value.
template <typename T, typename UnaryOp>
void transform(std::span<T> values, UnaryOp op) {
    std::transform(values.begin(), values.end(), values.begin(), op);
}

/// @brief Transform an input span into an output span of equal length.
/// @param input The source span.
/// @param output The destination span (must be at least input.size()).
/// @param op A callable mapping an input element to an output element.
template <typename In, typename Out, typename UnaryOp>
void transform(std::span<In> input, std::span<Out> output, UnaryOp op) {
    std::transform(input.begin(), input.end(), output.begin(), op);
}

/// @brief Reduce a column span with an associative, commutative operation.
/// @param values The span to reduce.
/// @param init The identity / starting value.
/// @param op The reduction operation (defaults to addition).
/// @return The reduced value.
template <typename T, typename U, typename BinaryOp = std::plus<>>
[[nodiscard]] U reduce(std::span<T> values, U init, BinaryOp op = {}) {
    return std::reduce(values.begin(), values.end(), std::move(init), op);
}

/// @brief Inclusive prefix scan of an input span into an output span.
/// @param input The source span.
/// @param output The destination span (must be at least input.size()).
/// @param op The combining operation (defaults to addition).
template <typename In, typename Out, typename BinaryOp = std::plus<>>
void inclusive_scan(std::span<In> input, std::span<Out> output, BinaryOp op = {}) {
    std::inclusive_scan(input.begin(), input.end(), output.begin(), op);
}

/// @brief Count the elements of a column span that satisfy a predicate.
/// @param values The span to scan.
/// @param pred The predicate.
/// @return The number of matching elements.
template <typename T, typename Pred>
[[nodiscard]] std::size_t count_if(std::span<T> values, Pred pred) {
    return static_cast<std::size_t>(std::count_if(values.begin(), values.end(), pred));
}

// =========================
// Table column conveniences
// =========================

/// @brief Transform every value of a column in place (works for SoA and AoSoA storage).
/// @tparam T The column type.
/// @param table The table.
/// @param op A callable mapping each value to its new value.
template <typename T, typename Storage, typename... Columns, typename UnaryOp>
void transform_column(basic_soa_table<Storage, Columns...>& table, UnaryOp op) {
    for (std::span<T> tile : table.template column_tiles<T>()) {
        compute::transform(tile, op);
    }
}

/// @brief Left-fold a column's values, allowing a projecting accumulator.
/// @tparam T The column type.
/// @param table The table.
/// @param init The starting accumulator value.
/// @param op A callable op(accumulator, const T&) returning the new accumulator.
/// @return The accumulated value.
template <typename T, typename Storage, typename... Columns, typename U, typename BinaryOp>
[[nodiscard]] U reduce_column(
    const basic_soa_table<Storage, Columns...>& table, U init, BinaryOp op
) {
    U accumulator = std::move(init);
    for (std::span<const T> tile : table.template column_tiles<T>()) {
        for (const T& value : tile) {
            accumulator = op(std::move(accumulator), value);
        }
    }
    return accumulator;
}

/// @brief Count the rows whose value in column T satisfies a predicate.
/// @tparam T The column type.
/// @param table The table.
/// @param pred The predicate over const T&.
/// @return The number of matching values.
template <typename T, typename Storage, typename... Columns, typename Pred>
[[nodiscard]] std::size_t count_column_if(
    const basic_soa_table<Storage, Columns...>& table, Pred pred
) {
    std::size_t total = 0;
    for (std::span<const T> tile : table.template column_tiles<T>()) {
        total += compute::count_if(tile, pred);
    }
    return total;
}

// =========================
// Cross-column (row-wise) computation
// =========================

/// @brief Compute an output column from one or more input columns, per row.
///
/// Iterates the rows that have every input column (the select/view join) and assigns the result of
/// @p op to the output column. Runs in two passes so assigning the output never invalidates the
/// in-progress join.
/// @tparam Out The output column type (must differ from every input).
/// @tparam In The input column types.
/// @param table The table.
/// @param op A callable op(const In&...) returning an Out value.
template <typename Out, typename... In, typename Storage, typename... Columns, typename Op>
void assign_from(basic_soa_table<Storage, Columns...>& table, Op op) {
    static_assert(
        (!std::same_as<Out, In> && ...), "assign_from: the output column must differ from inputs."
    );

    std::vector<std::pair<row_id, Out>> results;
    for (auto row : table.template view<In...>()) {
        results.emplace_back(row.id(), op(row.template get<In>()...));
    }
    for (auto& [id, value] : results) {
        table.template assign<Out>(id, std::move(value));
    }
}

}  // namespace soatable::compute
