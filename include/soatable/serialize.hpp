#pragma once

// Opt-in binary serialization for soa_table. Included separately so the core header pays nothing
// for it. Supports the trivially-copyable fast path: each column's dense values are dumped/loaded
// as raw bytes, wrapped in a versioned, schema-checked header that round-trips the full table state
// (handles, generations, free list, and physical column order).
//
// Portability: the format is self-describing and versioned, but values are written in the host's
// native byte order and ABI. It is intended for snapshots consumed by the same architecture (tick
// stores, simulation checkpoints). Cross-endian / cross-ABI interchange is a future enhancement and
// is the natural place for an Arrow bridge, built on column<T>() spans and validity<T>().words().

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

#include "soatable/soatable.hpp"

namespace soatable {

/// @brief Outcome of a load() call.
enum class serialize_status {
    ok,
    bad_magic,
    version_mismatch,
    schema_mismatch,
    truncated,
};

namespace detail {

/// @brief Friend accessor exposing a table's private state to the serializer only.
/// @tparam Storage The table's storage policy.
/// @tparam Columns The table's column types.
template <typename Storage, typename... Columns>
struct table_access {
    using table_type = basic_soa_table<Storage, Columns...>;

    static auto&          rows(table_type& t) noexcept { return t.m_rows; }
    static const auto&    rows(const table_type& t) noexcept { return t.m_rows; }
    static auto&          free_links(table_type& t) noexcept { return t.m_free_links; }
    static const auto&    free_links(const table_type& t) noexcept { return t.m_free_links; }
    static std::uint32_t& free_head(table_type& t) noexcept { return t.m_free_head; }
    static std::uint32_t  free_head(const table_type& t) noexcept { return t.m_free_head; }
    static std::size_t&   alive_count(table_type& t) noexcept { return t.m_alive_count; }
    static std::size_t    alive_count(const table_type& t) noexcept { return t.m_alive_count; }
    static auto&          columns(table_type& t) noexcept { return t.m_columns; }
    static const auto&    columns(const table_type& t) noexcept { return t.m_columns; }
};

inline constexpr std::array<std::byte, 8> serialize_magic = {
    std::byte {'S'},
    std::byte {'O'},
    std::byte {'A'},
    std::byte {'T'},
    std::byte {'B'},
    std::byte {'L'},
    std::byte {'E'},
    std::byte {'\0'}
};
inline constexpr std::uint32_t serialize_version = 1;

/// @brief FNV-1a hash of the column layout so load() can reject a mismatched schema.
template <typename... Columns>
constexpr std::uint64_t schema_fingerprint() {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto    mix  = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    mix(sizeof...(Columns));
    (mix(sizeof(Columns)), ...);
    (mix(alignof(Columns)), ...);
    return hash;
}

/// @brief Append a trivially-copyable value to a byte buffer.
template <typename T>
void append_pod(std::vector<std::byte>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

/// @brief Append raw bytes of a contiguous range of trivially-copyable values.
template <typename T>
void append_array(std::vector<std::byte>& out, std::span<const T> values) {
    const auto* bytes = reinterpret_cast<const std::byte*>(values.data());
    out.insert(out.end(), bytes, bytes + values.size_bytes());
}

/// @brief Sequential little-state reader over a byte span with bounds checking.
struct byte_reader {
    std::span<const std::byte> data;
    std::size_t                offset = 0;
    bool                       ok     = true;

    template <typename T>
    T read_pod() {
        T value {};
        if (!ok || offset + sizeof(T) > data.size()) {
            ok = false;
            return value;
        }
        std::memcpy(&value, data.data() + offset, sizeof(T));
        offset += sizeof(T);
        return value;
    }

    template <typename T>
    void read_into(T* dst, std::size_t count) {
        const std::size_t bytes = count * sizeof(T);
        if (!ok || offset + bytes > data.size()) {
            ok = false;
            return;
        }
        std::memcpy(dst, data.data() + offset, bytes);
        offset += bytes;
    }
};

}  // namespace detail

/// @brief Serialize a table to a self-describing byte buffer (trivially-copyable columns only).
/// @tparam Columns The table's column types.
/// @param table The table to serialize.
/// @return A byte buffer that load() can reconstruct the table from.
template <typename Storage, typename... Columns>
[[nodiscard]] std::vector<std::byte> save(const basic_soa_table<Storage, Columns...>& table) {
    static_assert(
        (std::is_trivially_copyable_v<Columns> && ...),
        "save() requires every column type to be trivially copyable."
    );
    static_assert(
        basic_soa_table<Storage, Columns...>::column_count <= 64,
        "save() supports up to 64 columns (the signature is stored as a 64-bit word)."
    );

    using access = detail::table_access<Storage, Columns...>;
    std::vector<std::byte> out;

    const auto& rows       = access::rows(table);
    const auto& free_links = access::free_links(table);

    out.insert(out.end(), detail::serialize_magic.begin(), detail::serialize_magic.end());
    detail::append_pod(out, detail::serialize_version);
    detail::append_pod(out, detail::schema_fingerprint<Columns...>());
    detail::append_pod(out, static_cast<std::uint64_t>(rows.size()));
    detail::append_pod(out, access::free_head(table));
    detail::append_pod(out, static_cast<std::uint64_t>(access::alive_count(table)));

    for (const auto& meta : rows) {
        detail::append_pod(out, meta.generation);
        detail::append_pod(out, static_cast<std::uint64_t>(meta.signature.to_ullong()));
        detail::append_pod(out, static_cast<std::uint8_t>(meta.alive ? 1 : 0));
    }
    detail::append_array(out, std::span<const std::uint32_t>(free_links.data(), free_links.size()));

    // Each column: its dense row indices followed by its raw values, in declaration order.
    std::apply(
        [&out](const auto&... pool) {
            (
                [&out](const auto& column_pool) {
                    const auto& dense = column_pool.dense_rows();
                    const auto& data  = column_pool.raw_data();
                    detail::append_pod(out, static_cast<std::uint64_t>(dense.size()));
                    detail::append_array(
                        out, std::span<const std::uint32_t>(dense.data(), dense.size())
                    );
                    using pool_type = std::decay_t<decltype(column_pool)>;
                    if constexpr (pool_type::is_contiguous) {
                        using value_type = std::decay_t<decltype(data[0])>;
                        detail::append_array(
                            out, std::span<const value_type>(data.data(), data.size())
                        );
                    } else {
                        // Tiled storage is not contiguous; write each value individually.
                        for (std::size_t i = 0; i < data.size(); ++i) {
                            detail::append_pod(out, data[i]);
                        }
                    }
                }(pool),
                ...);
        },
        access::columns(table)
    );

    return out;
}

/// @brief Reconstruct a table from a buffer produced by save().
/// @tparam Columns The table's column types (must match the schema that was saved).
/// @param table The table to overwrite (cleared first).
/// @param bytes The serialized buffer.
/// @return serialize_status::ok on success, or the reason the buffer was rejected.
template <typename Storage, typename... Columns>
[[nodiscard]] serialize_status load(
    basic_soa_table<Storage, Columns...>& table, std::span<const std::byte> bytes
) {
    static_assert(
        (std::is_trivially_copyable_v<Columns> && ...),
        "load() requires every column type to be trivially copyable."
    );
    static_assert(
        basic_soa_table<Storage, Columns...>::column_count <= 64,
        "load() supports up to 64 columns (the signature is stored as a 64-bit word)."
    );

    using access = detail::table_access<Storage, Columns...>;

    detail::byte_reader reader {bytes};

    std::array<std::byte, 8> magic {};
    reader.read_into(magic.data(), magic.size());
    if (!reader.ok) {
        return serialize_status::truncated;
    }
    if (magic != detail::serialize_magic) {
        return serialize_status::bad_magic;
    }
    if (reader.read_pod<std::uint32_t>() != detail::serialize_version) {
        return serialize_status::version_mismatch;
    }
    if (reader.read_pod<std::uint64_t>() != detail::schema_fingerprint<Columns...>()) {
        return serialize_status::schema_mismatch;
    }

    const auto row_slots   = static_cast<std::size_t>(reader.read_pod<std::uint64_t>());
    const auto free_head   = reader.read_pod<std::uint32_t>();
    const auto alive_count = static_cast<std::size_t>(reader.read_pod<std::uint64_t>());
    if (!reader.ok) {
        return serialize_status::truncated;
    }

    table.clear();

    auto& rows = access::rows(table);
    rows.resize(row_slots);
    for (auto& meta : rows) {
        meta.generation = reader.read_pod<std::uint32_t>();
        const auto sig  = reader.read_pod<std::uint64_t>();
        meta.signature  = typename basic_soa_table<Storage, Columns...>::signature_type {sig};
        meta.alive      = reader.read_pod<std::uint8_t>() != 0;
    }

    auto& free_links = access::free_links(table);
    free_links.resize(row_slots);
    reader.read_into(free_links.data(), row_slots);

    access::free_head(table)   = free_head;
    access::alive_count(table) = alive_count;
    if (!reader.ok) {
        table.clear();
        return serialize_status::truncated;
    }

    // Replay each column's dense entries through emplace, rebuilding the sparse index.
    bool columns_ok = true;
    std::apply(
        [&](auto&... pool) {
            (
                [&](auto& column_pool) {
                    if (!columns_ok) {
                        return;
                    }
                    using value_type = std::decay_t<decltype(column_pool.raw_data()[0])>;
                    column_pool.clear();
                    const auto count = static_cast<std::size_t>(reader.read_pod<std::uint64_t>());
                    if (!reader.ok) {
                        columns_ok = false;
                        return;
                    }
                    std::vector<std::uint32_t> dense(count);
                    reader.read_into(dense.data(), count);
                    std::vector<value_type> data(count);
                    reader.read_into(data.data(), count);
                    if (!reader.ok) {
                        columns_ok = false;
                        return;
                    }
                    for (std::size_t i = 0; i < count; ++i) {
                        column_pool.emplace(dense[i], data[i]);
                    }
                }(pool),
                ...);
        },
        access::columns(table)
    );

    if (!columns_ok) {
        table.clear();
        return serialize_status::truncated;
    }
    return serialize_status::ok;
}

}  // namespace soatable
