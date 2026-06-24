/// @file const_correctness_test.cpp
/// @brief Const-correctness contract (Phase 2.2): compile-time assertions that const accessors
/// yield const references end to end, plus a runtime read through a const table.
/// @author Bertin Balouki SIMYELI

#include <gtest/gtest.h>

#include <expected>
#include <functional>
#include <type_traits>

#include "soatable/soatable.hpp"
#include "test_types.hpp"

using soatable_test::Age;
using soatable_test::DemoTable;

namespace {

using row_id = soatable::row_id;

// get<T> respects the table's const-qualification.
static_assert(std::is_same_v<
              decltype(std::declval<DemoTable&>().get<Age>(std::declval<row_id>())), Age&>);
static_assert(std::is_same_v<
              decltype(std::declval<const DemoTable&>().get<Age>(std::declval<row_id>())),
              const Age&>);

// try_get<T> likewise.
static_assert(std::is_same_v<
              decltype(std::declval<DemoTable&>().try_get<Age>(std::declval<row_id>())), Age*>);
static_assert(std::is_same_v<
              decltype(std::declval<const DemoTable&>().try_get<Age>(std::declval<row_id>())),
              const Age*>);

// get_expected<T> wraps a const reference when called on a const table.
static_assert(std::is_same_v<
              decltype(std::declval<const DemoTable&>().get_expected<Age>(std::declval<row_id>())),
              std::expected<std::reference_wrapper<const Age>, soatable::access_error>>);

}  // namespace

TEST(ConstCorrectnessTest, ConstTableIsFullyReadable) {
    DemoTable  table;
    const auto id = table.insert();
    table.assign<Age>(id, 11);

    const DemoTable& const_table = table;
    EXPECT_EQ(const_table.get<Age>(id).value, 11);
    EXPECT_NE(const_table.try_get<Age>(id), nullptr);
    EXPECT_TRUE(const_table.contains<Age>(id));

    int via_select = 0;
    for (auto [row, age] : const_table.select<Age>()) {
        static_cast<void>(row);
        via_select += age.get().value;
    }
    EXPECT_EQ(via_select, 11);
}
