/// @file dynamic.hpp
/// @brief Opt-in runtime schema evolution: a type-erased table whose columns are added and removed
/// at runtime by name, with per-column string metadata.
/// @author Bertin Balouki SIMYELI
///
/// It complements the statically-typed soa_table for schemas that are not known at compile time.
/// Columns are type-checked without RTTI (each type has a unique key address), so set<T>/get<T>
/// reject a type mismatch. Cells are stored sparsely per column, mirroring soa_table's "pay only
/// for columns a row has" model.
#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace soatable {

namespace detail {
/// @brief A process-unique key for a type, used for type-erased checks without RTTI.
/// @tparam T The type.
/// @return A stable address unique to T.
template <typename T>
inline const void* dynamic_type_key() noexcept {
    static const char key = 0;
    return &key;
}
}  // namespace detail

/// @brief A table whose columns can be added and removed at runtime, with per-column metadata.
class dynamic_table {
   public:
    /// @brief Row and column index type.
    using size_type = std::size_t;

    /// @brief Add a typed column.
    /// @tparam T The column value type.
    /// @param name The column name (must be unique).
    /// @return True if added, false if a column with that name already exists.
    template <typename T>
    bool add_column(std::string name) {
        if (m_columns.contains(name)) {
            return false;
        }
        auto column      = std::make_unique<typed_column<T>>();
        column->type_key = detail::dynamic_type_key<T>();
        m_columns.emplace(std::move(name), std::move(column));
        return true;
    }

    /// @brief Remove a column and all its cells.
    /// @param name The column name.
    /// @return True if a column was removed.
    bool remove_column(const std::string& name) { return m_columns.erase(name) != 0; }

    /// @brief Whether a column with the given name exists.
    [[nodiscard]] bool has_column(const std::string& name) const {
        return m_columns.contains(name);
    }

    /// @brief The number of columns.
    [[nodiscard]] size_type column_count() const noexcept { return m_columns.size(); }

    /// @brief The names of all columns (unordered).
    [[nodiscard]] std::vector<std::string> column_names() const {
        std::vector<std::string> names;
        names.reserve(m_columns.size());
        for (const auto& [name, column] : m_columns) {
            static_cast<void>(column);
            names.push_back(name);
        }
        return names;
    }

    /// @brief Insert a new alive row.
    /// @return The new row index.
    size_type insert_row() {
        const size_type row = m_alive.size();
        m_alive.push_back(true);
        ++m_alive_count;
        return row;
    }

    /// @brief Erase a row, dropping its cells from every column.
    /// @param row The row index.
    void erase_row(size_type row) {
        if (row >= m_alive.size() || !m_alive[row]) {
            return;
        }
        for (auto& [name, column] : m_columns) {
            static_cast<void>(name);
            column->erase_row(row);
        }
        m_alive[row] = false;
        --m_alive_count;
    }

    /// @brief Whether a row index is alive.
    [[nodiscard]] bool is_alive(size_type row) const {
        return row < m_alive.size() && m_alive[row];
    }

    /// @brief The number of alive rows.
    [[nodiscard]] size_type size() const noexcept { return m_alive_count; }

    /// @brief The number of row slots ever allocated.
    [[nodiscard]] size_type row_slots() const noexcept { return m_alive.size(); }

    /// @brief Set a cell value.
    /// @tparam T The column value type (must match the column's type).
    /// @param row The row index.
    /// @param name The column name.
    /// @param value The value to store.
    /// @throws std::out_of_range If the column or row is invalid.
    /// @throws std::invalid_argument If T does not match the column's type.
    template <typename T>
    void set(size_type row, const std::string& name, T value) {
        const auto it = m_columns.find(name);
        if (it == m_columns.end()) {
            throw std::out_of_range("dynamic_table: unknown column '" + name + "'");
        }
        if (it->second->type_key != detail::dynamic_type_key<T>()) {
            throw std::invalid_argument("dynamic_table: type mismatch for column '" + name + "'");
        }
        if (row >= m_alive.size() || !m_alive[row]) {
            throw std::out_of_range("dynamic_table: invalid row");
        }
        static_cast<typed_column<T>*>(it->second.get())->cells[row] = std::move(value);
    }

    /// @brief Get a pointer to a cell value, or nullptr if absent or the type mismatches.
    /// @tparam T The column value type.
    /// @param row The row index.
    /// @param name The column name.
    /// @return Pointer to the value, or nullptr.
    template <typename T>
    [[nodiscard]] T* get(size_type row, const std::string& name) {
        auto* column = typed<T>(name);
        if (column == nullptr) {
            return nullptr;
        }
        const auto cell = column->cells.find(row);
        return cell == column->cells.end() ? nullptr : &cell->second;
    }

    /// @brief Const overload of get().
    template <typename T>
    [[nodiscard]] const T* get(size_type row, const std::string& name) const {
        const auto* column = typed<T>(name);
        if (column == nullptr) {
            return nullptr;
        }
        const auto cell = column->cells.find(row);
        return cell == column->cells.end() ? nullptr : &cell->second;
    }

    /// @brief Attach a string metadata key/value to a column (e.g. a unit label).
    /// @param column The column name.
    /// @param key The metadata key.
    /// @param value The metadata value.
    /// @throws std::out_of_range If the column does not exist.
    void set_metadata(const std::string& column, std::string key, std::string value) {
        const auto it = m_columns.find(column);
        if (it == m_columns.end()) {
            throw std::out_of_range("dynamic_table: unknown column '" + column + "'");
        }
        it->second->metadata[std::move(key)] = std::move(value);
    }

    /// @brief Read a column's metadata value, or nullptr if the column or key is absent.
    [[nodiscard]] const std::string* get_metadata(
        const std::string& column, const std::string& key
    ) const {
        const auto it = m_columns.find(column);
        if (it == m_columns.end()) {
            return nullptr;
        }
        const auto entry = it->second->metadata.find(key);
        return entry == it->second->metadata.end() ? nullptr : &entry->second;
    }

   private:
    struct column_base {
        const void*                                  type_key = nullptr;
        std::unordered_map<std::string, std::string> metadata;
        virtual ~column_base()                = default;
        virtual void erase_row(size_type row) = 0;
    };

    template <typename T>
    struct typed_column : column_base {
        std::unordered_map<size_type, T> cells;
        void                             erase_row(size_type row) override { cells.erase(row); }
    };

    template <typename T>
    typed_column<T>* typed(const std::string& name) {
        const auto it = m_columns.find(name);
        if (it == m_columns.end() || it->second->type_key != detail::dynamic_type_key<T>()) {
            return nullptr;
        }
        return static_cast<typed_column<T>*>(it->second.get());
    }

    template <typename T>
    const typed_column<T>* typed(const std::string& name) const {
        const auto it = m_columns.find(name);
        if (it == m_columns.end() || it->second->type_key != detail::dynamic_type_key<T>()) {
            return nullptr;
        }
        return static_cast<const typed_column<T>*>(it->second.get());
    }

    std::unordered_map<std::string, std::unique_ptr<column_base>> m_columns;
    std::vector<bool>                                             m_alive;
    size_type                                                     m_alive_count = 0;
};

}  // namespace soatable
