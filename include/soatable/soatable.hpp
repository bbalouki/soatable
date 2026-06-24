#pragma once

#include <algorithm>
#include <bit>
#include <bitset>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <future>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#if __has_include(<print>)
#include <print>
#define SOATABLE_HAS_PRINT 1
#else
#define SOATABLE_HAS_PRINT 0
#endif

// Freestanding / no-exceptions mode. Define SOATABLE_NO_EXCEPTIONS to compile without throwing:
// every internal `throw` is redirected to a terminating handler, and callers use the non-throwing
// get_expected<T>() / try_get<T>() accessors for error handling. The library uses no RTTI either.
#ifdef SOATABLE_NO_EXCEPTIONS
#include <cstdio>
#include <cstdlib>
namespace soatable::detail {
/// @brief Terminating handler invoked in place of a throw under SOATABLE_NO_EXCEPTIONS.
/// @param message A human-readable description of the failed precondition.
[[noreturn]] inline void terminate_with(const char* message) noexcept {
    std::fputs("soatable fatal: ", stderr);
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::abort();
}
}  // namespace soatable::detail
#define SOATABLE_THROW(exception_type, message) ::soatable::detail::terminate_with(message)
#else
/// @brief Throw @p exception_type with @p message, or terminate under SOATABLE_NO_EXCEPTIONS.
#define SOATABLE_THROW(exception_type, message) throw exception_type(message)
#endif

/// @brief The main namespace for the soa_table library.
namespace soatable {

// =========================================
// 1. Stable row identity and type helpers
// =========================================

/// @brief A stable identifier for a row in an soa_table.
///
/// Contains an index into the row metadata and a generation count to prevent
/// the ABA problem (referencing a new row with an old handle).
struct row_id {
    /// @brief The index of the row in the table's internal storage.
    std::uint32_t index = 0;
    /// @brief The generation of the row at this index. Incremented when a row is erased.
    std::uint32_t generation = 0;

    /// @brief Default equality operator for comparing two row_ids.
    /// @param other The other row_id to compare against.
    /// @return True if both index and generation match, false otherwise.
    constexpr bool operator==(const row_id& other) const = default;
};

/// @brief Failure categories returned by the non-throwing std::expected accessors.
enum class access_error {
    /// @brief The row_id is stale or out of range.
    invalid_row,
    /// @brief The row is valid but does not carry the requested column.
    missing_column,
};

/// @brief Helper concept to check if a type is present in a parameter pack.
/// @tparam T The type to search for.
/// @tparam Types The parameter pack to search in.
template <typename T, typename... Types>
inline constexpr bool contains_type_v = (std::same_as<T, Types> || ...);

/// @brief Helper to check if all types in a pack are unique.
/// @tparam Types The parameter pack to check for uniqueness.
template <typename... Types>
struct is_unique;

/// @brief Base case for is_unique (empty pack is unique).
template <>
struct is_unique<> {
    /// @brief Result of the uniqueness check.
    static constexpr bool value = true;
};

/// @brief Recursive case for is_unique.
/// @tparam T The first type in the pack.
/// @tparam Rest The remaining types in the pack.
template <typename T, typename... Rest>
struct is_unique<T, Rest...> {
    /// @brief Result of the uniqueness check.
    static constexpr bool value = (!std::same_as<T, Rest> && ...) && is_unique<Rest...>::value;
};

/// @brief Helper to find the compile-time index of a type in a tuple.
/// @tparam T The type to find.
/// @tparam Tuple The tuple type to search in.
template <typename T, typename Tuple>
struct index_of;

/// @brief Specialization of index_of for std::tuple.
/// @tparam T The type to find.
/// @tparam Types The types contained in the tuple.
template <typename T, typename... Types>
struct index_of<T, std::tuple<Types...>> {
    static_assert(
        sizeof...(Types) > 0, "soa_table must contain at least one registered column type."
    );

    /// @brief Internal function to find the index of type T in the pack.
    /// @return The zero-based index of T, or sizeof...(Types) if not found.
    static consteval std::size_t value_of() {
        constexpr bool matches[] = {std::same_as<T, Types>...};
        for (std::size_t i = 0; i < sizeof...(Types); ++i) {
            if (matches[i]) {
                return i;
            }
        }
        return sizeof...(Types);
    }

    /// @brief The compile-time index of type T in Types....
    static constexpr std::size_t value = value_of();
    static_assert(
        value < sizeof...(Types), "The requested type is not registered inside the soa_table."
    );
};

/// @brief Default over-alignment (in bytes) for dense column storage.
///
/// Columns are over-aligned to this boundary (matching Apache Arrow's 64-byte recommendation) so the
/// spans returned by soa_table::column<T>() are ready for aligned SIMD loads and cache-line friendly.
inline constexpr std::size_t simd_alignment = 64;

/// @brief The alignment used for a column of T: the larger of T's natural alignment and simd_alignment.
/// @tparam T The column value type.
template <typename T>
inline constexpr std::size_t column_alignment =
    (alignof(T) > simd_alignment) ? alignof(T) : simd_alignment;

/// @brief Detail namespace for internal implementation details.
namespace detail {

/// @brief Friend accessor granting the opt-in <soatable/serialize.hpp> header access to the private
/// state of a basic_soa_table without widening its public surface. Defined only in that header.
/// @tparam Storage The table's storage policy.
/// @tparam Columns The table's column types.
template <typename Storage, typename... Columns>
struct table_access;

/// @brief A standard-conforming allocator that over-aligns its allocations to Alignment bytes.
/// @tparam T The allocated value type.
/// @tparam Alignment The required alignment in bytes (defaults to column_alignment<T>).
template <typename T, std::size_t Alignment = column_alignment<T>>
struct aligned_allocator {
    static_assert(Alignment >= alignof(T), "Alignment must be at least alignof(T).");

    /// @brief The allocated value type.
    using value_type = T;
    /// @brief The over-alignment in bytes.
    static constexpr std::size_t alignment = Alignment;

    /// @brief Rebind helper so container nodes can be allocated with the same alignment policy.
    template <typename U>
    struct rebind {
        using other = aligned_allocator<U, column_alignment<U>>;
    };

    constexpr aligned_allocator() noexcept = default;

    /// @brief Converting constructor required by the Allocator concept.
    template <typename U, std::size_t OtherAlignment>
    constexpr aligned_allocator(const aligned_allocator<U, OtherAlignment>&) noexcept {}

    /// @brief Allocate n over-aligned elements.
    /// @param count The number of elements.
    /// @return Pointer to the aligned storage.
    [[nodiscard]] T* allocate(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
#ifdef SOATABLE_NO_EXCEPTIONS
            terminate_with("aligned_allocator: allocation size overflow.");
#else
            throw std::bad_alloc();
#endif
        }
        void* ptr = ::operator new(count * sizeof(T), std::align_val_t {Alignment});
        return static_cast<T*>(ptr);
    }

    /// @brief Release storage obtained from allocate().
    /// @param ptr The pointer to release.
    void deallocate(T* ptr, std::size_t) noexcept {
        ::operator delete(ptr, std::align_val_t {Alignment});
    }

    template <typename U, std::size_t OtherAlignment>
    constexpr bool operator==(const aligned_allocator<U, OtherAlignment>&) const noexcept {
        return Alignment == OtherAlignment;
    }
};

/// @brief A vector whose storage is over-aligned to column_alignment<T> for SIMD-friendly columns.
/// @tparam T The element type.
template <typename T>
using aligned_vector = std::vector<T, aligned_allocator<T>>;

/// @brief A tiled (AoSoA-style) dense container: a sequence of fixed-size, individually over-aligned
/// tiles. Growth appends a tile and never copies existing tiles, bounding reallocation cost.
/// @tparam T The element type.
/// @tparam TileSize The number of elements per tile.
///
/// Exposes the subset of the std::vector interface that column_vector relies on, so it is a drop-in
/// alternative storage backend. Elements are not contiguous across tiles, so it is non-contiguous.
template <typename T, std::size_t TileSize>
class tiled_vector {
    static_assert(TileSize > 0, "tiled_vector requires a positive tile size.");

   public:
    /// @brief Number of stored elements.
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    /// @brief Whether the container is empty.
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    /// @brief Mutable element access by flat index.
    [[nodiscard]] T& operator[](std::size_t index) {
        return m_tiles[index / TileSize][index % TileSize];
    }
    /// @brief Const element access by flat index.
    [[nodiscard]] const T& operator[](std::size_t index) const {
        return m_tiles[index / TileSize][index % TileSize];
    }

    /// @brief Construct an element at the end, allocating a new tile when the last one is full.
    template <typename... Args>
    T& emplace_back(Args&&... args) {
        if (m_tiles.empty() || m_tiles.back().size() == TileSize) {
            m_tiles.emplace_back();
            m_tiles.back().reserve(TileSize);
        }
        T& ref = m_tiles.back().emplace_back(std::forward<Args>(args)...);
        ++m_size;
        return ref;
    }

    /// @brief Append a value at the end.
    void push_back(const T& value) { emplace_back(value); }
    /// @brief Append a value at the end (move).
    void push_back(T&& value) { emplace_back(std::move(value)); }

    /// @brief The last element.
    [[nodiscard]] T& back() { return m_tiles.back().back(); }

    /// @brief Remove the last element, dropping the trailing tile when it becomes empty.
    void pop_back() {
        m_tiles.back().pop_back();
        if (m_tiles.back().empty()) {
            m_tiles.pop_back();
        }
        --m_size;
    }

