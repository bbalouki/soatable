// Must fail to compile: a table must declare at least one column type.

#include "soatable/soatable.hpp"

int main() {
    soatable::soa_table<> table;
    static_cast<void>(table);
    return 0;
}
