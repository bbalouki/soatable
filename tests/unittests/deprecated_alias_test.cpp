/// @file deprecated_alias_test.cpp
/// @brief Verifies the deprecated PascalCase aliases (Phase 2.4 migration window) still resolve to
/// the new snake_case types.
/// @author Bertin Balouki SIMYELI
///
/// The deprecation diagnostic is intentionally silenced for this TU, since exercising the
/// deprecated path is the whole point of the test.

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)  // deprecated declaration
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <gtest/gtest.h>

#include <type_traits>

#include "soatable/soatable.hpp"

namespace {
struct Age {
    int value = 0;
};
enum class Flags : std::uint32_t {
    dirty = 1U << 0U,
};
}  // namespace

TEST(DeprecatedAliasTest, PascalCaseNamesResolveToSnakeCase) {
    static_assert(std::is_same_v<soatable::SoaTable<Age>, soatable::soa_table<Age>>);
    static_assert(std::is_same_v<soatable::RowHandle<Age>, soatable::row_handle<Age>>);
    static_assert(std::is_same_v<soatable::ColumnVector<Age>, soatable::column_vector<Age>>);
    static_assert(std::is_same_v<soatable::DeltaValue<double>, soatable::delta_value<double>>);
    static_assert(std::is_same_v<soatable::DirtyMask<Flags>, soatable::dirty_mask<Flags>>);

    soatable::SoaTable<Age> table;
    const auto              id = table.insert();
    table.assign<Age>(id, 3);
    EXPECT_EQ(table.get<Age>(id).value, 3);
}

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
