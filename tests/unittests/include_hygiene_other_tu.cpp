// Second translation unit that also includes the header. If the header carried any non-inline
// definition, linking this TU together with include_hygiene_test.cpp would fail.

#include <cstddef>

#include "soatable/soatable.hpp"

namespace {
struct Tag {
    int value = 0;
};
}  // namespace

std::size_t row_count_from_other_tu() {
    soatable::SoaTable<Tag> table;
    static_cast<void>(table.insert());
    static_cast<void>(table.insert());
    return table.size();
}
