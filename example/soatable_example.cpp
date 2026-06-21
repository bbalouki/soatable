#include <cstdint>
#include <iostream>
#include <string>

#include <soatable/soatable.hpp>

struct CashFlow {
  double value = 0.0;
};

struct AccountId {
  std::uint64_t value = 0;
};

struct RiskScore {
  sstd::quantized_float<std::uint8_t, 0, 100000, 8> value;
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

using Table = soatable::soa_table<CashFlow, AccountId, RiskScore, Mass, Velocity,
                                  Position, ObjectName>;
using Row = soatable::row_handle<CashFlow, AccountId, RiskScore, Mass, Velocity,
                                 Position, ObjectName>;

int main() {
  Table database;
  database.reserve(32);

  auto make_row = [&](Table& table) { return Row{table.insert(), table}; };

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

  std::cout << "Finance rows\n";
  double net_balance = 0.0;
  for (auto [id, flow, risk, name] :
       database.select<CashFlow, RiskScore, ObjectName>()) {
    std::cout << "  row " << id.index << ": " << name.value << '\n';
    std::cout << "    flow=" << flow.value << ", risk=" << risk.value.get()
              << '\n';
    net_balance += flow.value;
  }
  std::cout << "  net balance = " << net_balance << "\n\n";

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

  std::cout << "Physics step\n";
  const double dt = 0.016;
  for (auto [id, position, velocity, mass, name] :
       database.select<Position, Velocity, Mass, ObjectName>()) {
    position.x += velocity.dx * dt;
    position.y += velocity.dy * dt;
    std::cout << "  row " << id.index << ": " << name.value << " (" << mass.kg
              << " kg) -> (" << position.x << ", " << position.y << ")\n";
  }

  std::cout << "\nSorted by cash flow\n";
  database.sort_by<CashFlow>([](const CashFlow& lhs, const CashFlow& rhs) {
    return lhs.value < rhs.value;
  });
  for (auto [id, flow, name] : database.select<CashFlow, ObjectName>()) {
    std::cout << "  row " << id.index << ": " << flow.value << " :: "
              << name.value << '\n';
  }

  std::cout << "\nHandle safety\n";
  std::cout << "  body_a valid before erase: " << body_a.is_valid() << '\n';
  const bool erased = body_a.erase();
  std::cout << "  erase returned: " << erased << '\n';
  std::cout << "  body_a valid after erase: " << body_a.is_valid() << '\n';

  auto recycled = make_row(database);
  recycled.assign<CashFlow>(10500.0);
  recycled.assign<ObjectName>("Recycled Micro-Loan Settlement");
  std::cout << "  recycled row = (" << recycled.id.index << ", "
            << recycled.id.generation << ")\n";
  std::cout << "  stale handle valid: " << body_a.is_valid() << '\n';

  return 0;
}
