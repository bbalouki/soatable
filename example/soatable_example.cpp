#include <cstdint>
#include <print>
#include <string>

#include "soatable/soatable.hpp"

struct CashFlow {
    double value = 0.0;
};

struct AccountId {
    std::uint64_t value = 0;
};

struct RiskScore {
    soatable::quantized_float<std::uint8_t, 0, 100000, 8> value;
};

struct Mass {
    double kg = 0.0;
};

struct Velocity {
    double dx = 0.0;
    double dy = 0.0;
};

struct Position {
    double x = 0.0;
    double y = 0.0;
};

struct ObjectName {
    std::string value;
};

using Table =
    soatable::SoaTable<CashFlow, AccountId, RiskScore, Mass, Velocity, Position, ObjectName>;
using Row =
    soatable::RowHandle<CashFlow, AccountId, RiskScore, Mass, Velocity, Position, ObjectName>;

int main() {
    Table database;
    database.reserve(32);

    auto make_row = [&](Table& table) { return Row {table.insert(), table}; };

    auto transaction_a = make_row(database);
    transaction_a.assign<AccountId>(99014522);
    transaction_a.assign<CashFlow>(1450000.50);
    transaction_a.assign<RiskScore>(12.50);
    transaction_a.assign<ObjectName>("High-Yield Corporate Bond Portfolio");

    auto transaction_b = make_row(database);
    transaction_b.assign<AccountId>(88231011);
    transaction_b.assign<CashFlow>(-320000.00);
    transaction_b.assign<RiskScore>(45.80);
    transaction_b.assign<ObjectName>("Short-Selling Speculative Derivative Margin");

    std::println("Finance rows");
    double net_balance = 0.0;
    for (auto [id, flow, risk, name] : database.select<CashFlow, RiskScore, ObjectName>()) {
        std::println("  row {}: {}", id.index, name.get().value);
        std::println("    flow={}, risk={}", flow.get().value, risk.get().value.get());
        net_balance += flow.get().value;
    }
    std::println("  net balance = {}\n", net_balance);

    auto body_a = make_row(database);
    body_a.assign<Mass>(1200.0);
    body_a.assign<Position>(0.0, 0.0);
    body_a.assign<Velocity>(1.5, -0.5);
    body_a.assign<ObjectName>("Dynamic Projectile A");

    auto body_b = make_row(database);
    body_b.assign<Mass>(50.0);
    body_b.assign<Position>(10.0, 50.0);
    body_b.assign<Velocity>(0.0, 9.81);
    body_b.assign<ObjectName>("Dynamic Projectile B");

    std::println("Physics step");
    const double dt = 0.016;
    for (auto [id, position, velocity, mass, name] :
         database.select<Position, Velocity, Mass, ObjectName>()) {
        position.get().x += velocity.get().dx * dt;
        position.get().y += velocity.get().dy * dt;
        std::println(
            "  row {}: {} ({} kg) -> ({}, {})",
            id.index,
            name.get().value,
            mass.get().kg,
            position.get().x,
            position.get().y
        );
    }

    std::println("\nSorted by cash flow");
    database.sort_by<CashFlow>([](const CashFlow& lhs, const CashFlow& rhs) {
        return lhs.value < rhs.value;
    });
    for (auto [id, flow, name] : database.select<CashFlow, ObjectName>()) {
        std::println("  row {}: {} :: {}", id.index, flow.get().value, name.get().value);
    }

    std::println("\nHandle safety");
    std::println("  body_a valid before erase: {}", body_a.is_valid());
    const bool erased = body_a.erase();
    std::println("  erase returned: {}", erased);
    std::println("  body_a valid after erase: {}", body_a.is_valid());

    auto recycled = make_row(database);
    recycled.assign<CashFlow>(10500.0);
    recycled.assign<ObjectName>("Recycled Micro-Loan Settlement");
    std::println("  recycled row = ({}, {})", recycled.id.index, recycled.id.generation);
    std::println("  stale handle valid: {}", body_a.is_valid());

    return 0;
}
