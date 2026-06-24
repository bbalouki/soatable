// SoaTable benchmark suite. Covers the axes where regressions hide: insertion, erase-churn,
// single-column and parallel sort, sparse selection across density sweeps, and the select driver
// heuristic. Baselines compare against an Array-of-Structures scan and a hand-rolled columnar scan.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <random>
#include <vector>

#include "soatable/soatable.hpp"

namespace {

struct Temperature {
    double celsius = 0.0;
};
struct Pressure {
    double kpa = 0.0;
};
struct RegionId {
    std::uint32_t value = 0;
};

using SensorTable = soatable::soa_table<Temperature, Pressure, RegionId>;

// Deterministic per-row values so sort benchmarks do real work without consuming the presence RNG.
constexpr double temperature_value(std::size_t i) {
    return static_cast<double>((i * 48271U) % 1000U);
}
constexpr double pressure_value(std::size_t i) {
    return static_cast<double>((i * 16807U) % 1000U);
}

// Build a table of the given size with the given per-column presence probabilities (fixed seed).
SensorTable build_table(
    std::size_t row_count, double temp_p, double pressure_p, double region_p
) {
    SensorTable table;
    table.reserve(row_count);

    std::mt19937_64             rng(0x5A0A7EULL);
    std::bernoulli_distribution temp_present(temp_p);
    std::bernoulli_distribution pressure_present(pressure_p);
    std::bernoulli_distribution region_present(region_p);

    for (std::size_t i = 0; i < row_count; ++i) {
        const auto row = table.insert();
        if (temp_present(rng)) {
            table.assign<Temperature>(row, Temperature {temperature_value(i)});
        }
        if (pressure_present(rng)) {
            table.assign<Pressure>(row, Pressure {pressure_value(i)});
        }
        if (region_present(rng)) {
            table.assign<RegionId>(row, RegionId {static_cast<std::uint32_t>(i % 64U)});
        }
    }
    return table;
}

}  // namespace

