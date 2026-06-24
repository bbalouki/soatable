# SoaTable

[![Build](https://github.com/bbalouki/soatable/actions/workflows/ci.yaml/badge.svg)](https://github.com/bbalouki/soatable/actions/workflows/ci.yaml)
![C++](https://img.shields.io/badge/C%2B%2B-23-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.25%2B-green.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)
![Header-only](https://img.shields.io/badge/Header--only-Yes-blue)
![Dependencies](https://img.shields.io/badge/Dependencies-None-brightgreen)
![Namespace](https://img.shields.io/badge/Namespace-soatable-lightgrey)
![Performance](https://img.shields.io/badge/Speedup-5x--15x-orange)

SoaTable is a high-performance, header-only C++23 library providing a sparse column-oriented data structure. It is designed for workloads where data is naturally row-addressed but frequently queried by a subset of fields, offering significant performance gains through cache-friendly memory layout and efficient joins.

## Table of Contents

- [Core Concepts](#core-concepts)
- [Why Use SoaTable?](#why-use-soatable)
- [Features](#features)
- [Quick Start](#quick-start)
- [Real-World Example](#real-world-example)
- [Installation](#installation)
- [Benchmarks](#benchmarks)
- [License](#license)

## Core Concepts

SoaTable combines three powerful ideas:

- **Stable Generational Row Handles:** Use `row_id` to refer to rows without worrying about the ABA problem or pointer invalidation.
- **Sparse Column Storage:** Only pay for the columns a row actually has. Memory is managed densely per column while appearing sparse per row.
- **Efficient Joins:** The `select<...>()` mechanism automatically uses the smallest requested column as a "driver," minimizing iteration overhead during sparse joins.

## Why Use SoaTable?

Most applications start with an **Array of Structures (AoS)** layout:

```cpp
struct Particle {
    double x, y, vx, vy;
    std::string name;
};
std::vector<Particle> particles;
```

While simple, AoS becomes inefficient when:

1. **Workloads touch only a few fields:** Scanning the whole array pulls unrelated members into cache lines.
2. **Schema is sparse:** Many rows have optional fields, leading to wasted memory or complex pointer-heavy structures.
3. **Identity stability is required:** Deleting elements from a vector invalidates indices or requires expensive shifting.

**SoaTable** solves these by storing data in a **Structure of Arrays (SoA)** format internally, while providing a row-oriented abstraction for insertion and handles.

### Performance Benefits

In our benchmarks, SoaTable's `select` queries are often **5x to 15x faster** than traditional AoS branch-and-scan methods for sparse data. By focusing iteration on the most selective column, SoaTable avoids checking every single row for presence.

## Features

- **C++23 Standard:** Leverages modern features like `std::views`, `std::print`, and advanced template metaprogramming.
- **Stable IDs:** `row_id` handles remain valid until the row is explicitly erased, even if other rows are added or reordered.
- **Sparse Projection:** `select<T, std::optional<U>>()` allows joining required and optional columns seamlessly.
- **Batch Operations:** `insert_batch` and `assign_batch` for high-throughput data loading.
- **Parallel Sorting:** Physically reorder columns in parallel based on a comparison criteria.
- **Multi-Column Sorting:** Sort by primary, secondary, etc., keys.
- **Data Compression Utilities:** Includes `quantized_float`, `packed_bits`, and `delta_value` for compact storage.
- **Reference-Semantic Row Views:** `view<A, B>()` yields `row_view` proxies so structured bindings give real references (`for (auto [id, a, b] : table.view<A, B>()) a.x += b.y;`) without `.get()`.
- **Non-Throwing Accessors:** `get_expected<T>()` returns `std::expected<std::reference_wrapper<T>, access_error>` for hot paths and no-exceptions builds, alongside throwing `get<T>()` and nullable `try_get<T>()`.
- **Zero-Copy Column Spans:** `column<T>()` hands a column's dense values to BLAS / a SIMD kernel / numpy with no copy; `row_indices<T>()` maps them back to handles.
- **Validity Bitmaps:** `validity<T>()` produces an Arrow-style packed presence bitmap for branchless masked iteration and interchange.
- **Serialization:** opt-in `<soatable/serialize.hpp>` `save()` / `load()` snapshot trivially-copyable tables to a versioned, schema-checked byte buffer.
- **Pluggable Storage Layout:** the default `soa_table` is flat and SIMD-aligned; `aosoa_table<TileSize, ...>` opts into tiled (AoSoA-style) storage with bounded reallocation, and `column_tiles<T>()` views either as per-tile aligned spans.
- **Vectorized Compute:** opt-in `<soatable/compute.hpp>` provides `transform` / `reduce` / `inclusive_scan` / `count_if` over column spans, policy-agnostic `transform_column` / `reduce_column`, and cross-column `assign_from<Out, In...>()` (e.g. `pnl = price * qty`).

### Naming

The public API uses std-style lowercase names (`soa_table`, `row_handle`, `column_vector`, `delta_value`, `dirty_mask`). The previous PascalCase spellings (`SoaTable`, `RowHandle`, ...) remain as `[[deprecated]]` aliases for one migration window and will be removed in a future major release.

## Quick Start

```cpp
#include <soatable/soatable.hpp>
#include <print>

struct Health { float value; };
struct Name { std::string value; };
struct Poisoned {}; // Component with no data

int main() {
    soatable::soa_table<Health, Name, Poisoned> table;

    // Insertion
    auto id = table.insert();
    table.assign<Name>(id, "Warrior");
    table.assign<Health>(id, 100.0f);

    // Reference-semantic views: structured bindings yield real references (no .get()).
    for (auto [row, health, name] : table.view<Health, Name>()) {
        std::println("Row {}: {} has {} health", row.index, name.value, health.value);
    }

    // Optional columns still use select<>(), which returns reference_wrapper tuples.
    for (auto [row, name, poison] : table.select<Name, std::optional<Poisoned>>()) {
        if (poison) {
            std::println("{} is poisoned!", name.get().value);
        }
    }
}
```

## Real-World Example

This example demonstrates how to use SoaTable for a vehicle fleet management system, utilizing batch operations, sparse joins, multi-column sorting, and data compression.

```cpp
#include <soatable/soatable.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <utility>
#include <cstdint>
#include <print>

// 1. Data Compression Utilities
struct Status {
    uint8_t flags = 0;
    // packed_bits: [0-1] Engine State, [2-2] Lights, [3-3] Wiper
    using EngineState = soatable::packed_bits<uint8_t, uint8_t, 0, 2>;
    using Lights      = soatable::packed_bits<uint8_t, bool, 2, 1>;

    void set_engine(uint8_t state) { EngineState::set(flags, state); }
};

struct Position { float x, y; };

// Using quantized_float for Speed (0-200 km/h) and Fuel (0-100%)
using Speed = soatable::quantized_float<uint16_t, 0, 200000, 16>;
using Fuel  = soatable::quantized_float<uint8_t, 0, 100000, 8>;

struct Driver { std::string name; };
struct Maintenance { bool urgent; };

int main() {
    // Define the table with various column types
    soatable::soa_table<int, Position, Speed, Fuel, Status, soatable::delta_value<float>, Driver, Maintenance> fleet;

    // 2. Batch Operations: High-throughput loading
    auto ids = fleet.insert_batch(1000);
    std::vector<int> vids(1000);
    for(int i=0; i<1000; ++i) vids[i] = 1000 + i;
    fleet.assign_batch<int>(ids, vids.begin());

    // 3. Stable IDs: Keep a handle to a specific vehicle
    auto my_truck_id = ids[50];
    fleet.assign<Driver>(my_truck_id, "Alice");

    // 4. Sparse Projection: Efficiently join required and optional columns
    fleet.assign<Maintenance>(ids[10], Maintenance{true});
    fleet.assign<Maintenance>(ids[50], Maintenance{false});

    // Only iterates over rows that HAVE Maintenance. Driver is optional.
    for (auto [row, maint, driver] : fleet.select<Maintenance, std::optional<Driver>>()) {
        std::string name = driver ? driver->get().name : "Unknown";
        std::println("Vehicle ID {}: Maintenance (Urgent: {}), Driver: {}", row.index, maint.get().urgent, name);
    }

    // 5. Multi-Column Sorting: Physically reorder all columns
    // Sort by Fuel (ascending), then by Speed (descending)
    fleet.sort_by_multi(
        std::make_pair(Fuel{}, [](const Fuel& a, const Fuel& b) { return a.get() < b.get(); }),
        std::make_pair(Speed{}, [](const Speed& a, const Speed& b) { return a.get() > b.get(); })
    );

    // 6. Parallel Sorting: Reorder columns in parallel for large datasets
    fleet.sort_by_column_parallel<int>([](int a, int b) { return a < b; });

    // 7. Data Compression: DeltaValue for smooth tracking
    auto altitude_id = ids[0];
    soatable::delta_value<float> alt(100.0f);
    alt.apply_delta(alt.get_delta(105.0f));
    fleet.assign<soatable::delta_value<float>>(altitude_id, alt);

    return 0;
}
```

## Installation

SoaTable is header-only and requires a C++23 compiler and CMake 3.25+.

### CMake

Consume an installed package or add the repository as a subdirectory:

```cmake
find_package(soatable CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE soatable::soatable)
```

The repository ships `CMakePresets.json`. To build and test locally:

```sh
cmake --preset clang      # or: gcc, msvc
cmake --build --preset clang
ctest --preset clang
```

### Conan

A header-only recipe is provided at the repository root:

```sh
conan create .
```

### vcpkg

Use the overlay port under `packaging/vcpkg/ports`:

```sh
vcpkg install soatable --overlay-ports=packaging/vcpkg/ports
```

### Single-header drop-in

SoaTable is already a single header. To generate a stamped, self-contained copy:

```sh
python scripts/amalgamate.py   # writes dist/soatable.hpp
```

### Documentation

API reference is published to GitHub Pages from the `main` branch. Build it locally with:

```sh
cmake -S . -B build-docs -DSOATABLE_BUILD_DOCUMENTATION=ON
cmake --build build-docs --target docs
```

## Benchmarks

The benchmark suite covers insertion, erase-churn, single-column and parallel sort, sparse selection
across density sweeps, and the select driver heuristic, with Array-of-Structures and hand-rolled
columnar baselines. Run it (records the toolchain and emits a JSON results file):

```sh
./scripts/run_benchmarks.sh   # builds the `bench` preset and writes bench/results.json
```

Representative selective-join results (250,000 rows, Release):

| Method | Time |
|-------------------------------------|----------------|
| SoaTable `select` (smallest driver) | ~168,000 ns |
| Forced largest-column driver | ~1,547,000 ns |
| Hand-rolled columnar scan | ~1,058,000 ns |
| AoS branch scan | ~1,303,000 ns |

The ~9x gap between the smallest-driver and forced-largest-driver runs validates the heuristic of
driving iteration off the smallest required column. Numbers are machine dependent; see the harness
above to reproduce on your hardware.

## License

MIT
