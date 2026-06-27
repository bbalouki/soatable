/// @file duplicate_columns.cpp
/// @brief Must fail to compile: a table may not register the same column type twice (is_unique).
/// @author Bertin Balouki SIMYELI

#include "soatable/soatable.hpp"

namespace {
struct Age {
    int value = 0;
};
}  // namespace

int main() {
    soatable::soa_table<Age, Age> table;
    static_cast<void>(table);
    return 0;
}
