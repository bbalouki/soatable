# SoaTable

SoaTable is a high-performance, header-only C++23 library providing a sparse column-oriented data structure. It is designed for workloads where data is naturally row-addressed but frequently queried by a subset of fields, offering significant performance gains through cache-friendly memory layout and efficient joins.

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
- **Data Compression Utilities:** Includes `quantized_float`, `packed_bits`, and `DeltaValue` for compact storage.

## Quick Start

```cpp
#include <soatable/soatable.hpp>
#include <print>

struct Health { float value; };
struct Name { std::string value; };
struct Poisoned {}; // Component with no data

int main() {
    soatable::SoaTable<Health, Name, Poisoned> table;

    // Insertion
    auto id = table.insert();
    table.assign<Name>(id, "Warrior");
    table.assign<Health>(id, 100.0f);

    // Efficient Selection
    for (auto [row, health, name] : table.select<Health, Name>()) {
        std::println("Row {}: {} has {} health", row.index, name.get().value, health.get().value);
    }

    // Optional columns
    for (auto [row, name, poison] : table.select<Name, std::optional<Poisoned>>()) {
        if (poison) {
            std::println("{} is poisoned!", name.get().value);
        }
    }
}
```

## Installation

### CMake

Add the repository as a subdirectory or install it:

```cmake
find_package(soatable CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE soatable::soatable)
```

### vcpkg

SoaTable is designed to be easily integrated into vcpkg. You can add it to your `vcpkg.json` once published or use it as an overlay.

## Benchmarks

Benchmark results (250,000 rows, selective join):
| Method | Time |
|--------|------|
| SoaTable `select` | ~162,000 ns |
| AoS Branch Scan | ~2,035,000 ns |
_Run on 4x 2300 MHz CPU, Release Build._

## License

MIT
