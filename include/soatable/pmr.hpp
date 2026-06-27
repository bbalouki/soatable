/// @file pmr.hpp
/// @brief Opt-in std::pmr (polymorphic memory resource) support for SoaTable.
/// @author Bertin Balouki SIMYELI
///
/// Included separately so the core header does not pull in <memory_resource>. A pmr_soa_table
/// allocates its column storage from the active std::pmr default memory resource, letting callers
/// route allocations to an arena, monotonic buffer, or pool resource for deterministic,
/// fragmentation-free behaviour.
#pragma once

#include <memory_resource>

#include "soatable/soatable.hpp"

namespace soatable {

/// @brief A single-argument allocator template wrapping std::pmr::polymorphic_allocator.
/// @tparam T The element type.
template <typename T>
using pmr_allocator = std::pmr::polymorphic_allocator<T>;

/// @brief A flat table whose columns allocate from the active std::pmr default memory resource.
/// @tparam Columns The unique column types.
///
/// Set the resource with std::pmr::set_default_resource() (or wrap construction accordingly) before
/// populating the table to route its allocations through an arena / monotonic buffer / pool.
template <typename... Columns>
using pmr_soa_table = custom_soa_table<pmr_allocator, Columns...>;

}  // namespace soatable