    /// @brief Reserve capacity for at least the given number of elements (rounded up to tiles).
    void reserve(std::size_t capacity) {
        m_tiles.reserve((capacity + TileSize - 1) / TileSize);
    }

    /// @brief Remove all elements.
    void clear() noexcept {
        m_tiles.clear();
        m_size = 0;
    }

    /// @brief Release unused capacity.
    void shrink_to_fit() {
        for (auto& tile_data : m_tiles) {
            tile_data.shrink_to_fit();
        }
        m_tiles.shrink_to_fit();
    }

    /// @brief Number of tiles currently allocated.
    [[nodiscard]] std::size_t tile_count() const noexcept { return m_tiles.size(); }
    /// @brief A contiguous, over-aligned span over one tile's live elements.
    [[nodiscard]] std::span<T> tile(std::size_t tile_index) {
        return std::span<T>(m_tiles[tile_index].data(), m_tiles[tile_index].size());
    }
    /// @brief A contiguous, over-aligned const span over one tile's live elements.
    [[nodiscard]] std::span<const T> tile(std::size_t tile_index) const {
        return std::span<const T>(m_tiles[tile_index].data(), m_tiles[tile_index].size());
    }

   private:
    std::vector<aligned_vector<T>> m_tiles;
    std::size_t                    m_size = 0;
};

/// @brief Generates a mask with the specified number of low bits set.
/// @tparam UInt The unsigned integer type.
/// @param bits The number of bits to set.
/// @return The generated mask with 'bits' low bits set.
template <typename UInt>
constexpr UInt low_bits_mask(std::size_t bits) {
    static_assert(std::is_unsigned_v<UInt>, "low_bits_mask requires an unsigned integer type.");
    constexpr std::size_t digits = std::numeric_limits<UInt>::digits;
    if (bits >= digits) {
        return static_cast<UInt>(~UInt {0});
    }
    if (bits == 0) {
        return UInt {0};
    }
    return static_cast<UInt>((UInt {1} << bits) - UInt {1});
}

/// @brief Helper to check if a type is an instance of std::optional.
/// @tparam T The type to check.
template <typename T>
struct is_optional : std::false_type {};

/// @brief Specialization for std::optional.
/// @tparam T The underlying type.
template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

/// @brief Helper boolean to check if a type is an instance of std::optional.
/// @tparam T The type to check.
template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

/// @brief Extracts the value type from a std::optional.
/// @tparam T The type to extract from.
template <typename T>
struct optional_value_type {
    /// @brief The extracted type.
    using type = T;
};

/// @brief Specialization for std::optional to extract its value type.
/// @tparam T The underlying type.
template <typename T>
struct optional_value_type<std::optional<T>> {
    /// @brief The extracted type.
    using type = T;
};

/// @brief Helper alias to extract the value type from a std::optional or the type itself.
/// @tparam T The type to extract from.
template <typename T>
using optional_value_type_t = typename optional_value_type<T>::type;

/// @brief The return type of a selection query.
/// @tparam Self The soa_table type (const or non-const).
/// @tparam ReqColumns The requested column types.
template <typename Self, typename... ReqColumns>
using select_result_t = std::tuple<
    row_id,
    std::conditional_t<
        is_optional_v<ReqColumns>,
        std::optional<std::reference_wrapper<std::conditional_t<
            std::is_const_v<Self>,
            const optional_value_type_t<ReqColumns>,
            optional_value_type_t<ReqColumns>>>>,
        std::reference_wrapper<std::conditional_t<
            std::is_const_v<Self>,
            const optional_value_type_t<ReqColumns>,
            optional_value_type_t<ReqColumns>>>>...>;

}  // namespace detail

// =================================
// 1b. Column storage policies
// =================================

/// @brief Storage policy: a flat, contiguous column allocated with a caller-chosen allocator.
/// @tparam Allocator A single-argument allocator template applied per element type
/// (e.g. std::pmr::polymorphic_allocator). Compose with custom_soa_table.
template <template <typename> class Allocator>
struct soa_layout_with {
    /// @brief The dense container backing a column of T, using the chosen allocator.
    template <typename T>
    using container_type = std::vector<T, Allocator<T>>;
    /// @brief A flat column is a single contiguous block.
    static constexpr bool is_contiguous = true;
};

namespace detail {
/// @brief The default column allocator: over-aligned to column_alignment<T> for SIMD-friendly spans.
template <typename T>
using default_column_allocator = aligned_allocator<T>;
}  // namespace detail

/// @brief Storage policy: a flat, contiguous, over-aligned column (the default Structure-of-Arrays
/// layout). column<T>() exposes a single contiguous span.
struct soa_layout : soa_layout_with<detail::default_column_allocator> {};

/// @brief Storage policy: a tiled (AoSoA-style) column whose dense values live in fixed-size,
/// individually over-aligned tiles. Bounds reallocation cost on growth and is the natural unit for
/// chunked SIMD kernels; not contiguous, so use column_tiles<T>() instead of column<T>().
/// @tparam TileSize The number of elements per tile.
template <std::size_t TileSize>
struct aosoa_layout {
    /// @brief The dense container backing a column of T.
    template <typename T>
    using container_type = detail::tiled_vector<T, TileSize>;
    /// @brief Tiled columns are not a single contiguous block.
    static constexpr bool is_contiguous = false;
    /// @brief The number of elements per tile.
    static constexpr std::size_t tile_size = TileSize;
};

// =================================
// 2. Compression and state helpers
// =================================

/// @brief A helper to store a floating-point value in a quantized integer format.
/// @tparam StorageType The unsigned integer type used for storage.
/// @tparam MinM1000 The minimum value multiplied by 1000.
/// @tparam MaxM1000 The maximum value multiplied by 1000.
/// @tparam Bits The number of bits to use for quantization.
template <
    typename StorageType = std::uint16_t,
    int         MinM1000 = 0,
    int         MaxM1000 = 1000000,
    std::size_t Bits     = 16>
    requires std::is_unsigned_v<StorageType>
struct quantized_float {
    static_assert(Bits > 0, "quantized_float requires at least one bit.");
    static_assert(
        Bits <= std::numeric_limits<StorageType>::digits,
        "Bits cannot exceed the storage type width."
    );
    static_assert(
        MaxM1000 > MinM1000, "quantized_float requires MaxM1000 to be greater than MinM1000."
    );

    /// @brief The underlying quantized integer value.
    StorageType quantized_value = 0;

    /// @brief The minimum representable value.
    static constexpr double Min = MinM1000 / 1000.0;
    /// @brief The maximum representable value.
    static constexpr double Max = MaxM1000 / 1000.0;
    /// @brief The maximum possible quantized value.
    static constexpr StorageType MaxVal = detail::low_bits_mask<StorageType>(Bits);

    /// @brief Default constructor. Initializes the quantized value to zero.
    constexpr quantized_float() = default;
    /// @brief Construct from a double value.
    /// @param value The floating-point value to quantize.
    constexpr quantized_float(double value) { set(value); }

    /// @brief Quantize and set the value.
    /// @param value The floating-point value to quantize. Will be clamped to [Min, Max].
    constexpr void set(double value) {
        const double clamped    = std::clamp(value, Min, Max);
        const double normalized = (clamped - Min) / (Max - Min);
        const auto   quantized =
            static_cast<StorageType>(normalized * static_cast<double>(MaxVal) + 0.5);
        quantized_value = std::min(quantized, MaxVal);
    }

    /// @brief Dequantize and get the double value.
    /// @return The dequantized double value.
    constexpr double get() const {
        return Min +
               (static_cast<double>(quantized_value) / static_cast<double>(MaxVal)) * (Max - Min);
    }

    /// @brief Implicit conversion to double.
    /// @return The dequantized double value.
    constexpr operator double() const { return get(); }

    /// @brief Assignment from double.
    /// @param value The floating-point value to quantize.
    /// @return Reference to this object.
    constexpr quantized_float& operator=(double value) {
        set(value);
        return *this;
    }
};

/// @brief A helper to pack multiple small values into a single integer.
/// @tparam ContainerType The unsigned integer type used as the container.
/// @tparam ValueType The type of the value being packed.
/// @tparam Offset The bit offset in the container.
/// @tparam Bits The number of bits for the value.
template <typename ContainerType, typename ValueType, std::size_t Offset, std::size_t Bits>
    requires std::is_unsigned_v<ContainerType>
struct packed_bits {
    static_assert(Bits > 0, "packed_bits requires at least one bit.");
    static_assert(
        Offset < std::numeric_limits<ContainerType>::digits, "Offset exceeds the container width."
    );
    static_assert(
        Offset + Bits <= std::numeric_limits<ContainerType>::digits,
        "Offset + Bits cannot exceed the container width."
    );

    /// @brief Bitmask for the value within the container.
    static constexpr ContainerType ValueMask =
        static_cast<ContainerType>(detail::low_bits_mask<ContainerType>(Bits) << Offset);

    /// @brief Set the value in the container at the configured offset and width.
    /// @param container The container integer to modify.
    /// @param value The value to pack into the container.
    static constexpr void set(ContainerType& container, ValueType value) {
        container = static_cast<ContainerType>(
            (container & ~ValueMask) | ((static_cast<ContainerType>(value) << Offset) & ValueMask)
        );
    }