// --- Insertion --------------------------------------------------------------------------------
static void BM_Insert(benchmark::State& state) {
    const std::size_t row_count = static_cast<std::size_t>(state.range(0));
    for (auto _ : state) {
        SensorTable table;
        table.reserve(row_count);
        for (std::size_t i = 0; i < row_count; ++i) {
            const auto row = table.insert();
            table.assign<Temperature>(row, Temperature {temperature_value(i)});
        }
        benchmark::DoNotOptimize(table.size());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Insert)->Arg(100'000);

// --- Erase churn ------------------------------------------------------------------------------
static void BM_EraseChurn(benchmark::State& state) {
    const std::size_t row_count = static_cast<std::size_t>(state.range(0));
    for (auto _ : state) {
        SensorTable                   table;
        std::vector<soatable::row_id> ids;
        ids.reserve(row_count);
        table.reserve(row_count);
        for (std::size_t i = 0; i < row_count; ++i) {
            const auto row = table.insert();
            table.assign<Temperature>(row, Temperature {temperature_value(i)});
            ids.push_back(row);
        }
        for (const auto id : ids) {
            table.erase(id);
        }
        benchmark::DoNotOptimize(table.size());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0) * 2);
}
BENCHMARK(BM_EraseChurn)->Arg(100'000);

// --- Sorting ----------------------------------------------------------------------------------
static void BM_SortByColumn(benchmark::State& state) {
    const std::size_t row_count = static_cast<std::size_t>(state.range(0));
    SensorTable       table     = build_table(row_count, 1.0, 1.0, 1.0);
    bool              ascending = true;
    for (auto _ : state) {
        if (ascending) {
            table.sort_by_column<Temperature>(
                [](const Temperature& a, const Temperature& b) { return a.celsius < b.celsius; }
            );
        } else {
            table.sort_by_column<Temperature>(
                [](const Temperature& a, const Temperature& b) { return a.celsius > b.celsius; }
            );
        }
        ascending = !ascending;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_SortByColumn)->Arg(100'000);

static void BM_SortByColumnParallel(benchmark::State& state) {
    const std::size_t row_count = static_cast<std::size_t>(state.range(0));
    SensorTable       table     = build_table(row_count, 1.0, 1.0, 1.0);
    bool              ascending = true;
    for (auto _ : state) {
        if (ascending) {
            table.sort_by_column_parallel<Temperature>(
                [](const Temperature& a, const Temperature& b) { return a.celsius < b.celsius; }
            );
        } else {
            table.sort_by_column_parallel<Temperature>(
                [](const Temperature& a, const Temperature& b) { return a.celsius > b.celsius; }
            );
        }
        ascending = !ascending;
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_SortByColumnParallel)->Arg(100'000);

// --- Selection: SoaTable vs baselines ---------------------------------------------------------
static void BM_SoaTableSelect(benchmark::State& state) {
    const std::size_t row_count = static_cast<std::size_t>(state.range(0));
    SensorTable       table     = build_table(row_count, 0.70, 0.40, 0.05);
    for (auto _ : state) {
        double sum = 0.0;
        for (auto [id, temperature, pressure, region] :
             table.select<Temperature, Pressure, RegionId>()) {
            sum += temperature.get().celsius + pressure.get().kpa + region.get().value;
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_SoaTableSelect)->Arg(100'000)->Arg(250'000);

static void BM_AoSBranchScan(benchmark::State& state) {
    struct AosRecord {
        bool   has_temperature = false;
        bool   has_pressure    = false;
        bool   has_region      = false;
        double celsius         = 0.0;
        double kpa             = 0.0;
        std::uint32_t region   = 0;
    };

    const std::size_t      row_count = static_cast<std::size_t>(state.range(0));
    std::vector<AosRecord> aos;
    aos.reserve(row_count);

    std::mt19937_64             rng(0x5A0A7EULL);
    std::bernoulli_distribution temp_present(0.70);
    std::bernoulli_distribution pressure_present(0.40);
    std::bernoulli_distribution region_present(0.05);
    for (std::size_t i = 0; i < row_count; ++i) {
        AosRecord record;
        if (temp_present(rng)) {
            record.has_temperature = true;
            record.celsius         = temperature_value(i);
        }
        if (pressure_present(rng)) {
            record.has_pressure = true;
            record.kpa          = pressure_value(i);
        }
        if (region_present(rng)) {
            record.has_region = true;
            record.region     = static_cast<std::uint32_t>(i % 64U);
        }
        aos.push_back(record);
    }

    for (auto _ : state) {
        double sum = 0.0;
        for (const auto& record : aos) {
            if (record.has_temperature && record.has_pressure && record.has_region) {
                sum += record.celsius + record.kpa + record.region;
            }
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_AoSBranchScan)->Arg(100'000)->Arg(250'000);

// --- Density sweep: select cost should track the smallest required column ---------------------
static void BM_SelectDensitySweep(benchmark::State& state) {
    const std::size_t row_count      = static_cast<std::size_t>(state.range(0));
    const double      region_density = static_cast<double>(state.range(1)) / 100.0;
    SensorTable       table          = build_table(row_count, 0.90, 0.90, region_density);
    for (auto _ : state) {
        double sum = 0.0;
        for (auto [id, temperature, pressure, region] :
             table.select<Temperature, Pressure, RegionId>()) {
            sum += temperature.get().celsius + pressure.get().kpa + region.get().value;
        }
        benchmark::DoNotOptimize(sum);
    }
}
// Same row count, increasing density of the driver column (1%, 5%, 20%, 50%).
BENCHMARK(BM_SelectDensitySweep)
    ->Args({250'000, 1})
    ->Args({250'000, 5})
    ->Args({250'000, 20})
    ->Args({250'000, 50});

// --- Driver heuristic validation: smallest column should drive iteration ----------------------
// RegionId is sparse and Temperature is dense; the auto driver picks RegionId. The forced variant
// drives off the dense Temperature column and filters manually, modelling a wrong driver choice.
static void BM_SelectAutoSmallestDriver(benchmark::State& state) {
    const std::size_t row_count = static_cast<std::size_t>(state.range(0));
    SensorTable       table     = build_table(row_count, 0.95, 0.95, 0.02);
    for (auto _ : state) {
        double sum = 0.0;
        for (auto [id, temperature, pressure, region] :
             table.select<Temperature, Pressure, RegionId>()) {
            sum += temperature.get().celsius + pressure.get().kpa + region.get().value;
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_SelectAutoSmallestDriver)->Arg(250'000);

static void BM_SelectForcedLargestDriver(benchmark::State& state) {
    const std::size_t row_count = static_cast<std::size_t>(state.range(0));
    SensorTable       table     = build_table(row_count, 0.95, 0.95, 0.02);
    for (auto _ : state) {
        double sum = 0.0;
        // Drive off the dense Temperature column, then filter the other columns by hand.
        for (auto [id, temperature] : table.select<Temperature>()) {
            const auto* pressure = table.try_get<Pressure>(id);
            const auto* region   = table.try_get<RegionId>(id);
            if (pressure != nullptr && region != nullptr) {
                sum += temperature.get().celsius + pressure->kpa + region->value;
            }
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_SelectForcedLargestDriver)->Arg(250'000);

// --- Hand-rolled columnar (SoA) full scan baseline --------------------------------------------
static void BM_HandRolledSoAScan(benchmark::State& state) {
    const std::size_t          row_count = static_cast<std::size_t>(state.range(0));
    std::vector<double>        celsius(row_count);
    std::vector<double>        kpa(row_count);
    std::vector<std::uint32_t> region(row_count);
    std::vector<char>          has_temp(row_count, 0);
    std::vector<char>          has_pres(row_count, 0);
    std::vector<char>          has_region(row_count, 0);

    std::mt19937_64             rng(0x5A0A7EULL);
    std::bernoulli_distribution temp_present(0.70);
    std::bernoulli_distribution pressure_present(0.40);
    std::bernoulli_distribution region_present(0.05);
    for (std::size_t i = 0; i < row_count; ++i) {
        if (temp_present(rng)) {
            has_temp[i] = 1;
            celsius[i]  = temperature_value(i);
        }
        if (pressure_present(rng)) {
            has_pres[i] = 1;
            kpa[i]      = pressure_value(i);
        }
        if (region_present(rng)) {
            has_region[i] = 1;
            region[i]     = static_cast<std::uint32_t>(i % 64U);
        }
    }

    for (auto _ : state) {
        double sum = 0.0;
        for (std::size_t i = 0; i < row_count; ++i) {
            if (has_temp[i] != 0 && has_pres[i] != 0 && has_region[i] != 0) {
                sum += celsius[i] + kpa[i] + region[i];
            }
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_HandRolledSoAScan)->Arg(100'000)->Arg(250'000);

BENCHMARK_MAIN();
