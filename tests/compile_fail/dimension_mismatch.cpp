// Must fail to compile: adding quantities of different dimensions is a dimensional-safety error.

#include "soatable/units.hpp"

namespace u = soatable::units;

int main() {
    const u::length<>   metres {1.0};
    const u::duration<> seconds {1.0};
    const auto          nonsense = metres + seconds;  // length + time is ill-formed.
    static_cast<void>(nonsense);
    return 0;
}