    /// @brief Get the value from the container at the configured offset and width.
    /// @param container The container integer to extract from.
    /// @return The extracted value.
    static constexpr ValueType get(ContainerType container) {
        return static_cast<ValueType>((container & ValueMask) >> Offset);
    }
};

/// @brief Tracks a value relative to a baseline, using a scaled delta.
/// @tparam T The type of the value (usually float or double).
/// @tparam DeltaType The type used for the delta (e.g., int16_t).
/// @tparam ScaleM1000 The scale factor multiplied by 1000.
template <typename T = double, typename DeltaType = std::int16_t, int ScaleM1000 = 10>
class delta_value {
    static_assert(ScaleM1000 > 0, "delta_value requires a positive scale.");

   private:
    T                       m_value {};
    static constexpr double Scale = ScaleM1000 / 1000.0;

   public:
    /// @brief Default constructor. Initializes the value to zero.
    constexpr delta_value() = default;
    /// @brief Construct with an initial value.
    /// @param initial The initial absolute value.
    constexpr explicit delta_value(T initial) : m_value(initial) {}

    /// @brief Set the absolute value.
    /// @param value The new absolute value.
    constexpr void set(T value) { m_value = value; }

    /// @brief Apply a delta to the current value.
    /// @param delta The delta to apply, which will be multiplied by the scale.
    constexpr void apply_delta(DeltaType delta) { m_value += static_cast<T>(delta) * Scale; }

    /// @brief Calculate the best-fit delta for a new absolute value.
    /// @param new_value The target absolute value.
    /// @return The delta that, when applied, brings the current value closest to new_value.
    [[nodiscard]] DeltaType get_delta(T new_value) const {
        const T      diff      = new_value - m_value;
        const double scaled    = static_cast<double>(diff) / Scale;
        const double rounded   = scaled >= 0.0 ? std::floor(scaled + 0.5) : std::ceil(scaled - 0.5);
        const auto   min_value = static_cast<double>(std::numeric_limits<DeltaType>::min());
        const auto   max_value = static_cast<double>(std::numeric_limits<DeltaType>::max());
        const double clamped   = std::clamp(rounded, min_value, max_value);
        return static_cast<DeltaType>(clamped);
    }

    /// @brief Get the current absolute value.
    /// @return The current value.
    [[nodiscard]] constexpr T get() const { return m_value; }
    /// @brief Implicit conversion to T.
    /// @return The current value.
    constexpr operator T() const { return get(); }
};

/// @brief Manages a set of dirty flags efficiently.
/// @tparam EnumType The enum type defining the flags.
/// @tparam MaskType The unsigned integer type used for the mask.
template <typename EnumType, typename MaskType = std::uint32_t>
    requires std::is_enum_v<EnumType>
class dirty_mask {
    static_assert(std::is_unsigned_v<MaskType>, "dirty_mask requires an unsigned mask type.");

   private:
    MaskType m_mask = 0;

   public:
    /// @brief Mark a flag as dirty.
    /// @param flag The flag to set in the mask.
    constexpr void mark_dirty(EnumType flag) { m_mask |= static_cast<MaskType>(flag); }

    /// @brief Clear a dirty flag.
    /// @param flag The flag to clear in the mask.
    constexpr void clear_dirty(EnumType flag) {
        m_mask &= static_cast<MaskType>(~static_cast<MaskType>(flag));
    }

    /// @brief Check if a flag is dirty.
    /// @param flag The flag to check.
    /// @return True if the flag is set, false otherwise.
    [[nodiscard]] constexpr bool is_dirty(EnumType flag) const {
        return (m_mask & static_cast<MaskType>(flag)) != 0;
    }

    /// @brief Check if any flags are dirty.
    /// @return True if the mask is non-zero, false otherwise.
    [[nodiscard]] constexpr bool is_any_dirty() const { return m_mask != 0; }
    /// @brief Reset all flags to clean (zero).
    constexpr void reset() { m_mask = 0; }
    /// @brief Get the raw mask.
    /// @return The underlying mask integer.
    [[nodiscard]] constexpr MaskType get_mask() const { return m_mask; }
};

// =========================
// 2b. Validity bitmap
// =========================

/// @brief A packed bit container for per-row column presence (Arrow-style validity).
///
/// Bits are stored little-endian within 64-bit words so the raw words() can be handed to an
/// Arrow-compatible consumer. Produced by soa_table::validity<T>(); see that method for semantics.
class bitmap {
   public:
    /// @brief The word type backing the bitmap.
    using word_type = std::uint64_t;
    /// @brief Number of bits per backing word.
    static constexpr std::size_t bits_per_word = std::numeric_limits<word_type>::digits;

    /// @brief Construct an all-clear bitmap of the given bit length.
    /// @param bit_count The number of bits the bitmap addresses.
    explicit bitmap(std::size_t bit_count)
        : m_words((bit_count + bits_per_word - 1) / bits_per_word, word_type {0}),
          m_size(bit_count) {}

    /// @brief Set the bit at the given position.
    /// @param index The bit position.
    void set(std::size_t index) {
        m_words[index / bits_per_word] |= word_type {1} << (index % bits_per_word);
    }

    /// @brief Test the bit at the given position.
    /// @param index The bit position.
    /// @return True if the bit is set, false otherwise.
    [[nodiscard]] bool test(std::size_t index) const {
        return (m_words[index / bits_per_word] >> (index % bits_per_word) & word_type {1}) != 0;
    }

    /// @brief Number of bits the bitmap addresses.
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }

    /// @brief Number of set bits.
    [[nodiscard]] std::size_t count() const {
        std::size_t total = 0;
        for (const word_type word : m_words) {
            total += static_cast<std::size_t>(std::popcount(word));
        }
        return total;
    }

    /// @brief The raw backing words (little-endian bit order), suitable for Arrow interchange.
    [[nodiscard]] std::span<const word_type> words() const noexcept {
        return std::span<const word_type>(m_words.data(), m_words.size());
    }

    /// @brief Invoke a callback for each set bit position, in ascending order, branch-free per
    /// word.
    /// @tparam Func A callable accepting a std::size_t bit position.
    /// @param func The callback.
    template <typename Func>
    void for_each_set(Func&& func) const {
        for (std::size_t word_index = 0; word_index < m_words.size(); ++word_index) {
            word_type remaining = m_words[word_index];
            while (remaining != 0) {
                const auto bit = static_cast<std::size_t>(std::countr_zero(remaining));
                std::invoke(func, word_index * bits_per_word + bit);
                remaining &= remaining - 1;  // Clear the lowest set bit.
            }
        }
    }

   private:
    std::vector<word_type> m_words;
    std::size_t            m_size = 0;
};

// =========================
// 3. Sparse column storage
// =========================

/// @brief A sparse-to-dense storage for a single column.
///
/// Uses a sparse array (mapping row index to dense index) and a dense array
/// (storing the actual data and the back-mapping to row index).
/// This allows for O(1) insertion, deletion, and iteration over present values.
/// @tparam T The column value type.
/// @tparam Storage The storage policy selecting the dense backend (soa_layout or aosoa_layout).
template <typename T, typename Storage = soa_layout>
class column_vector {
   public:
    /// @brief Unsigned index type for dense and sparse positions.
    ///
    /// Widening this to `std::uint64_t` is the single change required for a 64-bit build; every
    /// internal index derives from it. Kept at 32 bits to match `row_id::index`.
    using size_type = std::uint32_t;
    /// @brief The dense storage type chosen by the storage policy.
    using data_vector = typename Storage::template container_type<T>;
    /// @brief Whether the dense storage is a single contiguous block.
    static constexpr bool is_contiguous = Storage::is_contiguous;

   private:
    /// @brief Sentinel stored in m_sparse for a row that has no value in this column.
    static constexpr size_type npos = std::numeric_limits<size_type>::max();
    /// @brief Maps row_index -> dense_index (npos when absent).
    std::vector<size_type> m_sparse;
    /// @brief Maps dense_index -> row_index.
    std::vector<size_type> m_dense;
    /// @brief The actual data stored densely (over-aligned for SIMD-friendly column spans).
    data_vector m_data;

   public:
    /// @brief Default constructor.
    column_vector() = default;

    /// @brief Reserve capacity for the vectors.
    /// @param capacity The number of elements to reserve space for.
    void reserve(std::size_t capacity) {
        m_sparse.reserve(capacity);
        m_dense.reserve(capacity);
        m_data.reserve(capacity);
    }

    /// @brief Clear all data from the column.
    void clear() {
        m_sparse.clear();
        m_dense.clear();
        m_data.clear();
    }

    /// @brief Shrink vectors to fit their current size, freeing unused memory.
    void shrink_to_fit() {
        m_sparse.shrink_to_fit();
        m_dense.shrink_to_fit();
        m_data.shrink_to_fit();
    }

    /// @brief Check if the column has a value for the given row index.
    /// @param row_index The internal index of the row.
    /// @return True if a value is present, false otherwise.
    [[nodiscard]] bool contains(std::uint32_t row_index) const {
        return row_index < m_sparse.size() && m_sparse[row_index] != npos;
    }

    /// @brief Try to get a pointer to the value at the given row index.
    /// @param row_index The internal index of the row.
    /// @return Pointer to the value, or nullptr if not present.
    [[nodiscard]] T* try_get(std::uint32_t row_index) {
        if (!contains(row_index)) {
            return nullptr;
        }
        return &m_data[m_sparse[row_index]];
    }

