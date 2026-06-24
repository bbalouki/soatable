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
#include <cstdint>
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
// Broadcasting, masked, and strided ops
// =========================

/// @brief Broadcast a scalar over a column span in place: values[i] = op(values[i], scalar).
/// @param values The span to modify.
/// @param scalar The scalar operand applied to every element.
/// @param op The combining operation (defaults to addition).
template <typename T, typename Scalar, typename BinaryOp = std::plus<>>
void broadcast(std::span<T> values, const Scalar& scalar, BinaryOp op = {}) {
    for (T& value : values) {
        value = op(value, scalar);
    }
}

/// @brief Add a scalar to every element of a column span (a column bias).
template <typename T, typename Scalar>
void add_scalar(std::span<T> values, const Scalar& scalar) {
    broadcast(values, scalar, std::plus<> {});
}

/// @brief Multiply every element of a column span by a scalar.
template <typename T, typename Scalar>
void multiply_scalar(std::span<T> values, const Scalar& scalar) {
    broadcast(values, scalar, std::multiplies<> {});
}

/// @brief Transform only the elements that satisfy a predicate (a value-flagged subset).
/// @param values The span to modify.
/// @param pred A predicate over const T& selecting which elements to transform.
/// @param op A callable mapping a selected element to its new value.
template <typename T, typename Pred, typename UnaryOp>
void transform_if(std::span<T> values, Pred pred, UnaryOp op) {
    for (T& value : values) {
        if (pred(value)) {
            value = op(value);
        }
    }
}

/// @brief Transform the elements flagged by a parallel byte mask of equal length.
/// @param values The span to modify.
/// @param mask A span where a non-zero entry flags the element at the same index.
/// @param op A callable mapping a flagged element to its new value.
template <typename T, typename UnaryOp>
void transform_masked(std::span<T> values, std::span<const std::uint8_t> mask, UnaryOp op) {
    const std::size_t count = std::min(values.size(), mask.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (mask[i] != 0) {
            values[i] = op(values[i]);
        }
    }
}

/// @brief Transform every stride-th element of a column span (a strided subset).
/// @param values The span to modify.
/// @param stride The step between transformed elements (must be positive).
/// @param op A callable mapping a selected element to its new value.
template <typename T, typename UnaryOp>
void transform_strided(std::span<T> values, std::size_t stride, UnaryOp op) {
    for (std::size_t i = 0; i < values.size(); i += stride) {
        values[i] = op(values[i]);
    }
}

/// @brief Broadcast a scalar over a whole column in place (works for SoA and AoSoA storage).
/// @tparam T The column type.
/// @param table The table.
/// @param scalar The scalar operand.
/// @param op The combining operation (defaults to addition).
template <typename T, typename Storage, typename... Columns, typename Scalar,
          typename BinaryOp = std::plus<>>
void broadcast_column(basic_soa_table<Storage, Columns...>& table, const Scalar& scalar,
                      BinaryOp op = {}) {
    for (std::span<T> tile : table.template column_tiles<T>()) {
        compute::broadcast(tile, scalar, op);
    }
}

/// @brief Transform a column's flagged values in place across both storage policies.
/// @tparam T The column type.
/// @param table The table.
/// @param pred A predicate over const T& selecting which values to transform.
/// @param op A callable mapping a selected value to its new value.
template <typename T, typename Storage, typename... Columns, typename Pred, typename UnaryOp>
void transform_column_if(basic_soa_table<Storage, Columns...>& table, Pred pred, UnaryOp op) {
    for (std::span<T> tile : table.template column_tiles<T>()) {
        compute::transform_if(tile, pred, op);
    }
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
