#pragma once

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace sstd {

// ============================================================================
// 1. Stable row identity and type helpers
// ============================================================================

struct row_id {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;

  constexpr bool operator==(const row_id&) const = default;
};

template <typename T, typename... Types>
inline constexpr bool contains_type_v = (std::same_as<T, Types> || ...);

template <typename... Types>
struct is_unique;

template <>
struct is_unique<> {
  static constexpr bool value = true;
};

template <typename T, typename... Rest>
struct is_unique<T, Rest...> {
  static constexpr bool value =
      (!std::same_as<T, Rest> && ...) && is_unique<Rest...>::value;
};

template <typename T, typename Tuple>
struct index_of;

template <typename T, typename... Types>
struct index_of<T, std::tuple<Types...>> {
  static_assert(sizeof...(Types) > 0,
                "soa_table must contain at least one registered column type.");

  static consteval std::size_t value_of() {
    constexpr bool matches[] = {std::same_as<T, Types>...};
    for (std::size_t i = 0; i < sizeof...(Types); ++i) {
      if (matches[i]) {
        return i;
      }
    }
    return sizeof...(Types);
  }

  static constexpr std::size_t value = value_of();
  static_assert(value < sizeof...(Types),
                "The requested type is not registered inside the soa_table.");
};

namespace detail {

template <typename UInt>
constexpr UInt low_bits_mask(std::size_t bits) {
  static_assert(std::is_unsigned_v<UInt>,
                "low_bits_mask requires an unsigned integer type.");
  constexpr std::size_t digits = std::numeric_limits<UInt>::digits;
  if (bits >= digits) {
    return static_cast<UInt>(~UInt{0});
  }
  if (bits == 0) {
    return UInt{0};
  }
  return static_cast<UInt>((UInt{1} << bits) - UInt{1});
}

template <typename Self, typename... ReqColumns>
using select_row_t =
    std::tuple<row_id, std::conditional_t<std::is_const_v<Self>,
                                          const ReqColumns&, ReqColumns&>...>;

}  // namespace detail

// ============================================================================
// 2. Compression and state helpers
// ============================================================================

template <typename StorageType = std::uint16_t, int MinM1000 = 0,
          int MaxM1000 = 1000000, std::size_t Bits = 16>
requires std::is_unsigned_v<StorageType>
struct quantized_float {
  static_assert(Bits > 0, "quantized_float requires at least one bit.");
  static_assert(Bits <= std::numeric_limits<StorageType>::digits,
                "Bits cannot exceed the storage type width.");
  static_assert(MaxM1000 > MinM1000,
                "quantized_float requires MaxM1000 to be greater than MinM1000.");

  StorageType quantized_value = 0;

  static constexpr double Min = MinM1000 / 1000.0;
  static constexpr double Max = MaxM1000 / 1000.0;
  static constexpr StorageType MaxVal =
      detail::low_bits_mask<StorageType>(Bits);

  constexpr quantized_float() = default;
  constexpr quantized_float(double value) { set(value); }

  constexpr void set(double value) {
    const double clamped = std::clamp(value, Min, Max);
    const double normalized = (clamped - Min) / (Max - Min);
    const auto quantized = static_cast<StorageType>(
        normalized * static_cast<double>(MaxVal) + 0.5);
    quantized_value = std::min(quantized, MaxVal);
  }

  constexpr double get() const {
    return Min + (static_cast<double>(quantized_value) /
                  static_cast<double>(MaxVal)) *
                     (Max - Min);
  }

  constexpr operator double() const { return get(); }

  constexpr quantized_float& operator=(double value) {
    set(value);
    return *this;
  }
};

template <typename ContainerType, typename ValueType, std::size_t Offset,
          std::size_t Bits>
requires std::is_unsigned_v<ContainerType>
struct packed_bits {
  static_assert(Bits > 0, "packed_bits requires at least one bit.");
  static_assert(Offset < std::numeric_limits<ContainerType>::digits,
                "Offset exceeds the container width.");
  static_assert(Offset + Bits <= std::numeric_limits<ContainerType>::digits,
                "Offset + Bits cannot exceed the container width.");

  static constexpr ContainerType ValueMask =
      static_cast<ContainerType>(detail::low_bits_mask<ContainerType>(Bits)
                                 << Offset);

  static constexpr void set(ContainerType& container, ValueType value) {
    container = static_cast<ContainerType>((container & ~ValueMask) |
                                           ((static_cast<ContainerType>(value)
                                             << Offset) &
                                            ValueMask));
  }

  static constexpr ValueType get(ContainerType container) {
    return static_cast<ValueType>((container & ValueMask) >> Offset);
  }
};

template <typename T = double, typename DeltaType = std::int16_t,
          int ScaleM1000 = 10>
class delta_value {
  static_assert(ScaleM1000 > 0, "delta_value requires a positive scale.");

 private:
  T m_value{};
  static constexpr double Scale = ScaleM1000 / 1000.0;

 public:
  constexpr delta_value() = default;
  constexpr explicit delta_value(T initial) : m_value(initial) {}

  constexpr void set(T value) { m_value = value; }

  constexpr void apply_delta(DeltaType delta) {
    m_value += static_cast<T>(delta) * Scale;
  }

  [[nodiscard]] DeltaType get_delta(T new_value) const {
    const T diff = new_value - m_value;
    const double scaled = static_cast<double>(diff) / Scale;
    const double rounded = scaled >= 0.0 ? std::floor(scaled + 0.5)
                                          : std::ceil(scaled - 0.5);
    const auto min_value = static_cast<double>(std::numeric_limits<DeltaType>::min());
    const auto max_value = static_cast<double>(std::numeric_limits<DeltaType>::max());
    const double clamped = std::clamp(rounded, min_value, max_value);
    return static_cast<DeltaType>(clamped);
  }

  [[nodiscard]] constexpr T get() const { return m_value; }
  constexpr operator T() const { return get(); }
};

template <typename EnumType, typename MaskType = std::uint32_t>
requires std::is_enum_v<EnumType>
class dirty_mask {
  static_assert(std::is_unsigned_v<MaskType>,
                "dirty_mask requires an unsigned mask type.");

 private:
  MaskType m_mask = 0;

 public:
  constexpr void mark_dirty(EnumType flag) {
    m_mask |= static_cast<MaskType>(flag);
  }

  constexpr void clear_dirty(EnumType flag) {
    m_mask &= static_cast<MaskType>(~static_cast<MaskType>(flag));
  }

  [[nodiscard]] constexpr bool is_dirty(EnumType flag) const {
    return (m_mask & static_cast<MaskType>(flag)) != 0;
  }

  [[nodiscard]] constexpr bool is_any_dirty() const { return m_mask != 0; }
  constexpr void reset() { m_mask = 0; }
  [[nodiscard]] constexpr MaskType get_mask() const { return m_mask; }
};

// ============================================================================
// 3. Sparse column storage
// ============================================================================

template <typename T>
class column_vector {
 private:
  static constexpr std::int32_t tombstone = -1;
  std::vector<std::int32_t> m_sparse;
  std::vector<std::uint32_t> m_dense;
  std::vector<T> m_data;

 public:
  column_vector() = default;

  void reserve(std::size_t capacity) {
    m_sparse.reserve(capacity);
    m_dense.reserve(capacity);
    m_data.reserve(capacity);
  }

  void clear() {
    m_sparse.clear();
    m_dense.clear();
    m_data.clear();
  }

  void shrink_to_fit() {
    m_sparse.shrink_to_fit();
    m_dense.shrink_to_fit();
    m_data.shrink_to_fit();
  }

  [[nodiscard]] bool contains(std::uint32_t row_index) const {
    return row_index < m_sparse.size() && m_sparse[row_index] != tombstone;
  }

  [[nodiscard]] T* try_get(std::uint32_t row_index) {
    if (!contains(row_index)) {
      return nullptr;
    }
    return &m_data[static_cast<std::size_t>(m_sparse[row_index])];
  }

  [[nodiscard]] const T* try_get(std::uint32_t row_index) const {
    if (!contains(row_index)) {
      return nullptr;
    }
    return &m_data[static_cast<std::size_t>(m_sparse[row_index])];
  }

  T& get(std::uint32_t row_index) {
    assert(contains(row_index) && "Out of bounds sparse column reference.");
    return m_data[static_cast<std::size_t>(m_sparse[row_index])];
  }

  const T& get(std::uint32_t row_index) const {
    assert(contains(row_index) && "Out of bounds sparse column reference.");
    return m_data[static_cast<std::size_t>(m_sparse[row_index])];
  }

  template <typename... Args>
  T& emplace(std::uint32_t row_index, Args&&... args) {
    if (row_index >= m_sparse.size()) {
      m_sparse.resize(static_cast<std::size_t>(row_index) + 1, tombstone);
    }

    if (m_sparse[row_index] != tombstone) {
      const std::size_t dense_index = static_cast<std::size_t>(m_sparse[row_index]);
      m_data[dense_index] = T(std::forward<Args>(args)...);
      return m_data[dense_index];
    }

    m_sparse[row_index] = static_cast<std::int32_t>(m_dense.size());
    m_dense.push_back(row_index);
    m_data.emplace_back(std::forward<Args>(args)...);
    return m_data.back();
  }

  void remove(std::uint32_t row_index) {
    if (!contains(row_index)) {
      return;
    }

    const std::size_t dense_index = static_cast<std::size_t>(m_sparse[row_index]);
    const std::uint32_t last_row_index = m_dense.back();

    std::swap(m_data[dense_index], m_data.back());
    std::swap(m_dense[dense_index], m_dense.back());

    m_sparse[last_row_index] = static_cast<std::int32_t>(dense_index);
    m_sparse[row_index] = tombstone;

    m_data.pop_back();
    m_dense.pop_back();
  }

  void reorder(const std::vector<std::uint32_t>& row_order) {
    std::vector<T> new_data;
    std::vector<std::uint32_t> new_dense;
    new_data.reserve(m_data.size());
    new_dense.reserve(m_dense.size());

    for (const std::uint32_t row_index : row_order) {
      if (contains(row_index)) {
        new_data.push_back(std::move(get(row_index)));
        new_dense.push_back(row_index);
      }
    }

    std::fill(m_sparse.begin(), m_sparse.end(), tombstone);
    for (std::size_t i = 0; i < new_dense.size(); ++i) {
      m_sparse[new_dense[i]] = static_cast<std::int32_t>(i);
    }

    m_data = std::move(new_data);
    m_dense = std::move(new_dense);
  }

  [[nodiscard]] std::size_t size() const { return m_dense.size(); }
  [[nodiscard]] bool empty() const { return m_dense.empty(); }
  [[nodiscard]] const std::vector<std::uint32_t>& dense_rows() const {
    return m_dense;
  }
  std::vector<T>& raw_data() { return m_data; }
  const std::vector<T>& raw_data() const { return m_data; }
};

// ============================================================================
// 4. Relational column table
// ============================================================================

template <typename... Columns>
class soa_table {
 public:
  static_assert(sizeof...(Columns) > 0,
                "soa_table must contain at least one column type.");
  static_assert(is_unique<Columns...>::value,
                "All registered columns in an soa_table must have unique types.");

  static constexpr std::size_t column_count = sizeof...(Columns);
  using signature_type = std::bitset<column_count>;

  struct RowMeta {
    std::uint32_t generation = 0;
    signature_type signature{};
    bool alive = false;
  };

  template <typename T>
  static constexpr bool registered_column_v = contains_type_v<T, Columns...>;

 private:
  static constexpr std::uint32_t npos = std::numeric_limits<std::uint32_t>::max();

  std::vector<RowMeta> m_rows;
  std::vector<std::uint32_t> m_free_links;
  std::uint32_t m_free_head = npos;
  std::size_t m_alive_count = 0;
  std::tuple<column_vector<Columns>...> m_pools;

  [[nodiscard]] static constexpr bool can_address_rows(std::size_t count) {
    return count <= std::numeric_limits<std::uint32_t>::max();
  }

  template <typename Self>
  static auto rows_impl(Self* self) {
    return std::views::iota(std::size_t{0}, self->m_rows.size()) |
           std::views::filter([self](std::size_t row_index) {
             return self->m_rows[row_index].alive;
           }) |
           std::views::transform([self](std::size_t row_index) {
             return row_id{static_cast<std::uint32_t>(row_index),
                           self->m_rows[row_index].generation};
           });
  }

  template <typename Self, typename... ReqColumns>
  static auto select_impl(Self* self) {
    static_assert(sizeof...(ReqColumns) > 0,
                  "Must provide at least one column type for projection.");
    static_assert((registered_column_v<ReqColumns> && ...),
                  "One or more requested columns are not registered inside "
                  "the soa_table schema.");

    signature_type mask;
    ((mask.set(index_of<ReqColumns, std::tuple<Columns...>>::value)), ...);

    std::size_t min_size = std::numeric_limits<std::size_t>::max();
    const std::vector<std::uint32_t>* driver_dense = nullptr;

    auto choose_driver = [&](auto* pool) {
      if (pool->size() < min_size) {
        min_size = pool->size();
        driver_dense = &pool->dense_rows();
      }
    };

    (choose_driver(&std::get<column_vector<ReqColumns>>(self->m_pools)), ...);
    assert(driver_dense != nullptr);

    auto filter_func = [self, mask](std::uint32_t row_index) -> bool {
      if (row_index >= self->m_rows.size() || !self->m_rows[row_index].alive) {
        return false;
      }
      return (self->m_rows[row_index].signature & mask) == mask;
    };

    auto transform_func = [self](std::uint32_t row_index) {
      const row_id id{row_index, self->m_rows[row_index].generation};
      using tuple_type = detail::select_row_t<Self, ReqColumns...>;
      return tuple_type{id,
                        std::get<column_vector<ReqColumns>>(self->m_pools)
                            .get(row_index)...};
    };

    return *driver_dense | std::views::filter(std::move(filter_func)) |
           std::views::transform(std::move(transform_func));
  }

 public:
  soa_table() = default;

  [[nodiscard]] std::size_t size() const noexcept { return m_alive_count; }
  [[nodiscard]] std::size_t row_slots() const noexcept { return m_rows.size(); }
  [[nodiscard]] bool empty() const noexcept { return m_alive_count == 0; }

  void reserve(std::size_t capacity) {
    m_rows.reserve(capacity);
    m_free_links.reserve(capacity);
    std::apply(
        [capacity](auto&... pool) {
          (pool.reserve(capacity), ...);
        },
        m_pools);
  }

  void shrink_to_fit() {
    m_rows.shrink_to_fit();
    m_free_links.shrink_to_fit();
    std::apply([](auto&... pool) { (pool.shrink_to_fit(), ...); }, m_pools);
  }

  void clear() {
    m_rows.clear();
    m_free_links.clear();
    m_free_head = npos;
    m_alive_count = 0;
    std::apply([](auto&... pool) { (pool.clear(), ...); }, m_pools);
  }

  [[nodiscard]] row_id insert() {
    std::uint32_t index = 0;
    if (m_free_head != npos) {
      index = m_free_head;
      m_free_head = m_free_links[index];
      m_rows[index].alive = true;
      m_rows[index].signature.reset();
    } else {
      if (!can_address_rows(m_rows.size() + 1)) {
        throw std::overflow_error("soa_table exhausted the row_id index space.");
      }
      index = static_cast<std::uint32_t>(m_rows.size());
      m_rows.push_back(RowMeta{});
      m_rows.back().alive = true;
      m_free_links.push_back(npos);
    }

    ++m_alive_count;
    return row_id{index, m_rows[index].generation};
  }

  [[nodiscard]] bool is_valid(row_id id) const noexcept {
    if (id.index >= m_rows.size()) {
      return false;
    }
    const auto& meta = m_rows[id.index];
    return meta.alive && meta.generation == id.generation;
  }

  void erase(row_id id) {
    if (!is_valid(id)) {
      return;
    }

    const std::uint32_t index = id.index;

    auto clear_row_from_pool = [index]<typename T>(column_vector<T>& pool) {
      if (pool.contains(index)) {
        pool.remove(index);
      }
    };
    std::apply([&](auto&... pool) { (clear_row_from_pool(pool), ...); }, m_pools);

    auto& meta = m_rows[index];
    meta.alive = false;
    ++meta.generation;
    meta.signature.reset();

    m_free_links[index] = m_free_head;
    m_free_head = index;

    --m_alive_count;
  }

  template <typename T, typename... Args>
  requires registered_column_v<T>
  T& assign(row_id id, Args&&... args) {
    if (!is_valid(id)) {
      throw std::out_of_range("assign() called with an invalid row_id.");
    }

    auto& pool = std::get<column_vector<T>>(m_pools);
    T& value = pool.emplace(id.index, std::forward<Args>(args)...);
    m_rows[id.index].signature.set(index_of<T, std::tuple<Columns...>>::value);
    return value;
  }

  template <typename T>
  requires registered_column_v<T>
  void unassign(row_id id) {
    if (!is_valid(id)) {
      return;
    }

    auto& pool = std::get<column_vector<T>>(m_pools);
    pool.remove(id.index);
    m_rows[id.index].signature.reset(index_of<T, std::tuple<Columns...>>::value);
  }

  template <typename T>
  requires registered_column_v<T>
  [[nodiscard]] bool contains(row_id id) const {
    if (!is_valid(id)) {
      return false;
    }
    return m_rows[id.index].signature.test(index_of<T, std::tuple<Columns...>>::value);
  }

  template <typename T>
  requires registered_column_v<T>
  T* try_get(row_id id) {
    if (!contains<T>(id)) {
      return nullptr;
    }
    return std::get<column_vector<T>>(m_pools).try_get(id.index);
  }

  template <typename T>
  requires registered_column_v<T>
  const T* try_get(row_id id) const {
    if (!contains<T>(id)) {
      return nullptr;
    }
    return std::get<column_vector<T>>(m_pools).try_get(id.index);
  }

  template <typename T>
  requires registered_column_v<T>
  T& get(row_id id) {
    auto* value = try_get<T>(id);
    if (value == nullptr) {
      throw std::out_of_range("Requested column is not available on this row.");
    }
    return *value;
  }

  template <typename T>
  requires registered_column_v<T>
  const T& get(row_id id) const {
    auto* value = try_get<T>(id);
    if (value == nullptr) {
      throw std::out_of_range("Requested column is not available on this row.");
    }
    return *value;
  }

  [[nodiscard]] auto rows() { return rows_impl(this); }
  [[nodiscard]] auto rows() const { return rows_impl(this); }

  template <typename Func>
  void for_each_row(Func&& func) {
    for (row_id id : rows()) {
      std::invoke(func, id);
    }
  }

  template <typename Func>
  void for_each_row(Func&& func) const {
    for (row_id id : rows()) {
      std::invoke(func, id);
    }
  }

  template <typename... ReqColumns>
  auto select() {
    return select_impl<soa_table, ReqColumns...>(this);
  }

  template <typename... ReqColumns>
  auto select() const {
    return select_impl<const soa_table, ReqColumns...>(this);
  }

  template <typename T, typename Compare>
  requires registered_column_v<T>
  void sort_by_column(Compare&& comp) {
    auto& pool = std::get<column_vector<T>>(m_pools);
    const auto& dense = pool.dense_rows();

    std::vector<std::uint32_t> sorted_rows = dense;
    std::sort(sorted_rows.begin(), sorted_rows.end(),
              [&](std::uint32_t a, std::uint32_t b) {
                return std::invoke(comp, pool.get(a), pool.get(b));
              });

    std::apply(
        [&](auto&... column_pool) { (column_pool.reorder(sorted_rows), ...); },
        m_pools);
  }

  template <typename T, typename Compare>
  requires registered_column_v<T>
  void sort_by(Compare&& comp) {
    sort_by_column<T>(std::forward<Compare>(comp));
  }

  template <typename T, typename Compare>
  requires registered_column_v<T>
  void sort_by_column_parallel(Compare&& comp) {
    if constexpr (column_count <= 1) {
      sort_by_column<T>(std::forward<Compare>(comp));
      return;
    }

    auto& pool = std::get<column_vector<T>>(m_pools);
    const auto& dense = pool.dense_rows();

    std::vector<std::uint32_t> sorted_rows = dense;
    std::sort(sorted_rows.begin(), sorted_rows.end(),
              [&](std::uint32_t a, std::uint32_t b) {
                return std::invoke(comp, pool.get(a), pool.get(b));
              });

    std::vector<std::future<void>> futures;
    futures.reserve(column_count);

    std::apply(
        [&](auto&... column_pool) {
          (futures.push_back(std::async(
               std::launch::async,
               [pool_ptr = &column_pool, &sorted_rows]() {
                 pool_ptr->reorder(sorted_rows);
               })), ...);
        },
        m_pools);

    for (auto& future : futures) {
      future.get();
    }
  }

  template <typename T, typename Compare>
  requires registered_column_v<T>
  void sort_by_parallel(Compare&& comp) {
    sort_by_column_parallel<T>(std::forward<Compare>(comp));
  }
};

// ============================================================================
// 5. Row handle helper
// ============================================================================

template <typename... RegisteredColumns>
struct row_handle {
  row_id id{};
  soa_table<RegisteredColumns...>* table = nullptr;

  constexpr row_handle() = default;
  constexpr row_handle(row_id row, soa_table<RegisteredColumns...>& table_ref)
      : id(row), table(&table_ref) {}

 private:
  [[nodiscard]] soa_table<RegisteredColumns...>& require_table() {
    if (table == nullptr) {
      throw std::logic_error("row_handle is not bound to a table.");
    }
    return *table;
  }

  [[nodiscard]] const soa_table<RegisteredColumns...>& require_table() const {
    if (table == nullptr) {
      throw std::logic_error("row_handle is not bound to a table.");
    }
    return *table;
  }

 public:
  [[nodiscard]] bool is_valid() const {
    return table != nullptr && table->is_valid(id);
  }

  explicit operator bool() const { return is_valid(); }

  template <typename T, typename... Args>
  T& assign(Args&&... args) {
    return require_table().template assign<T>(id, std::forward<Args>(args)...);
  }

  template <typename T>
  void unassign() {
    require_table().template unassign<T>(id);
  }

  template <typename T>
  [[nodiscard]] bool contains() const {
    return require_table().template contains<T>(id);
  }

  template <typename T>
  T& get() {
    return require_table().template get<T>(id);
  }

  template <typename T>
  const T& get() const {
    return require_table().template get<T>(id);
  }

  template <typename T>
  T* try_get() {
    return require_table().template try_get<T>(id);
  }

  template <typename T>
  const T* try_get() const {
    return require_table().template try_get<T>(id);
  }

  bool erase() {
    if (!is_valid()) {
      return false;
    }
    require_table().erase(id);
    return true;
  }
};

}  // namespace sstd

namespace soatable = sstd;
