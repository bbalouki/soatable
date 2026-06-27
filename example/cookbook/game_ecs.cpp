/// @file game_ecs.cpp
/// @brief Game / ECS cookbook: a world of entities with sparse components.
/// @author Bertin Balouki SIMYELI
///
/// Integrate motion over the entities that have both Position and Velocity, and tag a subset with a
/// data-less component.

#include <string>

#include "output.hpp"
#include "soatable/soatable.hpp"

namespace {
struct Position {
    float x = 0.0F;
    float y = 0.0F;
};
struct Velocity {
    float x = 0.0F;
    float y = 0.0F;
};
struct Name {
    std::string value;
};
struct Frozen {};  // A data-less tag component.
}  // namespace

int main() {
    soatable::soa_table<Position, Velocity, Name, Frozen> world;

    const auto spawn = [&](const std::string& name, Position pos, Velocity vel) {
        const auto entity = world.insert();
        world.assign<Name>(entity, Name {name});
        world.assign<Position>(entity, pos);
        world.assign<Velocity>(entity, vel);
        return entity;
    };
    spawn("player", Position {0.0F, 0.0F}, Velocity {1.0F, 2.0F});
    const auto rock = spawn("rock", Position {5.0F, 5.0F}, Velocity {0.0F, 0.0F});
    spawn("enemy", Position {10.0F, 0.0F}, Velocity {-1.0F, 0.0F});
    world.assign<Frozen>(rock);  // The rock does not move (data-less tag).

    // Motion system: advance every entity that has Position and Velocity and is not Frozen.
    constexpr float time_step = 0.5F;
    for (auto [id, position, velocity] : world.view<Position, Velocity>()) {
        if (world.contains<Frozen>(id)) {
            continue;
        }
        position.x += velocity.x * time_step;
        position.y += velocity.y * time_step;
    }

    for (auto [id, name, position] : world.select<Name, Position>()) {
        static_cast<void>(id);
        OUT_PRINTLN("{} at ({:.1f}, {:.1f})", name.get().value, position.get().x, position.get().y);
    }
    return 0;
}