    /// @brief Try to get a const pointer to the value at the given row index.
    /// @param row_index The internal index of the row.
    /// @return Const pointer to the value, or nullptr if not present.
    [[nodiscard]] const T* try_get(std::uint32_t row_index) const {
        if (!contains(row_index)) {
            return nullptr;
        }
        return &m_data[m_sparse[row_index]];
    }

    /// @brief Get a reference to the value at the given row index. Asserts if not present.
    /// @param row_index The internal index of the row.
    /// @return Reference to the value.
    T& get(std::uint32_t row_index) {
        assert(contains(row_index) && "Out of bounds sparse column reference.");
        return m_data[m_sparse[row_index]];
    }

    /// @brief Get a const reference to the value at the given row index. Asserts if not present.
    /// @param row_index The internal index of the row.
    /// @return Const reference to the value.
    const T& get(std::uint32_t row_index) const {
        assert(contains(row_index) && "Out of bounds sparse column reference.");
        return m_data[m_sparse[row_index]];
    }

    /// @brief Emplace a value at the given row index.
    /// @tparam Args Argument types for the constructor of T.
    /// @param row_index The internal index of the row.
    /// @param args Arguments to pass to the constructor of T.
    /// @return Reference to the emplaced value.
    /// @note Strong exception guarantee when inserting a new value: the dense storage is grown and
    /// only committed through the sparse slot once every throwing step has succeeded. Overwriting
    /// an existing value offers the guarantee of T's assignment (strong for nothrow-assignable
    /// types).
    template <typename... Args>
    T& emplace(std::uint32_t row_index, Args&&... args) {
        if (row_index >= m_sparse.size()) {
            m_sparse.resize(static_cast<std::size_t>(row_index) + 1, npos);
        }

        if (m_sparse[row_index] != npos) {
            const size_type dense_index = m_sparse[row_index];
            m_data[dense_index]         = T(std::forward<Args>(args)...);
            return m_data[dense_index];
        }

        // Reserve the back-mapping first so its push_back cannot reallocate (and thus cannot throw)
        // after the data element is constructed; the sparse slot is written last as the commit.
        m_dense.reserve(m_dense.size() + 1);
        m_data.emplace_back(std::forward<Args>(args)...);
        m_dense.push_back(row_index);
        m_sparse[row_index] = static_cast<size_type>(m_dense.size() - 1);
        return m_data.back();
    }

    /// @brief Remove the value at the given row index.
    /// @param row_index The internal index of the row.
    void remove(std::uint32_t row_index) {
        if (!contains(row_index)) {
            return;
        }

        const size_type dense_index    = m_sparse[row_index];
        const size_type last_row_index = m_dense.back();

        // Swap with last to keep the dense arrays contiguous.
        if (dense_index != m_data.size() - 1) {
            m_data[dense_index]      = std::move(m_data.back());
            m_dense[dense_index]     = m_dense.back();
            m_sparse[last_row_index] = dense_index;
        }

        m_sparse[row_index] = npos;
        m_data.pop_back();
        m_dense.pop_back();
    }

    /// @brief Physically reorder the dense storage based on a new row order.
    /// @param row_order The new order of row indices.
    /// @note Basic exception guarantee: if a contained element's move constructor throws, the
    /// column is left in a valid but unspecified state. The index rebuild after the move loop is
    /// no-throw, so the sparse/dense/data arrays are never left mutually inconsistent.
    void reorder(const std::vector<std::uint32_t>& row_order) {
        data_vector            new_data;
        std::vector<size_type> new_dense;
        new_data.reserve(m_data.size());
        new_dense.reserve(m_dense.size());

        for (const size_type row_index : row_order) {
            if (contains(row_index)) {
                new_data.push_back(std::move(get(row_index)));
                new_dense.push_back(row_index);
            }
        }

        std::vector<size_type> new_sparse(m_sparse.size(), npos);
        for (std::size_t i = 0; i < new_dense.size(); ++i) {
            new_sparse[new_dense[i]] = static_cast<size_type>(i);
        }

        m_sparse = std::move(new_sparse);
        m_data   = std::move(new_data);
        m_dense  = std::move(new_dense);
    }

    /// @brief Number of present values in the column.
    [[nodiscard]] std::size_t size() const { return m_dense.size(); }
    /// @brief Is the column empty?
    [[nodiscard]] bool empty() const { return m_dense.empty(); }
    /// @brief Get the list of row indices that have values in this column.
    [[nodiscard]] const std::vector<std::uint32_t>& dense_rows() const { return m_dense; }
    /// @brief Access the raw dense data.
    data_vector& raw_data() { return m_data; }
    /// @brief Access the raw dense data (const).
    const data_vector& raw_data() const { return m_data; }
};

// ===================================
// 4. Reference-semantic row proxy
// ===================================

/// @brief A lightweight, reference-semantic view of one row over a set of required columns.
///
/// row_view models the tuple protocol, so structured bindings yield real references instead of the
/// `std::reference_wrapper` tuples that select() returns:
/// @code
/// for (auto [id, pos, vel] : table.view<Position, Velocity>()) {
///     pos.x += vel.x;  // pos and vel are real references into the columns.
/// }
/// // or, by column type:
/// for (auto row : table.view<Position, Velocity>()) {
///     row.get<Position>().x += row.get<Velocity>().x;
/// }
/// @endcode
/// A row_view is invalidated by the same operations that invalidate column references (erase,
/// sort).
template <typename... Cols>
class row_view {
   public:
    /// @brief Construct from a row identity and one pointer per viewed column.
    /// @param id The identity of the row.
    /// @param columns One pointer per column, in declaration order.
    constexpr row_view(row_id id, Cols*... columns) noexcept : m_id(id), m_columns(columns...) {}

    /// @brief The identity of this row.
    [[nodiscard]] constexpr row_id id() const noexcept { return m_id; }

    /// @brief Access a viewed column by its type.
    /// @tparam T The column type, exactly as named in the view() call.
    /// @return Reference to the column value.
    template <typename T>
    [[nodiscard]] constexpr T& get() const noexcept {
        return *std::get<T*>(m_columns);
    }

    /// @brief Tuple-protocol accessor: index 0 is the row_id, indices 1..N are column references.
    /// @tparam Index The structured-binding position.
    template <std::size_t Index>
    [[nodiscard]] constexpr decltype(auto) get() const noexcept {
        if constexpr (Index == 0) {
            return m_id;
        } else {
            return (*std::get<Index - 1>(m_columns));
        }
    }

   private:
    row_id               m_id;
    std::tuple<Cols*...> m_columns;
};

/// @brief Maps a structured-binding index to the corresponding row_view element type.
/// @tparam Index The binding position (0 is the row_id).
/// @tparam Cols The viewed column types.
template <std::size_t Index, typename... Cols>
struct row_view_element {
    /// @brief The element type at the given index.
    using type = std::tuple_element_t<Index - 1, std::tuple<Cols...>>;
};

/// @brief Specialization for index 0, which yields the row identity.
/// @tparam Cols The viewed column types.
template <typename... Cols>
struct row_view_element<0, Cols...> {
    /// @brief The element type at index 0.
    using type = row_id;
};

// ===========================
// 5. Relational column table
// ===========================

/// @brief A table where data is stored in sparse columns.
///
/// @par Error-handling contract
/// Accessors come in three flavours so callers can pick their failure mode:
/// - get<T>()        throws std::out_of_range when the column is absent on the row.
/// - try_get<T>()    returns nullptr when the column is absent or the row is invalid.
/// - get_expected<T>() returns std::expected and never throws (for the no-exceptions audience).
///
/// @tparam Storage The column storage policy (soa_layout by default, or aosoa_layout<TileSize>).
/// @tparam Columns The types of the columns in the table. Each type must be unique.
/// @note Use the soa_table / aosoa_table aliases rather than naming basic_soa_table directly.
template <typename Storage, typename... Columns>
class basic_soa_table {
   public:
    static_assert(sizeof...(Columns) > 0, "soa_table must contain at least one column type.");
    static_assert(
        is_unique<Columns...>::value,
        "All registered columns in an soa_table must have unique types."
    );

    /// @brief Total number of registered columns.
    static constexpr std::size_t column_count = sizeof...(Columns);
    /// @brief Bitset used to track which columns are present for a row.
    using signature_type = std::bitset<column_count>;

    /// @brief Metadata for a single row slot.
    struct row_meta {
        /// @brief Generation count for handle stability.
        std::uint32_t generation = 0;
        /// @brief Bitmask of present columns.
        signature_type signature {};
        /// @brief Whether the slot is currently occupied.
        bool alive = false;
    };

    /// @brief Helper to check if a type T is a registered column.
    template <typename T>
    static constexpr bool registered_column_v = contains_type_v<T, Columns...>;

   private:
    static constexpr std::uint32_t npos = std::numeric_limits<std::uint32_t>::max();
    /// @brief Generation value at which a slot is retired instead of recycled (ABA wraparound
    /// bound).
    static constexpr std::uint32_t max_generation = std::numeric_limits<std::uint32_t>::max();
    /// @brief Alive-row count below which sort_by_column_parallel falls back to a serial sort.
    ///
    /// Below this size the per-column task-launch overhead dominates the work saved, so the
    /// parallel path is not worthwhile. The crossover is machine dependent; this is a conservative
    /// default measured to favour the serial path for small tables.
    static constexpr std::size_t parallel_sort_threshold = 4096;

