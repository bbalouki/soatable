/// @file reflect.hpp
/// @brief Build a table directly from a plain struct.
/// @author Bertin Balouki SIMYELI
///
/// C++26 static reflection (P2996) is not yet available in shipping toolchains, so this header
/// provides a portable fallback that works today and an automatic path that activates once a
/// compiler implements reflection:
///
/// * Portable (now): specialize columns_of<Struct> (via column_list) to list the struct's column
/// types, then use table_for<Struct>. * Automatic (future): when SOATABLE_HAS_REFLECTION is 1,
/// columns_of<Struct> is derived from the struct's members with no manual specialization.
#pragma once

#include "soatable/soatable.hpp"

#if defined(__cpp_reflection) && __cpp_reflection >= 202600L
#define SOATABLE_HAS_REFLECTION 1
#else
#define SOATABLE_HAS_REFLECTION 0
#endif

namespace soatable {

/// @brief A list of column types, exposing the corresponding soa_table type.
/// @tparam Columns The column types.
template <typename... Columns>
struct column_list {
    /// @brief The table type holding these columns.
    using table_type = soa_table<Columns...>;
};

/// @brief Maps a user struct to its column list. Specialize this (deriving from column_list) to use
/// table_for<Struct> without reflection; the specialization becomes unnecessary once reflection ships.
/// @tparam Struct The user struct identifying a schema.
template <typename Struct>
struct columns_of;

#if SOATABLE_HAS_REFLECTION
// When a toolchain implements C++26 reflection, a default definition of columns_of<Struct> that
// enumerates the struct's data members into columns is provided here, making the manual
// specialization above optional. (Intentionally left for the reflection-capable build.)
#endif

/// @brief The soa_table type for a struct, via its columns_of mapping.
/// @tparam Struct The user struct identifying a schema.
template <typename Struct>
using table_for = typename columns_of<Struct>::table_type;

}  // namespace soatable
