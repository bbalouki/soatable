#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "soatable/soatable.hpp"

#if SOATABLE_HAS_PRINT
#include <print>
#define OUT_PRINTLN(...) std::println(__VA_ARGS__)
#else
#include <format>
#define OUT_PRINTLN(...) std::cout << std::format(__VA_ARGS__) << '\n'
#endif

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

struct IsActive {};  // Component with no data (tag)

using Table = soatable::
    soa_table<CashFlow, AccountId, RiskScore, Mass, Velocity, Position, ObjectName, IsActive>;
using Row = soatable::
    row_handle<CashFlow, AccountId, RiskScore, Mass, Velocity, Position, ObjectName, IsActive>;

int main() {
    Table database;
    database.reserve(32);

    auto make_row = [&](Table& table) { return Row {table.insert(), table}; };

    OUT_PRINTLN(" Basic Operations ");
    auto transaction_a = make_row(database);
    transaction_a.assign<AccountId>(99014522);
    transaction_a.assign<CashFlow>(1450000.50);
    transaction_a.assign<RiskScore>(12.50);
    transaction_a.assign<ObjectName>("High-Yield Corporate Bond Portfolio");
    transaction_a.assign<IsActive>();

    auto transaction_b = make_row(database);
    transaction_b.assign<AccountId>(88231011);
    transaction_b.assign<CashFlow>(-320000.00);
    transaction_b.assign<RiskScore>(45.80);
    transaction_b.assign<ObjectName>("Short-Selling Speculative Derivative Margin");

    OUT_PRINTLN("Finance rows:");
    double net_balance = 0.0;
    for (auto [id, flow, risk, name] : database.select<CashFlow, RiskScore, ObjectName>()) {
        OUT_PRINTLN("  row {}: {}", id.index, name.get().value);
        OUT_PRINTLN("    flow={}, risk={}", flow.get().value, risk.get().value.get());
        net_balance += flow.get().value;
    }
    OUT_PRINTLN("  net balance = {}\n", net_balance);

    OUT_PRINTLN(" Optional Columns Selection ");
    // Using std::optional to join required and optional columns
    for (auto [id, name, active] : database.select<ObjectName, std::optional<IsActive>>()) {
        OUT_PRINTLN("  row {}: {} (Active: {})", id.index, name.get().value, active.has_value());
    }

    OUT_PRINTLN("\n Physics Simulation Scenario ");
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

    const double dt = 0.016;
    for (auto [id, position, velocity, mass, name] :
         database.select<Position, Velocity, Mass, ObjectName>()) {
        position.get().x += velocity.get().dx * dt;
        position.get().y += velocity.get().dy * dt;
        OUT_PRINTLN(
            "  row {}: {} ({} kg) -> ({}, {})",
            id.index,
            name.get().value,
            mass.get().kg,
            position.get().x,
            position.get().y
        );
    }

    OUT_PRINTLN("\n Batch Operations ");
    auto                    batch_ids = database.insert_batch(3);
    std::vector<ObjectName> names     = {{"Batch 1"}, {"Batch 2"}, {"Batch 3"}};
    database.assign_batch<ObjectName>(batch_ids, names.begin());
    OUT_PRINTLN("  Inserted and assigned 3 rows via batch operations.");

    OUT_PRINTLN("\n Sorting ");
    OUT_PRINTLN("Sorting by cash flow:");
    database.sort_by<CashFlow>([](const CashFlow& lhs, const CashFlow& rhs) {
        return lhs.value < rhs.value;
    });
    for (auto [id, flow, name] : database.select<CashFlow, ObjectName>()) {
        OUT_PRINTLN("  row {}: {} :: {}", id.index, flow.get().value, name.get().value);
    }

    OUT_PRINTLN("\n Handle Safety & Recycling ");
    OUT_PRINTLN("  body_a valid before erase: {}", body_a.is_valid());
    body_a.erase();
    OUT_PRINTLN("  body_a valid after erase: {}", body_a.is_valid());

    auto recycled = make_row(database);
    recycled.assign<CashFlow>(10500.0);
    recycled.assign<ObjectName>("Recycled Micro-Loan Settlement");
    OUT_PRINTLN(
        "  recycled row = (index: {}, generation: {})", recycled.id.index, recycled.id.generation
    );
    OUT_PRINTLN("  Stale body_a handle valid: {} (should be false)", body_a.is_valid());

    OUT_PRINTLN("\n Edge Case: Multi-column sorting with missing values ");
    // Sort by AccountId. Rows without AccountId should be handled gracefully.
    database.sort_by_multi(
        std::pair<AccountId, std::function<bool(const AccountId&, const AccountId&)>> {
            {}, [](const AccountId& a, const AccountId& b) { return a.value < b.value; }
        }
    );

    OUT_PRINTLN("  Sorted by AccountId (rows without it should be last):");
    for (auto row : database.rows()) {
        auto* acc  = database.try_get<AccountId>(row);
        auto* name = database.try_get<ObjectName>(row);
        OUT_PRINTLN(
            "    Row {}: Account={}, Name={}",
            row.index,
            acc ? std::to_string(acc->value) : "N/A",
            name ? name->value : "N/A"
        );
    }

    return 0;
}