    /// @brief Metadata for all row slots.
    std::vector<row_meta> m_rows;
    /// @brief Linked list of free slots.
    std::vector<std::uint32_t> m_free_links;
    /// @brief Index of the first free slot.
    std::uint32_t m_free_head = npos;
    /// @brief Number of active rows.
    std::size_t m_alive_count = 0;
    /// @brief Storage for each column, using the selected storage policy.
    std::tuple<column_vector<Columns, Storage>...> m_columns;

    /// @brief Grants the opt-in serialization header access to the private state above.
    friend struct detail::table_access<Storage, Columns...>;

    /// @brief Internal check for index overflow.
    [[nodiscard]] static constexpr bool can_address_rows(std::size_t count) {
        return count <= std::numeric_limits<std::uint32_t>::max();
    }

    /// @brief Implementation of rows() iteration.
    template <typename Self>
    static auto rows_impl(Self* self) {
        return std::views::iota(std::size_t {0}, self->m_rows.size()) |
               std::views::filter([self](std::size_t row_index) {
                   return self->m_rows[row_index].alive;
               }) |
               std::views::transform([self](std::size_t row_index) {
                   return row_id {
                       static_cast<std::uint32_t>(row_index), self->m_rows[row_index].generation
                   };
               });
    }

    template <typename Self, typename... ReqColumns>
    static auto get_transform_func(Self* self) {
        return [self](std::uint32_t row_index) -> detail::select_result_t<Self, ReqColumns...> {
            const row_id id {row_index, self->m_rows[row_index].generation};
            auto         get_col = [&]<typename T>(std::uint32_t idx) {
                using ActualT = detail::optional_value_type_t<T>;
                auto* ptr = std::get<column_vector<ActualT, Storage>>(self->m_columns).try_get(idx);
                if constexpr (detail::is_optional_v<T>) {
                    using ResT = std::optional<std::reference_wrapper<
                        std::conditional_t<std::is_const_v<Self>, const ActualT, ActualT>>>;
                    if (ptr)
                        return ResT(*ptr);
                    return ResT {};
                } else {
                    return std::reference_wrapper<
                        std::conditional_t<std::is_const_v<Self>, const ActualT, ActualT>>(*ptr);
                }
            };
            return std::make_tuple(id, get_col.template operator()<ReqColumns>(row_index)...);
        };
    }

   public:
    /// @brief Default constructor.
    basic_soa_table() = default;

    /// @brief Number of active rows in the table.
    [[nodiscard]] std::size_t size() const noexcept { return m_alive_count; }
    /// @brief Number of row slots currently allocated.
    [[nodiscard]] std::size_t row_slots() const noexcept { return m_rows.size(); }
    /// @brief Is the table empty?
    [[nodiscard]] bool empty() const noexcept { return m_alive_count == 0; }

    /// @brief Reserve capacity for rows and columns.
    /// @param capacity The number of rows to reserve space for.
    void reserve(std::size_t capacity) {
        m_rows.reserve(capacity);
        m_free_links.reserve(capacity);
        std::apply([capacity](auto&... pool) { (pool.reserve(capacity), ...); }, m_columns);
    }

    /// @brief Shrink all internal storage to fit current size.
    void shrink_to_fit() {
        m_rows.shrink_to_fit();
        m_free_links.shrink_to_fit();
        std::apply([](auto&... pool) { (pool.shrink_to_fit(), ...); }, m_columns);
    }

    /// @brief Clear all rows and columns. Does not reset generations.
    void clear() {
        m_rows.clear();
        m_free_links.clear();
        m_free_head   = npos;
        m_alive_count = 0;
        std::apply([](auto&... pool) { (pool.clear(), ...); }, m_columns);
    }

    /// @brief Insert a new empty row and return its ID.
    /// @return The row_id of the newly created row.
    /// @note Strong exception guarantee. In the grow path both parallel arrays are reserved before
    /// either is mutated, so a `bad_alloc` cannot leave them at mismatched sizes.
    /// @throws std::overflow_error If the row_id index space is exhausted.
    [[nodiscard]] row_id insert() {
        std::uint32_t index = 0;
        if (m_free_head != npos) {
            index               = m_free_head;
            m_free_head         = m_free_links[index];
            m_rows[index].alive = true;
            m_rows[index].signature.reset();
        } else {
            if (!can_address_rows(m_rows.size() + 1)) {
                SOATABLE_THROW(
                    std::overflow_error, "soa_table exhausted the row_id index space."
                );
            }
            m_rows.reserve(m_rows.size() + 1);
            m_free_links.reserve(m_free_links.size() + 1);
            index = static_cast<std::uint32_t>(m_rows.size());
            m_rows.push_back(row_meta {});
            m_rows.back().alive = true;
            m_free_links.push_back(npos);
        }

        ++m_alive_count;
        return row_id {index, m_rows[index].generation};
    }

    /// @brief Check if a row_id is still valid and alive.
    /// @param id The row_id to check.
    /// @return True if the row is valid and alive, false otherwise.
    [[nodiscard]] bool is_valid(row_id id) const noexcept {
        if (id.index >= m_rows.size()) {
            return false;
        }
        const auto& meta = m_rows[id.index];
        return meta.alive && meta.generation == id.generation;
    }

    /// @brief Erase a row by ID.
    /// @param id The row_id of the row to erase.
    /// @note Basic exception guarantee: column element removal relies on T's move assignment, which
    /// may throw for non-nothrow-movable types, leaving the table valid but in an unspecified
    /// state.
    /// @note ABA safety: a slot's generation defeats stale handles for up to 2^32 erase/reuse
    /// cycles. When that counter would wrap, the slot is retired (never recycled) so a saturated
    /// stale handle can never alias a freshly inserted row. Retiring leaks one slot, which is
    /// acceptable given the cycle count required to reach it.
    void erase(row_id id) {
        if (!is_valid(id)) {
            return;
        }

        const std::uint32_t index = id.index;

        auto clear_row_from_pool = [index]<typename T>(column_vector<T, Storage>& pool) {
            if (pool.contains(index)) {
                pool.remove(index);
            }
        };
        std::apply([&](auto&... pool) { (clear_row_from_pool(pool), ...); }, m_columns);

        auto& meta = m_rows[index];
        meta.alive = false;
        meta.signature.reset();

        if (meta.generation == max_generation) {
            // Retire the saturated slot: leave it dead and off the free list forever.
        } else {
            ++meta.generation;
            m_free_links[index] = m_free_head;
            m_free_head         = index;
        }

        --m_alive_count;
    }

    /// @brief Assign a value to a column for the given row.
    /// @tparam T The type of the column.
    /// @tparam Args Argument types for the constructor of T.
    /// @param id The row_id of the row.
    /// @param args Arguments to pass to the constructor of T.
    /// @return Reference to the assigned value.
    /// @note Strong exception guarantee when adding a new column value (delegates to the strong
    /// path of column_vector::emplace); overwriting an existing value inherits T's assignment
    /// guarantee.
    /// @throws std::out_of_range If the row_id is invalid (thrown before any mutation).
    template <typename T, typename... Args>
        requires registered_column_v<T>
    T& assign(row_id id, Args&&... args) {
        if (!is_valid(id)) {
            SOATABLE_THROW(std::out_of_range, "assign() called with an invalid row_id.");
        }

        auto& pool  = std::get<column_vector<T, Storage>>(m_columns);
        T&    value = pool.emplace(id.index, std::forward<Args>(args)...);
        m_rows[id.index].signature.set(index_of<T, std::tuple<Columns...>>::value);
        return value;
    }

    /// @brief Remove a value from a column for the given row.
    /// @tparam T The type of the column.
    /// @param id The row_id of the row.
    template <typename T>
        requires registered_column_v<T>
    void unassign(row_id id) {
        if (!is_valid(id)) {
            return;
        }

        auto& pool = std::get<column_vector<T, Storage>>(m_columns);
        pool.remove(id.index);
        m_rows[id.index].signature.reset(index_of<T, std::tuple<Columns...>>::value);
    }

    /// @brief Check if a row has a value for the specified column.
    /// @tparam T The type of the column.
    /// @param id The row_id of the row.
    /// @return True if the row has the column, false otherwise.
    template <typename T>
        requires registered_column_v<T>
    [[nodiscard]] bool contains(row_id id) const {
        if (!is_valid(id)) {
            return false;
        }
        return m_rows[id.index].signature.test(index_of<T, std::tuple<Columns...>>::value);
    }

    /// @brief Try to get a pointer to the value of a column for the given row.
    /// @tparam T The type of the column.
    /// @param id The row_id of the row.
    /// @return Pointer to the value, or nullptr if not present or row is invalid.
    template <typename T>
        requires registered_column_v<T>
    T* try_get(row_id id) {
        if (!contains<T>(id)) {
            return nullptr;
        }
        return std::get<column_vector<T, Storage>>(m_columns).try_get(id.index);
    }

    /// @brief Try to get a const pointer to the value of a column for the given row.
    /// @tparam T The type of the column.
    /// @param id The row_id of the row.
    /// @return Const pointer to the value, or nullptr if not present or row is invalid.
    template <typename T>
        requires registered_column_v<T>
    const T* try_get(row_id id) const {
        if (!contains<T>(id)) {
            return nullptr;
        }
        return std::get<column_vector<T, Storage>>(m_columns).try_get(id.index);
    }

