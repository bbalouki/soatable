// Verifies header hygiene: the header is safe to include in multiple translation units (no
// non-inline definitions causing ODR/link errors) and the opt-in sstd alias works when requested.

#define SOATABLE_ENABLE_SSTD_ALIAS
#include "soatable/soatable.hpp"
// Include a second time to confirm the include guard makes re-inclusion a no-op.
#include "soatable/soatable.hpp"

#include <gtest/gtest.h>

#include <cstddef>

// Defined in include_hygiene_other_tu.cpp; both TUs include the header.
std::size_t row_count_from_other_tu();

namespace {
struct Tag {
    int value = 0;
};
}  // namespace

TEST(IncludeHygieneTest, SstdAliasResolvesToSoatable) {
    sstd::SoaTable<Tag> table;
    const auto          id = table.insert();
    table.assign<Tag>(id, 1);
    EXPECT_EQ(table.get<Tag>(id).value, 1);
}

TEST(IncludeHygieneTest, HeaderLinksAcrossTranslationUnits) {
    EXPECT_EQ(row_count_from_other_tu(), 2U);
}
