#include <benchmark/benchmark.h>

#include <iostream>
#include <random>
#include <vector>

#include "soatable/soatable.hpp"

struct Temperature {
    double celsius = 0.0;
};
struct Pressure {
    double kpa = 0.0;
};
struct RegionId {
    std::uint32_t value = 0;
};

struct AOSRecord {
    bool        has_temperature = false;
    bool        has_pressure    = false;
    bool        has_region      = false;
    Temperature temperature {};
    Pressure    pressure {};
    RegionId    region {};
};

static void BM_SoaTableSelect(benchmark::State& state) {
    const std::size_t row_count = static_cast<std::size_t>(state.range(0));
    soatable::soa_table<Temperature, Pressure, RegionId> table;
    table.reserve(row_count);

    std::mt19937_64             rng(0x5A0A7EULL);
    std::bernoulli_distribution temp_present(0.70);
    std::bernoulli_distribution pressure_present(0.40);
    std::bernoulli_distribution region_present(0.05);

    for (std::size_t i = 0; i < row_count; ++i) {
        const auto row = table.insert();
        if (temp_present(rng))
            table.assign<Temperature>(row, 25.0);
        if (pressure_present(rng))
            table.assign<Pressure>(row, 101.3);
        if (region_present(rng))
            table.assign<RegionId>(row, 1);
    }

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
    const std::size_t      row_count = static_cast<std::size_t>(state.range(0));
    std::vector<AOSRecord> aos;
    aos.reserve(row_count);

    std::mt19937_64             rng(0x5A0A7EULL);
    std::bernoulli_distribution temp_present(0.70);
    std::bernoulli_distribution pressure_present(0.40);
    std::bernoulli_distribution region_present(0.05);

    for (std::size_t i = 0; i < row_count; ++i) {
        AOSRecord record;
        if (temp_present(rng)) {
            record.has_temperature     = true;
            record.temperature.celsius = 25.0;
        }
        if (pressure_present(rng)) {
            record.has_pressure = true;
            record.pressure.kpa = 101.3;
        }
        if (region_present(rng)) {
            record.has_region   = true;
            record.region.value = 1;
        }
        aos.push_back(record);
    }

    for (auto _ : state) {
        double sum = 0.0;
        for (const auto& record : aos) {
            if (record.has_temperature && record.has_pressure && record.has_region) {
                sum += record.temperature.celsius + record.pressure.kpa + record.region.value;
            }
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_AoSBranchScan)->Arg(100'000)->Arg(250'000);

BENCHMARK_MAIN();