    /// @brief Get a reference to the value of a column for the given row. Throws if not present.
    /// @tparam T The type of the column.
    /// @param id The row_id of the row.
    /// @return Reference to the value.
    /// @throws std::out_of_range If the column is not present on the row.
    template <typename T>
        requires registered_column_v<T>
    T& get(row_id id) {
        auto* value = try_get<T>(id);
        if (value == nullptr) {
            SOATABLE_THROW(std::out_of_range, "Requested column is not available on this row.");
        }
        return *value;
    }

    /// @brief Get a const reference to the value of a column for the given row. Throws if not
    /// present.
    /// @tparam T The type of the column.
    /// @param id The row_id of the row.
    /// @return Const reference to the value.
    /// @throws std::out_of_range If the column is not present on the row.
    template <typename T>
        requires registered_column_v<T>
    const T& get(row_id id) const {
        auto* value = try_get<T>(id);
        if (value == nullptr) {
            SOATABLE_THROW(std::out_of_range, "Requested column is not available on this row.");
        }
        return *value;
    }

    /// @brief Non-throwing accessor returning a column reference or an access_error.
    /// @tparam T The type of the column.
    /// @param id The row_id of the row.
    /// @return The column reference on success, or an access_error describing the failure.
    /// @note Never throws. Intended for hot paths and no-exceptions builds.
    template <typename T>
        requires registered_column_v<T>
    [[nodiscard]] std::expected<std::reference_wrapper<T>, access_error> get_expected(row_id id) {
        if (!is_valid(id)) {
            return std::unexpected(access_error::invalid_row);
        }
        T* value = std::get<column_vector<T, Storage>>(m_columns).try_get(id.index);
        if (value == nullptr) {
            return std::unexpected(access_error::missing_column);
        }
        return std::reference_wrapper<T>(*value);
    }

    /// @brief Non-throwing const accessor returning a const column reference or an access_error.
    /// @tparam T The type of the column.
    /// @param id The row_id of the row.
    /// @return The const column reference on success, or an access_error describing the failure.
    /// @note Never throws.
    template <typename T>
        requires registered_column_v<T>
    [[nodiscard]] std::expected<std::reference_wrapper<const T>, access_error> get_expected(
        row_id id
    ) const {
        if (!is_valid(id)) {
            return std::unexpected(access_error::invalid_row);
        }
        const T* value = std::get<column_vector<T, Storage>>(m_columns).try_get(id.index);
        if (value == nullptr) {
            return std::unexpected(access_error::missing_column);
        }
        return std::reference_wrapper<const T>(*value);
    }

    /// @brief Iterate over all alive row IDs.
    [[nodiscard]] auto rows() { return rows_impl(this); }
    /// @brief Iterate over all alive row IDs (const).
    [[nodiscard]] auto rows() const { return rows_impl(this); }

    /// @brief Execute a function for each alive row ID.
    /// @tparam Func The type of the function to execute.
    /// @param func The function to execute. It must accept a row_id.
    template <typename Func>
    void for_each_row(Func&& func) {
        for (row_id id : rows()) {
            std::invoke(func, id);
        }
    }

    /// @brief Execute a function for each alive row ID (const).
    /// @tparam Func The type of the function to execute.
    /// @param func The function to execute. It must accept a row_id.
    template <typename Func>
    void for_each_row(Func&& func) const {
        for (row_id id : rows()) {
            std::invoke(func, id);
        }
    }

    /// @brief Select rows that have all specified columns and provide access to them.
    /// @tparam ReqColumns The requested column types. Use std::optional<T> for optional columns.
    /// @return A range of tuples containing (row_id, reference-wrapped columns...).
    template <typename... ReqColumns>
    auto select() {
        static_assert(
            sizeof...(ReqColumns) > 0, "Must provide at least one column type for projection."
        );
        static_assert(
            (registered_column_v<detail::optional_value_type_t<ReqColumns>> && ...),
            "One or more requested columns are not registered inside the soa_table schema."
        );

        signature_type required_mask;
        (
            [&] {
                if constexpr (!detail::is_optional_v<ReqColumns>)
                    required_mask.set(index_of<ReqColumns, std::tuple<Columns...>>::value);
            }(),
            ...);

        constexpr bool has_required = (!detail::is_optional_v<ReqColumns> || ...);

        if constexpr (!has_required) {
            return std::views::iota(std::size_t {0}, m_rows.size()) |
                   std::views::filter([this](std::size_t row_index) {
                       return m_rows[row_index].alive;
                   }) |
                   std::views::transform([this](std::size_t row_index) {
                       return get_transform_func<basic_soa_table, ReqColumns...>(this)(
                           static_cast<std::uint32_t>(row_index)
                       );
                   });
        } else {
            std::size_t                       min_size = std::numeric_limits<std::size_t>::max();
            const std::vector<std::uint32_t>* driver_dense = nullptr;

            (
                [&] {
                    if constexpr (!detail::is_optional_v<ReqColumns>) {
                        auto& pool = std::get<column_vector<ReqColumns, Storage>>(m_columns);
                        if (pool.size() < min_size) {
                            min_size     = pool.size();
                            driver_dense = &pool.dense_rows();
                        }
                    }
                }(),
                ...);

            auto filter_func = [this, required_mask](std::uint32_t row_index) -> bool {
                if (row_index >= m_rows.size() || !m_rows[row_index].alive) {
                    return false;
                }
                return (m_rows[row_index].signature & required_mask) == required_mask;
            };

            return *driver_dense | std::views::filter(std::move(filter_func)) |
                   std::views::transform(get_transform_func<basic_soa_table, ReqColumns...>(this));
        }
    }

    /// @brief Select rows that have all specified columns (const version).
    /// @tparam ReqColumns The requested column types. Use std::optional<T> for optional columns.
    /// @return A range of tuples containing (row_id, reference-wrapped const columns...).
    template <typename... ReqColumns>
    auto select() const {
        static_assert(
            sizeof...(ReqColumns) > 0, "Must provide at least one column type for projection."
        );
        static_assert(
            (registered_column_v<detail::optional_value_type_t<ReqColumns>> && ...),
            "One or more requested columns are not registered inside the soa_table schema."
        );

        signature_type required_mask;
        (
            [&] {
                if constexpr (!detail::is_optional_v<ReqColumns>)
                    required_mask.set(index_of<ReqColumns, std::tuple<Columns...>>::value);
            }(),
            ...);

        constexpr bool has_required = (!detail::is_optional_v<ReqColumns> || ...);

        if constexpr (!has_required) {
            return std::views::iota(std::size_t {0}, m_rows.size()) |
                   std::views::filter([this](std::size_t row_index) {
                       return m_rows[row_index].alive;
                   }) |
                   std::views::transform([this](std::size_t row_index) {
                       return get_transform_func<const basic_soa_table, ReqColumns...>(this)(
                           static_cast<std::uint32_t>(row_index)
                       );
                   });
        } else {
            std::size_t                       min_size = std::numeric_limits<std::size_t>::max();
            const std::vector<std::uint32_t>* driver_dense = nullptr;

            (
                [&] {
                    if constexpr (!detail::is_optional_v<ReqColumns>) {
                        auto& pool = std::get<column_vector<ReqColumns, Storage>>(m_columns);
                        if (pool.size() < min_size) {
                            min_size     = pool.size();
                            driver_dense = &pool.dense_rows();
                        }
                    }
                }(),
                ...);

            auto filter_func = [this, required_mask](std::uint32_t row_index) -> bool {
                if (row_index >= m_rows.size() || !m_rows[row_index].alive) {
                    return false;
                }
                return (m_rows[row_index].signature & required_mask) == required_mask;
            };

            return *driver_dense | std::views::filter(std::move(filter_func)) |
                   std::views::transform(get_transform_func<const basic_soa_table, ReqColumns...>(this
                   ));
        }
    }

    /// @brief Select rows that have all specified columns and view them as reference-semantic rows.
    /// @tparam ReqColumns The requested column types (optional columns are not supported here).
    /// @return A range of row_view proxies supporting structured bindings to real references.
    /// @note Built on select(); a row_view is invalidated by the same operations (erase, sort).
    template <typename... ReqColumns>
    [[nodiscard]] auto view() {
        static_assert(sizeof...(ReqColumns) > 0, "Must provide at least one column type to view.");
        static_assert(
            (!detail::is_optional_v<ReqColumns> && ...),
            "view() supports required columns only; use select() for optional columns."
        );
        return select<ReqColumns...>() | std::views::transform([](auto projected) {
                   return std::apply(
                       [](row_id id, auto&... refs) {
                           return row_view<ReqColumns...>(id, std::addressof(refs.get())...);
                       },
                       projected
                   );
               });
    }

    /// @brief Const overload of view(); yields row_view proxies over const column references.
    /// @tparam ReqColumns The requested column types (optional columns are not supported here).
    /// @return A range of row_view proxies over const references.
    template <typename... ReqColumns>
    [[nodiscard]] auto view() const {
        static_assert(sizeof...(ReqColumns) > 0, "Must provide at least one column type to view.");
        static_assert(
            (!detail::is_optional_v<ReqColumns> && ...),
            "view() supports required columns only; use select() for optional columns."
        );
        return select<ReqColumns...>() | std::views::transform([](auto projected) {
                   return std::apply(
                       [](row_id id, auto&... refs) {
                           return row_view<const ReqColumns...>(id, std::addressof(refs.get())...);
                       },
                       projected
                   );
               });
    }

