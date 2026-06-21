# SoaTable

SoaTable is a header-only C++20 sparse column table for data that is naturally
row-addressed but frequently queried by a subset of fields.

It combines three ideas:

- stable generational row handles
- optional sparse columns
- fast joins over only the columns you care about

The library is exposed through the public alias `soatable`, while the original
implementation namespace `sstd` remains available for compatibility.

---

## Why It Exists

Most programs start with an Array of Structures layout:

```cpp
struct Particle {
  double x;
  double y;
  double vx;
  double vy;
  std::string name;
};
```

That is simple, but it becomes expensive when most operations only touch a few
fields. Every scan drags unrelated members into cache lines, and optional fields
usually turn into pointer chasing, null checks, or separate side maps.

SoaTable is for the opposite shape of problem:

- you need stable row identities
- different rows populate different subsets of columns
- the same workload repeatedly joins a few columns together
- iteration speed matters more than object-style convenience

This is common in:

- ECS/game state
- telemetry and metrics pipelines
- simulation data
- ledgers and transaction stores
- feature tables for analytics

If your data is tiny, fixed, and always accessed as one full object, plain AoS is
still the better choice. SoaTable is valuable when the schema is sparse and the
query pattern is selective.

---

## Highlights

- Stable generational `row_id` handles prevent stale references and the ABA
  problem.
- Optional columns are stored sparsely, so you only pay for the fields a row
  actually has.
- `select<...>()` automatically drives iteration from the smallest requested
  column, which keeps sparse joins efficient.
- `rows()` and `for_each_row()` provide generic row traversal when you want to
  write your own algorithms.
- `sort_by_column()` and `sort_by_column_parallel()` physically reorder the
  columns using one permutation.
- Utility types help with compact data representation:
  - `quantized_float`
  - `packed_bits`
  - `delta_value`
  - `dirty_mask`

---

## Install

SoaTable is header-only.

### CMake

```cmake
find_package(soatable CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE soatable::soatable)
```

Or add the repository directly as a subdirectory:

```cmake
add_subdirectory(path/to/SoaTable)
target_link_libraries(your_target PRIVATE soatable::soatable)
```

### vcpkg

The repository includes a `vcpkg.json` manifest and install/export-ready CMake
files, so it is ready to be turned into a vcpkg port.

---

## Quick Start

```cpp
#include <cstdint>
#include <iostream>
#include <soatable/soatable.hpp>

struct Price {
  double value = 0.0;
};

struct Volume {
  std::uint64_t count = 0;
};

struct Symbol {
  std::string value;
};

using MarketTable = soatable::soa_table<Price, Volume, Symbol>;

int main() {
  MarketTable table;
  table.reserve(1024);

  auto row = table.insert();
  table.assign<Price>(row, 182.50);
  table.assign<Volume>(row, 52000000ULL);
  table.assign<Symbol>(row, "AAPL");

  for (auto [id, price, volume, symbol] : table.select<Price, Volume, Symbol>()) {
    std::cout << id.index << ": " << symbol.value << " " << price.value
              << " vol=" << volume.count << '\n';
  }
}
```

### Row Handles

`row_handle` is the ergonomic wrapper for code that wants to keep a stable
reference to a row.

```cpp
soatable::row_handle<Price, Volume, Symbol> handle{table.insert(), table};

handle.assign<Symbol>("MSFT");
if (handle.contains<Price>()) {
  std::cout << handle.get<Price>().value << '\n';
}

handle.erase();
```

### Querying

`select<...>()` returns a C++20 range that yields `row_id` plus references to the
requested columns.

```cpp
for (auto [id, price, volume] : table.select<Price, Volume>()) {
  price.value += 1.0;
  volume.count += 1000;
}
```

### Sorting

```cpp
table.sort_by<Price>([](const Price& lhs, const Price& rhs) {
  return lhs.value < rhs.value;
});
```

`sort_by_column_parallel()` applies the same permutation across every column in
parallel, which is useful when the table is wide enough for the extra threading
overhead to pay off.

---

## API Notes

- `insert()` returns a stable `row_id`.
- `erase(row_id)` invalidates the handle and reuses the slot later.
- `assign<T>()` adds or replaces a sparse column value on a row.
- `try_get<T>()` is the non-throwing accessor.
- `get<T>()` throws if the row or column is not available.
- `rows()` iterates over every live row.
- `for_each_row()` is a convenience wrapper for custom algorithms.

The utility helpers are intentionally generic:

- `quantized_float` compresses a floating-point range into a compact integer
  representation.
- `packed_bits` packs multiple bit-fields into one integer.
- `delta_value` tracks a value relative to its current baseline.
- `dirty_mask` keeps track of changed flags efficiently.

---

## When To Use It

Use SoaTable when you need one or more of the following:

- stable IDs for long-lived rows
- optional components or fields
- sparse joins across a few columns
- cache-friendly scans over large tables
- physical reordering of aligned columnar data

In practice, SoaTable is especially strong when:

- only a small fraction of rows contain the queried columns
- you repeatedly iterate over the same subset of fields
- you want to avoid null checks and pointer indirection in your hot path

---

## When Not To Use It

Choose something simpler if:

- every record always has every field
- you mostly need random access to one full object at a time
- the dataset is tiny and performance is not a concern
- your schema changes constantly and does not benefit from stable handles

SoaTable is a performance-oriented data layout, not a replacement for every
container.

---

## Benchmark

The benchmark under `benchmarks/soatable_benchmark.cpp` compares a sparse
join in SoaTable against a straightforward AoS branch scan.

Test setup:

- 250,000 rows
- 12 iterations
- Release build on Windows with MSVC 19.44
- Query: `Temperature + Pressure + RegionId`
- Sparse presence:
  - `Temperature`: 70%
  - `Pressure`: 40%
  - `RegionId`: 5%

Local result:

| Query           | Time / iteration | Relative speed |
| --------------- | ---------------: | -------------: |
| SoaTable select |       279.583 us |    4.9x faster |
| AoS branch scan |       1373.58 us |       baseline |

The checksum matched exactly, so both loops computed the same result.

What this shows:

- SoaTable wins when the workload is sparse and join-heavy.
- The driver-column optimization keeps the scan focused on the smallest useful
  subset of rows.
- The cost of extra structure is repaid when you avoid scanning the full AoS
  table and branching on every row.

This benchmark is not a claim that SoaTable beats every container for every
workload. It shows where this layout is the right tool.

---

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
```

Example binaries are enabled by default. To disable them:

```bash
cmake -S . -B build -DSOATABLE_BUILD_EXAMPLES=OFF -DSOATABLE_BUILD_BENCHMARKS=OFF
```

---

## Project Layout

- `include/soatable/soatable.hpp` - public header
- `example/soatable_example.cpp` - usage sample
- `benchmarks/soatable_benchmark.cpp` - sparse join benchmark
- `CMakeLists.txt` - package/export setup
- `vcpkg.json` - vcpkg-ready manifest

---

## License

MIT
