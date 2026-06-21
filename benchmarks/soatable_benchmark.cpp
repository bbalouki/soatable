#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <soatable/soatable.hpp>

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
  bool has_temperature = false;
  bool has_pressure = false;
  bool has_region = false;
  Temperature temperature{};
  Pressure pressure{};
  RegionId region{};
};

using Clock = std::chrono::steady_clock;

template <typename Func>
double time_stage(const std::string& name, int iterations, Func&& func) {
  double checksum = 0.0;
  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    checksum += func();
  }
  const auto end = Clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  const double per_iteration =
      static_cast<double>(elapsed.count()) / static_cast<double>(iterations);

  std::cout << std::left << std::setw(28) << name << " "
            << std::right << std::setw(10) << per_iteration << " us/iter"
            << "  checksum=" << checksum << '\n';
  return checksum;
}

int main() {
  constexpr std::size_t row_count = 250'000;
  constexpr int iterations = 12;

  std::mt19937_64 rng(0x5A0A7EULL);
  std::bernoulli_distribution temp_present(0.70);
  std::bernoulli_distribution pressure_present(0.40);
  std::bernoulli_distribution region_present(0.05);
  std::uniform_real_distribution<double> temp_dist(-20.0, 45.0);
  std::uniform_real_distribution<double> pressure_dist(95.0, 110.0);
  std::uniform_int_distribution<std::uint32_t> region_dist(1, 1024);

  sstd::soa_table<Temperature, Pressure, RegionId> table;
  table.reserve(row_count);

  std::vector<AOSRecord> aos;
  aos.reserve(row_count);

  for (std::size_t i = 0; i < row_count; ++i) {
    AOSRecord record;
    const auto row = table.insert();

    if (temp_present(rng)) {
      record.has_temperature = true;
      record.temperature.celsius = temp_dist(rng);
      table.assign<Temperature>(row, record.temperature.celsius);
    }

    if (pressure_present(rng)) {
      record.has_pressure = true;
      record.pressure.kpa = pressure_dist(rng);
      table.assign<Pressure>(row, record.pressure.kpa);
    }

    if (region_present(rng)) {
      record.has_region = true;
      record.region.value = region_dist(rng);
      table.assign<RegionId>(row, record.region.value);
    }

    aos.push_back(record);
  }

  std::cout << "SoaTable benchmark\n";
  std::cout << "Rows: " << row_count << ", iterations: " << iterations << '\n';
  std::cout << "Query: Temperature + Pressure + RegionId\n";
  std::cout << "Data is intentionally sparse so the smallest populated column "
               "drives the join.\n\n";

  auto soa_checksum = time_stage("SoaTable select", iterations, [&]() {
    double sum = 0.0;
    for (auto [id, temperature, pressure, region] :
         table.select<Temperature, Pressure, RegionId>()) {
      (void)id;
      sum += temperature.celsius + pressure.kpa + region.value;
    }
    return sum;
  });

  auto aos_checksum = time_stage("AoS branch scan", iterations, [&]() {
    double sum = 0.0;
    for (const auto& record : aos) {
      if (record.has_temperature && record.has_pressure && record.has_region) {
        sum += record.temperature.celsius + record.pressure.kpa +
               record.region.value;
      }
    }
    return sum;
  });

  std::cout << "\nChecksum delta: " << (soa_checksum - aos_checksum) << '\n';
  return 0;
}