    /// @brief Zero-copy view of a column's dense values as a contiguous span.
    /// @tparam T The column type.
    /// @return A span over the column's packed values, ready to hand to BLAS / a SIMD kernel /
    /// numpy.
    /// @note The span covers only rows that currently have the column, in dense storage order; pair
    /// it with row_indices<T>() to recover each value's row. Any operation that adds, removes, or
    /// reorders the column (assign, unassign, erase, sort, clear, reserve) invalidates the span.
    /// Only available for contiguous (soa_layout) storage; tiled tables use column_tiles<T>().
    template <typename T>
        requires registered_column_v<T> && Storage::is_contiguous
    [[nodiscard]] std::span<T> column() {
        auto& data = std::get<column_vector<T, Storage>>(m_columns).raw_data();
        return std::span<T>(data.data(), data.size());
    }

    /// @brief Const zero-copy view of a column's dense values.
    /// @tparam T The column type.
    /// @return A span over the column's packed const values.
    template <typename T>
        requires registered_column_v<T> && Storage::is_contiguous
    [[nodiscard]] std::span<const T> column() const {
        const auto& data = std::get<column_vector<T, Storage>>(m_columns).raw_data();
        return std::span<const T>(data.data(), data.size());
    }

    /// @brief View a column as a sequence of contiguous, over-aligned tiles.
    /// @tparam T The column type.
    /// @return One span per storage tile (a single whole-column span for soa_layout).
    /// @note The uniform tiled view across both storage policies: feed each span to a SIMD kernel.
    /// Invalidated by the same operations as column<T>().
    template <typename T>
        requires registered_column_v<T>
    [[nodiscard]] std::vector<std::span<T>> column_tiles() {
        auto&                     data = std::get<column_vector<T, Storage>>(m_columns).raw_data();
        std::vector<std::span<T>> tiles;
        if constexpr (Storage::is_contiguous) {
            tiles.emplace_back(data.data(), data.size());
        } else {
            tiles.reserve(data.tile_count());
            for (std::size_t i = 0; i < data.tile_count(); ++i) {
                tiles.push_back(data.tile(i));
            }
        }
        return tiles;
    }

    /// @brief Const overload of column_tiles().
    /// @tparam T The column type.
    /// @return One const span per storage tile.
    template <typename T>
        requires registered_column_v<T>
    [[nodiscard]] std::vector<std::span<const T>> column_tiles() const {
        const auto& data = std::get<column_vector<T, Storage>>(m_columns).raw_data();
        std::vector<std::span<const T>> tiles;
        if constexpr (Storage::is_contiguous) {
            tiles.emplace_back(data.data(), data.size());
        } else {
            tiles.reserve(data.tile_count());
            for (std::size_t i = 0; i < data.tile_count(); ++i) {
                tiles.push_back(data.tile(i));
            }
        }
        return tiles;
    }

    /// @brief Row indices parallel to column<T>(), mapping each dense value back to its row.
    /// @tparam T The column type.
    /// @return A span where element i is the internal row index of column<T>()[i].
    /// @note Combine an index with make_row_id() to obtain a stable handle. Invalidated by the same
    /// operations as column<T>().
    template <typename T>
        requires registered_column_v<T>
    [[nodiscard]] std::span<const std::uint32_t> row_indices() const {
        const auto& dense = std::get<column_vector<T, Storage>>(m_columns).dense_rows();
        return std::span<const std::uint32_t>(dense.data(), dense.size());
    }

    /// @brief Recover a stable handle from an internal row index (e.g. from row_indices<T>()).
    /// @param row_index The internal row index.
    /// @return The row_id for that slot, or an invalid handle if the index is out of range or dead.
    [[nodiscard]] row_id make_row_id(std::uint32_t row_index) const noexcept {
        if (row_index >= m_rows.size() || !m_rows[row_index].alive) {
            return row_id {npos, 0};
        }
        return row_id {row_index, m_rows[row_index].generation};
    }

    /// @brief Arrow-style validity bitmap of a column over the row-slot space.
    /// @tparam T The column type.
    /// @return A bitmap of length row_slots() where bit i is set iff slot i is alive and has T.
    /// @note Indexed by internal row index (row_id::index), so it composes with make_row_id() and
    /// the raw word view feeds branchless masked iteration or Arrow interchange. Rebuilt on each
    /// call.
    template <typename T>
        requires registered_column_v<T>
    [[nodiscard]] bitmap validity() const {
        constexpr std::size_t column_index = index_of<T, std::tuple<Columns...>>::value;
        bitmap                mask(m_rows.size());
        for (std::size_t row_index = 0; row_index < m_rows.size(); ++row_index) {
            const auto& meta = m_rows[row_index];
            if (meta.alive && meta.signature.test(column_index)) {
                mask.set(row_index);
            }
        }
        return mask;
    }

    /// @brief Fast batch insertion of multiple rows.
    /// @param count The number of rows to insert.
    /// @return A vector of newly created row_ids.
    std::vector<row_id> insert_batch(std::size_t count) {
        std::vector<row_id> ids;
        ids.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            ids.push_back(insert());
        }
        return ids;
    }

    /// @brief Fast batch assignment for a single column.
    /// @tparam T The type of the column.
    /// @tparam InputIt The type of the input iterator.
    /// @param ids A vector of row_ids to assign values to.
    /// @param first The start of the range of values to assign.
    template <typename T, typename InputIt>
        requires registered_column_v<T>
    void assign_batch(const std::vector<row_id>& ids, InputIt first) {
        auto& pool = std::get<column_vector<T, Storage>>(m_columns);
        auto  it   = first;
        for (auto id : ids) {
            if (is_valid(id)) {
                pool.emplace(id.index, *it);
                m_rows[id.index].signature.set(index_of<T, std::tuple<Columns...>>::value);
            }
            ++it;
        }
    }

    /// @brief Multi-column sorting. Physically reorders all columns based on multiple comparison
    /// criteria.
    /// @tparam SortCriteria The types of the comparison criteria.
    /// @param criteria The comparison criteria. Each should be a pair of (ColumnType, Comparator).
    /// @note Basic exception guarantee: each column is reordered in turn (see
    /// column_vector::reorder), so a throwing element move leaves the table valid but only
    /// partially reordered.
    template <typename... SortCriteria>
    void sort_by_multi(SortCriteria&&... criteria) {
        // Find smallest driver if possible, otherwise use all alive rows
        // For simplicity and correctness, we will use all alive rows indices
        std::vector<std::uint32_t> sorted_rows;
        sorted_rows.reserve(m_alive_count);
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].alive) {
                sorted_rows.push_back(static_cast<std::uint32_t>(i));
            }
        }

        auto multi_comp = [&](std::uint32_t a, std::uint32_t b) {
            bool result = false;
            bool decided =
                (([&] {
                     using T     = typename std::decay_t<SortCriteria>::first_type;
                     auto& pool  = std::get<column_vector<T, Storage>>(m_columns);
                     bool  has_a = pool.contains(a);
                     bool  has_b = pool.contains(b);
                     if (has_a && has_b) {
                         auto& val_a = pool.get(a);
                         auto& val_b = pool.get(b);
                         if (criteria.second(val_a, val_b)) {
                             result = true;
                             return true;
                         }
                         if (criteria.second(val_b, val_a)) {
                             result = false;
                             return true;
                         }
                         return false;
                     }
                     if (has_a != has_b) {
                         result = has_a;  // rows with the column come first
                         return true;
                     }
                     return false;
                 }()) ||
                 ...);
            return decided ? result : (a < b);
        };

        std::sort(sorted_rows.begin(), sorted_rows.end(), multi_comp);
        std::apply(
            [&](auto&... column_pool) { (column_pool.reorder(sorted_rows), ...); }, m_columns
        );
    }

    /// @brief Physically reorder all columns based on the sorted order of one column.
    /// @tparam T The type of the column to sort by.
    /// @tparam Compare The type of the comparator.
    /// @param comp The comparator to use for sorting values of type T.
    /// @note Basic exception guarantee (see column_vector::reorder).
    template <typename T, typename Compare>
        requires registered_column_v<T>
    void sort_by_column(Compare&& comp) {
        auto&       pool  = std::get<column_vector<T, Storage>>(m_columns);
        const auto& dense = pool.dense_rows();

        std::vector<std::uint32_t> sorted_rows = dense;
        std::sort(sorted_rows.begin(), sorted_rows.end(), [&](std::uint32_t a, std::uint32_t b) {
            return std::invoke(comp, pool.get(a), pool.get(b));
        });

        std::apply(
            [&](auto&... column_pool) { (column_pool.reorder(sorted_rows), ...); }, m_columns
        );
    }

    /// @brief Alias for sort_by_column.
    template <typename T, typename Compare>
        requires registered_column_v<T>
    void sort_by(Compare&& comp) {
        sort_by_column<T>(std::forward<Compare>(comp));
    }

    /// @brief Parallel version of sort_by_column. Reorders columns concurrently to speed up the
    /// process.
    /// @tparam T The type of the column to sort by.
    /// @tparam Compare The type of the comparator.
    /// @param comp The comparator to use for sorting values of type T.
    /// @note Threading model: the row order is computed once on the calling thread, then each
    /// column is reordered independently (columns are the unit of parallelism). The calling thread
    /// reorders the last column itself while @c column_count-1 std::async tasks handle the rest, so
    /// the caller is never idle. It falls back to a serial sort when there is a single column, a
    /// single hardware thread, or fewer than @ref parallel_sort_threshold alive rows, where
    /// task-launch overhead would dominate. A persistent thread pool is deliberately avoided:
    /// soa_table is a value-semantic container, and owning worker threads would complicate its
    /// copy/move/lifetime semantics.
    /// @note Basic exception guarantee. An exception thrown while reordering a column is rethrown
    /// by future::get after all tasks join, leaving the table valid but partially reordered.
    template <typename T, typename Compare>
        requires registered_column_v<T>
    void sort_by_column_parallel(Compare&& comp) {
        if constexpr (column_count <= 1) {
            sort_by_column<T>(std::forward<Compare>(comp));
        } else {
            if (m_alive_count < parallel_sort_threshold ||
                std::thread::hardware_concurrency() <= 1) {
                sort_by_column<T>(std::forward<Compare>(comp));
                return;
            }

            auto&       pool  = std::get<column_vector<T, Storage>>(m_columns);
            const auto& dense = pool.dense_rows();

            std::vector<std::uint32_t> sorted_rows = dense;
            std::sort(
                sorted_rows.begin(), sorted_rows.end(), [&](std::uint32_t a, std::uint32_t b) {
                    return std::invoke(comp, pool.get(a), pool.get(b));
                }
            );

            std::vector<std::future<void>> futures;
            futures.reserve(column_count - 1);

            // Dispatch every column but the last as an async task, then reorder the last column on
            // the calling thread so it overlaps the helper tasks instead of blocking idle.
            std::size_t column_index = 0;
            std::apply(
                [&](auto&... column_pool) {
                    (
                        [&] {
                            if (column_index == column_count - 1) {
                                column_pool.reorder(sorted_rows);
                            } else {
                                futures.push_back(
                                    std::async(
                                        std::launch::async,
                                        [pool_ptr = &column_pool, &sorted_rows]() {
                                            pool_ptr->reorder(sorted_rows);
                                        }
                                    )
                                );
                            }
                            ++column_index;
                        }(),
                        ...);
                },
                m_columns
            );

            for (auto& future : futures) {
                future.get();
            }
        }
    }

    /// @brief Alias for sort_by_column_parallel.
    template <typename T, typename Compare>
        requires registered_column_v<T>
    void sort_by_parallel(Compare&& comp) {
        sort_by_column_parallel<T>(std::forward<Compare>(comp));
    }
};

