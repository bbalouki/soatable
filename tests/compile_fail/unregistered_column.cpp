/// @file unregistered_column.cpp
/// @brief Must fail to compile: accessing a column type that is not part of the table's schema
/// violates the registered_column_v constraint.
/// @author Bertin Balouki SIMYELI

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
    soatable::soa_table<Age> table;
    const auto              id = table.insert();
    static_cast<void>(table.get<Score>(id));
    return 0;
}
