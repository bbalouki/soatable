/// @file soatable_example.cpp
/// @brief A guided tour exercising most of SoaTable's headers in one runnable program.
/// @author Bertin Balouki SIMYELI
///
/// Walks through the container basics, reference-semantic views, the non-throwing accessor, zero-copy
/// spans and validity bitmaps, the compute layer, query aggregation, binary serialization round-trip,
/// tiled (AoSoA) storage, dimensional units, time-series helpers, and the runtime dynamic table.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "soatable/compute.hpp"
#include "soatable/dynamic.hpp"
#include "soatable/query.hpp"
#include "soatable/serialize.hpp"
#include "soatable/soatable.hpp"
#include "soatable/timeseries.hpp"
#include "soatable/units.hpp"

#if SOATABLE_HAS_PRINT
#include <print>
#define OUT_PRINTLN(...) std::println(__VA_ARGS__)
#else
#include <format>
#include <iostream>
#define OUT_PRINTLN(...) std::cout << std::format(__VA_ARGS__) << std::endl
#endif

namespace {
struct Symbol {
    std::string value;
};
struct Price {
    double value = 0.0;
};
struct Qty {
    int value = 0;
};
struct Notional {
    double value = 0.0;
};
using Book = soatable::soa_table<Symbol, Price, Qty, Notional>;

Book make_book() {
    Book       book;
    const auto add = [&](const std::string& symbol, double price, int qty) {
        const auto id = book.insert();  // Stable generational handle.
        book.assign<Symbol>(id, Symbol {symbol});
        book.assign<Price>(id, Price {price});
        book.assign<Qty>(id, Qty {qty});
    };
    add("AAPL", 190.0, 100);
    add("AAPL", 191.0, 50);
    add("MSFT", 410.0, 30);
    return book;
}
}  // namespace

int main() {
    Book book = make_book();

    // 1. Reference-semantic views: structured bindings yield real references (no .get()).
    OUT_PRINTLN("== view ==");
    for (auto [id, symbol, price] : book.view<Symbol, Price>()) {
        static_cast<void>(id);
        OUT_PRINTLN("{} @ {:.2f}", symbol.value, price.value);
    }

    // 2. Compute: notional = price * qty over the Price/Qty join, then a span reduction.
    soatable::compute::assign_from<Notional, Price, Qty>(
        book, [](const Price& p, const Qty& q) { return Notional {p.value * q.value}; }
    );
    const double gross = soatable::compute::reduce_column<Notional>(
        book, 0.0, [](double acc, const Notional& n) { return acc + n.value; }
    );
    OUT_PRINTLN("== compute == gross notional {:.2f}", gross);

    // 3. Query: total notional per symbol.
    OUT_PRINTLN("== query ==");
    const auto volume = soatable::query::group_sum<Symbol, Notional>(
        book, [](const Symbol& s) { return s.value; }, [](const Notional& n) { return n.value; }
    );
    for (const auto& [symbol, total] : volume) {
        OUT_PRINTLN("{}: {:.2f}", symbol, total);
    }

    // 4. Zero-copy span + validity bitmap.
    const std::span<const Price> prices = book.column<Price>();
    OUT_PRINTLN("== span == {} prices, {} rows priced", prices.size(),
                book.validity<Price>().count());

    // 5. Serialization round-trip over a trivially-copyable schema.
    soatable::soa_table<Price, Qty> pod;
    for (auto [id, price, qty] : book.select<Price, Qty>()) {
        static_cast<void>(id);
        const auto row = pod.insert();
        pod.assign<Price>(row, price.get());
        pod.assign<Qty>(row, qty.get());
    }
    const auto                      bytes = soatable::save(pod);
    soatable::soa_table<Price, Qty> restored;
    const auto                      status = soatable::load(restored, bytes);
    OUT_PRINTLN("== serialize == {} bytes, restored {} rows, ok={}", bytes.size(), restored.size(),
                status == soatable::serialize_status::ok);

    // 6. Tiled (AoSoA) storage with per-chunk aligned spans.
    soatable::aosoa_table<4, Price> tiled;
    for (int i = 0; i < 10; ++i) {
        tiled.assign<Price>(tiled.insert(), Price {static_cast<double>(i)});
    }
    OUT_PRINTLN("== aosoa == {} price chunks", tiled.column_tiles<Price>().size());

    // 7. Dimensional units: velocity = distance / time, checked at compile time.
    namespace un      = soatable::units;
    const auto speed  = un::length<> {100.0} / un::duration<> {4.0};
    OUT_PRINTLN("== units == speed {:.1f} (length/time)", speed.value());

    // 8. Time-series rolling mean over the price column's dense order.
    const auto means = soatable::timeseries::rolling_mean_column<Price>(
        book, [](const Price& p) { return p.value; }, 2
    );
    OUT_PRINTLN("== timeseries == last 2-window mean {:.2f}", means.back());

    // 9. Runtime dynamic schema with metadata.
    soatable::dynamic_table dyn;
    dyn.add_column<double>("altitude");
    dyn.set_metadata("altitude", "unit", "metres");
    const auto sample = dyn.insert_row();
    dyn.set<double>(sample, "altitude", 1280.0);
    OUT_PRINTLN("== dynamic == altitude {:.0f} {}", *dyn.get<double>(sample, "altitude"),
                *dyn.get_metadata("altitude", "unit"));

    return 0;
}