// ==========================================
// 4b. Table aliases by storage policy
// ==========================================

/// @brief The default table: a flat, SIMD-aligned Structure-of-Arrays layout.
/// @tparam Columns The unique column types.
template <typename... Columns>
using soa_table = basic_soa_table<soa_layout, Columns...>;

/// @brief A table whose columns use tiled (AoSoA-style) storage of the given tile size.
/// @tparam TileSize The number of elements per tile.
/// @tparam Columns The unique column types.
template <std::size_t TileSize, typename... Columns>
using aosoa_table = basic_soa_table<aosoa_layout<TileSize>, Columns...>;

/// @brief A flat table whose columns are allocated with a caller-supplied allocator template.
/// @tparam Allocator A single-argument allocator template (e.g. a pmr or arena allocator).
/// @tparam Columns The unique column types.
template <template <typename> class Allocator, typename... Columns>
using custom_soa_table = basic_soa_table<soa_layout_with<Allocator>, Columns...>;

// =====================
// 5. Row handle helper
// =====================

/// @brief A convenience wrapper around a row_id and a reference to its table.
template <typename... RegisteredColumns>
struct row_handle {
    /// @brief The ID of the row.
    row_id id {};
    /// @brief Pointer to the table containing the row.
    soa_table<RegisteredColumns...>* table = nullptr;

    /// @brief Default constructor. Creates an unbound and invalid handle.
    constexpr row_handle() = default;
    /// @brief Construct from a row_id and table reference.
    /// @param row The row_id to wrap.
    /// @param table_ref The table that contains the row.
    constexpr row_handle(row_id row, soa_table<RegisteredColumns...>& table_ref)
        : id(row), table(&table_ref) {}

   private:
    /// @brief Ensure the table is bound.
    [[nodiscard]] soa_table<RegisteredColumns...>& require_table() {
        if (table == nullptr) {
            SOATABLE_THROW(std::logic_error, "row_handle is not bound to a table.");
        }
        return *table;
    }

    /// @brief Ensure the table is bound (const).
    [[nodiscard]] const soa_table<RegisteredColumns...>& require_table() const {
        if (table == nullptr) {
            SOATABLE_THROW(std::logic_error, "row_handle is not bound to a table.");
        }
        return *table;
    }

   public:
    /// @brief Check if the handle is valid and the row is alive.
    [[nodiscard]] bool is_valid() const { return table != nullptr && table->is_valid(id); }

    /// @brief Boolean conversion for validity check.
    explicit operator bool() const { return is_valid(); }

    /// @brief Assign a value to a column for this row.
    /// @tparam T The type of the column.
    /// @tparam Args Argument types for the constructor of T.
    /// @param args Arguments to pass to the constructor of T.
    /// @return Reference to the assigned value.
    template <typename T, typename... Args>
    T& assign(Args&&... args) {
        return require_table().template assign<T>(id, std::forward<Args>(args)...);
    }

    /// @brief Remove a value from a column for this row.
    /// @tparam T The type of the column.
    template <typename T>
    void unassign() {
        require_table().template unassign<T>(id);
    }

    /// @brief Check if this row has a value for the specified column.
    /// @tparam T The type of the column.
    /// @return True if the row has the column, false otherwise.
    template <typename T>
    [[nodiscard]] bool contains() const {
        return require_table().template contains<T>(id);
    }

    /// @brief Get a reference to a column's value. Throws if not present.
    /// @tparam T The type of the column.
    /// @return Reference to the value.
    /// @throws std::out_of_range If the column is not present on the row.
    template <typename T>
    T& get() {
        return require_table().template get<T>(id);
    }

    /// @brief Get a const reference to a column's value. Throws if not present.
    /// @tparam T The type of the column.
    /// @return Const reference to the value.
    /// @throws std::out_of_range If the column is not present on the row.
    template <typename T>
    const T& get() const {
        return require_table().template get<T>(id);
    }

    /// @brief Try to get a pointer to a column's value.
    /// @tparam T The type of the column.
    /// @return Pointer to the value, or nullptr if not present.
    template <typename T>
    T* try_get() {
        return require_table().template try_get<T>(id);
    }

    /// @brief Try to get a const pointer to a column's value.
    /// @tparam T The type of the column.
    /// @return Const pointer to the value, or nullptr if not present.
    template <typename T>
    const T* try_get() const {
        return require_table().template try_get<T>(id);
    }

    /// @brief Erase this row from the table.
    /// @return True if the row was successfully erased, false if it was already invalid.
    bool erase() {
        if (!is_valid()) {
            return false;
        }
        require_table().erase(id);
        return true;
    }
};

// =====================================
// 6. Deprecated PascalCase type aliases
// =====================================

// The public API uses std-style lowercase names (soa_table, row_handle, column_vector,
// delta_value, dirty_mask). The previous PascalCase spellings remain as deprecated aliases for one
// migration window; they will be removed in a future major release.

/// @deprecated Renamed to soa_table.
template <typename... Columns>
using SoaTable [[deprecated("Renamed to soa_table.")]] = soa_table<Columns...>;

/// @deprecated Renamed to row_handle.
template <typename... RegisteredColumns>
using RowHandle [[deprecated("Renamed to row_handle.")]] = row_handle<RegisteredColumns...>;

/// @deprecated Renamed to column_vector.
template <typename T>
using ColumnVector [[deprecated("Renamed to column_vector.")]] = column_vector<T>;

/// @deprecated Renamed to delta_value.
template <typename T = double, typename DeltaType = std::int16_t, int ScaleM1000 = 10>
using DeltaValue [[deprecated("Renamed to delta_value.")]] = delta_value<T, DeltaType, ScaleM1000>;

/// @deprecated Renamed to dirty_mask.
template <typename EnumType, typename MaskType = std::uint32_t>
using DirtyMask [[deprecated("Renamed to dirty_mask.")]] = dirty_mask<EnumType, MaskType>;

}  // namespace soatable

#ifdef SOATABLE_ENABLE_SSTD_ALIAS
/// @brief Opt-in compatibility alias for the old namespace.
///
/// Define `SOATABLE_ENABLE_SSTD_ALIAS` before including this header to expose `sstd`. It is opt-in
/// because a short top-level namespace alias is prone to collisions in large downstream builds.
namespace sstd = soatable;
#endif

// Tuple-protocol opt-in for row_view so structured bindings yield real column references.
namespace std {

/// @brief Number of structured-binding elements in a row_view (the row_id plus each column).
/// @tparam Cols The viewed column types.
template <typename... Cols>
struct tuple_size<soatable::row_view<Cols...>>
    : std::integral_constant<std::size_t, sizeof...(Cols) + 1> {};

/// @brief Type of the Index-th structured-binding element of a row_view.
/// @tparam Index The binding position (0 is the row_id).
/// @tparam Cols The viewed column types.
template <std::size_t Index, typename... Cols>
struct tuple_element<Index, soatable::row_view<Cols...>> {
    /// @brief The element type at the given index.
    using type = typename soatable::row_view_element<Index, Cols...>::type;
};

}  // namespace std

#undef SOATABLE_THROW
