// Finance cookbook: a tick store. Ingest trades, derive P&L per row with a cross-column op, and
// aggregate notional volume per symbol with the query helpers.

#include <cstdint>
#include <string>

#include "soatable/compute.hpp"
#include "soatable/query.hpp"
#include "soatable/soatable.hpp"

#include "output.hpp"

namespace {
struct Symbol {
    std::string value;
};
struct Price {
    double value = 0.0;
};
struct Quantity {
    double value = 0.0;
};
struct Notional {
    double value = 0.0;
};
}  // namespace

int main() {
    soatable::soa_table<Symbol, Price, Quantity, Notional> ticks;

    const auto add_tick = [&](const std::string& symbol, double price, double qty) {
        const auto id = ticks.insert();
        ticks.assign<Symbol>(id, Symbol {symbol});
        ticks.assign<Price>(id, Price {price});
        ticks.assign<Quantity>(id, Quantity {qty});
    };
    add_tick("AAPL", 190.10, 100);
    add_tick("AAPL", 190.50, 50);
    add_tick("MSFT", 410.00, 30);
    add_tick("MSFT", 409.50, 70);

    // notional = price * quantity, computed row-wise over the join of Price and Quantity.
    soatable::compute::assign_from<Notional, Price, Quantity>(
        ticks, [](const Price& p, const Quantity& q) { return Notional {p.value * q.value}; }
    );

    // Total notional volume per symbol.
    const auto volume = soatable::query::group_sum<Symbol, Notional>(
        ticks, [](const Symbol& s) { return s.value; },
        [](const Notional& n) { return n.value; }
    );
    for (const auto& [symbol, total] : volume) {
        OUT_PRINTLN("{}: notional volume {:.2f}", symbol, total);
    }

    // Print the large trades (notional over 10k).
    OUT_PRINTLN("--- large trades ---");
    for (auto row : soatable::query::select_where<Symbol, Notional>(ticks, [](auto trade) {
             return trade.template get<Notional>().value > 10000.0;
         })) {
        OUT_PRINTLN(
            "{} notional {:.2f}", row.template get<Symbol>().value,
            row.template get<Notional>().value
        );
    }
    return 0;
}
