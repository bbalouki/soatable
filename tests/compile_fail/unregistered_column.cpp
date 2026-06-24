// Must fail to compile: accessing a column type that is not part of the table's schema violates the
// registered_column_v constraint.

#include "soatable/soatable.hpp"

namespace {
struct Age {
    int value = 0;
};
struct Score {
    float value = 0.0F;
};
}  // namespace

int main() {
    soatable::SoaTable<Age> table;
    const auto              id = table.insert();
    static_cast<void>(table.get<Score>(id));
    return 0;
}
