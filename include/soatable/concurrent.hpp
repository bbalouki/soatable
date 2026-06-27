/// @file concurrent.hpp
/// @brief Opt-in thread-safe access to a table.
/// @author Bertin Balouki SIMYELI
///
/// synchronized_table wraps any soa_table behind a std::shared_mutex, giving many concurrent
/// readers and exclusive writers via scoped callbacks: the lock is held for the duration of the
/// callback, so access is always synchronized. This matches the "many strategy threads read while
/// one thread ingests" use case.
///
/// It is reader-writer synchronization, not lock-free MVCC or sharding; a snapshot/copy-on-write
/// read view and per-shard locking are natural future enhancements. References handed to a callback
/// must not escape it, since they are only valid while the lock is held.
#pragma once

#include <mutex>
#include <shared_mutex>
#include <utility>

namespace soatable {

/// @brief A thread-safe wrapper giving shared (reader) and exclusive (writer) access to a table.
/// @tparam Table The wrapped table type (e.g. soa_table<...>).
template <typename Table>
class synchronized_table {
   public:
    /// @brief The wrapped table type.
    using table_type = Table;

    /// @brief Default-construct an empty wrapped table.
    synchronized_table() = default;

    /// @brief Wrap an existing table by moving it in.
    /// @param table The table to take ownership of.
    explicit synchronized_table(Table table) : m_table(std::move(table)) {}

    /// @brief Run a callback with shared (read) access; many readers may proceed concurrently.
    /// @tparam Func A callable taking const Table&.
    /// @param func The callback; its return value is forwarded out.
    /// @return Whatever @p func returns.
    template <typename Func>
    decltype(auto) read(Func&& func) const {
        const std::shared_lock<std::shared_mutex> lock(m_mutex);
        return std::forward<Func>(func)(m_table);
    }

    /// @brief Run a callback with exclusive (write) access; no other reader or writer proceeds.
    /// @tparam Func A callable taking Table&.
    /// @param func The callback; its return value is forwarded out.
    /// @return Whatever @p func returns.
    template <typename Func>
    decltype(auto) write(Func&& func) {
        const std::unique_lock<std::shared_mutex> lock(m_mutex);
        return std::forward<Func>(func)(m_table);
    }

   private:
    Table                     m_table;
    mutable std::shared_mutex m_mutex;
};

}  // namespace soatable
