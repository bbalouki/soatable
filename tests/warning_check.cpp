// Compiles the public header under the project's strict warning set (no GoogleTest headers in the
// translation unit) so that -Wconversion / -Wsign-conversion regressions surface as build failures.

#include "soatable/soatable.hpp"

namespace {
struct Position {
    double value = 0.0;
};
struct Velocity {
    double value = 0.0;
};
}  // namespace

int main() {
    soatable::soa_table<Position, Velocity> table;
    const auto                             id = table.insert();
    table.assign<Position>(id, Position {1.0});
    table.assign<Velocity>(id, Velocity {2.0});

    double total = 0.0;
    for (auto [row, position, velocity] : table.view<Position, Velocity>()) {
        static_cast<void>(row);
        total += position.value + velocity.value;
    }

    if (const auto found = table.get_expected<Position>(id); found.has_value()) {
        total += found->get().value;
    }

    table.sort_by_column<Position>(
        [](const Position& lhs, const Position& rhs) { return lhs.value < rhs.value; }
    );
    table.erase(id);

    return total > 0.0 ? 0 : 0;
}
