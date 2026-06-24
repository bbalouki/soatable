// Built with SOATABLE_NO_EXCEPTIONS (and -fno-exceptions on GCC/Clang) to prove the freestanding
// no-exceptions path compiles and that the non-throwing accessors behave correctly. Returns 0 on
// success; a non-zero exit code identifies the failing check.

#include "soatable/soatable.hpp"

namespace {
struct Price {
    double value = 0.0;
};
struct Qty {
    int value = 0;
};
}  // namespace

int main() {
    soatable::soa_table<Price, Qty> table;
    const auto                      id = table.insert();
    table.assign<Price>(id, Price {2.5});

    const auto present = table.get_expected<Price>(id);
    if (!present.has_value() || present->get().value != 2.5) {
        return 1;
    }

    const auto missing = table.get_expected<Qty>(id);
    if (missing.has_value() || missing.error() != soatable::access_error::missing_column) {
        return 2;
    }

    table.erase(id);
    const auto dead = table.get_expected<Price>(id);
    if (dead.has_value() || dead.error() != soatable::access_error::invalid_row) {
        return 3;
    }
    if (table.try_get<Price>(id) != nullptr) {
        return 4;
    }
    return 0;
}
