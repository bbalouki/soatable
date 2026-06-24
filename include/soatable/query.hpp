#pragma once

// Opt-in query helpers layered on select<>() / view<>(): predicate filtering and group-by
// aggregation. These compose the existing smallest-driver scan with a predicate or a grouping pass,
// giving analytics ergonomics without leaving C++.

#include <cstddef>
#include <functional>
#include <ranges>
#include <type_traits>
#include <unordered_map>

#include "soatable/soatable.hpp"

namespace soatable::query {

/// @brief Select rows that have all of Cols and additionally satisfy a row predicate.
/// @tparam Cols The required column types (non-optional).
/// @param table The table to query.
/// @param predicate A callable taking a row_view<Cols...> and returning bool.
/// @return A range of row_view proxies for the matching rows (structured-binding friendly).
/// @note Lazy: it composes view<Cols...>() with std::views::filter. The table must outlive iteration.
template <typename... Cols, typename Storage, typename... Columns, typename Pred>
[[nodiscard]] auto select_where(basic_soa_table<Storage, Columns...>& table, Pred predicate) {
    return table.template view<Cols...>() |
           std::views::filter([predicate](auto row) { return predicate(row); });
}

/// @brief Group rows by a key derived from column Key and fold column Value within each group.
/// @tparam Key The key column type.
/// @tparam Value The value column type.
/// @param table The table to aggregate.
/// @param key_of A callable mapping const Key& to a hashable, equality-comparable group key.
/// @param init The per-group starting accumulator value.
/// @param op A callable op(accumulator, const Value&) returning the new accumulator.
/// @return A map from group key to accumulated value, over rows that have both Key and Value.
template <
    typename Key, typename Value, typename Storage, typename... Columns, typename KeyProj, typename U,
    typename Op>
[[nodiscard]] auto group_reduce(
    const basic_soa_table<Storage, Columns...>& table, KeyProj key_of, U init, Op op
) {
    using key_type = std::decay_t<std::invoke_result_t<KeyProj, const Key&>>;
    std::unordered_map<key_type, U> groups;
    for (auto [id, key, value] : table.template select<Key, Value>()) {
        static_cast<void>(id);
        const key_type group_key = key_of(key.get());
        auto           it        = groups.find(group_key);
        if (it == groups.end()) {
            it = groups.emplace(group_key, init).first;
        }
        it->second = op(std::move(it->second), value.get());
    }
    return groups;
}

/// @brief Group rows by a key and sum a projection of column Value within each group.
/// @tparam Key The key column type.
/// @tparam Value The value column type.
/// @param table The table to aggregate.
/// @param key_of A callable mapping const Key& to a hashable group key.
/// @param value_of A callable mapping const Value& to a summable number.
/// @return A map from group key to the summed projection.
template <
    typename Key, typename Value, typename Storage, typename... Columns, typename KeyProj,
    typename ValueProj>
[[nodiscard]] auto group_sum(
    const basic_soa_table<Storage, Columns...>& table, KeyProj key_of, ValueProj value_of
) {
    using sum_type = std::decay_t<std::invoke_result_t<ValueProj, const Value&>>;
    return group_reduce<Key, Value>(
        table, key_of, sum_type {},
        [value_of](sum_type accumulator, const Value& value) {
            return accumulator + value_of(value);
        }
    );
}

/// @brief Count the rows in each group keyed by a projection of column Key.
/// @tparam Key The key column type.
/// @param table The table to aggregate.
/// @param key_of A callable mapping const Key& to a hashable group key.
/// @return A map from group key to the number of rows that have Key in that group.
template <typename Key, typename Storage, typename... Columns, typename KeyProj>
[[nodiscard]] auto group_count(
    const basic_soa_table<Storage, Columns...>& table, KeyProj key_of
) {
    using key_type = std::decay_t<std::invoke_result_t<KeyProj, const Key&>>;
    std::unordered_map<key_type, std::size_t> counts;
    for (auto [id, key] : table.template select<Key>()) {
        static_cast<void>(id);
        ++counts[key_of(key.get())];
    }
    return counts;
}

}  // namespace soatable::query
