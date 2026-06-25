# SoaTable

[![Build](https://github.com/bbalouki/soatable/actions/workflows/ci.yaml/badge.svg)](https://github.com/bbalouki/soatable/actions/workflows/ci.yaml)
![C++](https://img.shields.io/badge/C%2B%2B-23-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.25%2B-green.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)
![Header-only](https://img.shields.io/badge/Header--only-Yes-blue)
![Dependencies](https://img.shields.io/badge/Core%20dependencies-None-brightgreen)

**SoaTable is a header-only C++23 Structure-of-Arrays (SoA) table** for data that is row-addressed but
queried by a subset of fields. It stores each column densely and independently, hands rows stable
generational handles, and drives joins off the smallest column, so sparse, column-oriented workloads
get cache-friendly memory and fast queries without giving up a row-oriented API.

The core is a single header with **no dependencies**. Everything beyond the container, compute,
queries, serialization, allocators, concurrency, out-of-core storage, time-series, units, and runtime
schemas, lives in **opt-in headers you include only if you use them**.

## Table of Contents

- [The idea in 30 seconds](#the-idea-in-30-seconds)
- [When SoaTable is the right tool](#when-soatable-is-the-right-tool)
- [Quick start](#quick-start)
- [Feature tour ](#feature-tour-what--why--how)
  - [1. The container: sparse columns + stable handles](#1-the-container-sparse-columns--stable-handles)
  - [2. Querying: select, view, and structured bindings](#2-querying-select-view-and-structured-bindings)
  - [3. Accessing values safely](#3-accessing-values-safely)
  - [4. Sorting and batch loading](#4-sorting-and-batch-loading)
  - [5. Zero-copy spans, validity bitmaps, serialization](#5-zero-copy-spans-validity-bitmaps-serialization)
  - [6. The compute layer (the "numpy part")](#6-the-compute-layer-the-numpy-part)
  - [7. Queries and aggregation](#7-queries-and-aggregation)
  - [8. Storage policies and allocators](#8-storage-policies-and-allocators)
  - [9. Concurrency](#9-concurrency)
  - [10. Domain helpers: compression, time-series, units, dynamic schema](#10-domain-helpers-compression-time-series-units-dynamic-schema)
  - [11. Portability: freestanding and reflection](#11-portability-freestanding-and-reflection)
- [The opt-in headers at a glance](#the-opt-in-headers-at-a-glance)
- [Domain cookbooks](#domain-cookbooks)
- [Installation](#installation)
- [Benchmarks](#benchmarks)
- [Building, testing, docs](#building-testing-docs)
- [Versioning and naming](#versioning-and-naming)
- [License](#license)

## The idea in 30 seconds

Most programs start with an **Array of Structures (AoS)**:

```cpp
struct Particle { double x, y, vx, vy; std::string name; };
std::vector<Particle> particles;
```

That is great until your access pattern stops matching your layout. AoS hurts when:

- **You touch only a few fields at a time.** Iterating `vx`/`vy` still pulls `name` into every cache
  line.
- **Rows are sparse.** Many rows lack many fields, so you waste memory or reach for pointers and
  `std::optional` everywhere.
- **Identity must be stable.** Erasing from a vector invalidates indices or forces shifting.

SoaTable stores a **Structure of Arrays** under the hood, each column is its own dense array, but
keeps a row-oriented API for insertion and handles. Columns are **independent and sparse**: a column
only stores the rows that actually have it, so you pay only for what each row uses. Queries iterate the
**smallest** requested column and check the rest, which is where the speed comes from.

## When SoaTable is the right tool

**Reach for it when:**

- **Entity-Component-System / game worlds**, entities with sparse components, stable handles across
  frames, and systems that iterate a few components at a time.
- **Finance**, tick and portfolio tables, columnar analytics, group-by aggregation, compact storage,
  and fast snapshots.
- **Engineering / aerospace telemetry**, frame stores with optional channels, smoothly varying signals
  (`delta_value`), dirty-region recompute, dimensional safety, and a no-exceptions build for flight
  software.
- **Scientific / data workloads**, hand a column straight to a SIMD kernel or BLAS through a zero-copy
  span, run vectorized ufuncs, or memory-map columns that exceed RAM.

**Prefer a plain `std::vector<Struct>` when:** the table is tiny, rows are always dense, and every pass
touches every field. SoaTable's win grows with size, sparsity, and how selective your queries are.

## Quick start

```cpp
#include <soatable/soatable.hpp>
#include <print>

struct Health { float value; };
struct Name   { std::string value; };
struct Poisoned {};                 // A data-less tag component.

int main() {
    soatable::soa_table<Health, Name, Poisoned> table;

    const auto id = table.insert();  // Stable handle, valid until erased.
    table.assign<Name>(id, "Warrior");
    table.assign<Health>(id, 100.0f);

    // Reference-semantic views: structured bindings yield real references (no .get()).
    for (auto [row, health, name] : table.view<Health, Name>()) {
        health.value -= 10.0f;       // Writes straight through to the column.
        std::println("{} has {} health", name.value, health.value);
    }

    // Optional columns: select<>() includes rows that may or may not have Poisoned.
    for (auto [row, name, poison] : table.select<Name, std::optional<Poisoned>>()) {
        if (poison) std::println("{} is poisoned!", name.get().value);
    }
}
```

## Feature tour

### 1. The container: sparse columns + stable handles

**What.** `soa_table<Columns...>` registers a fixed set of column types. `insert()` returns a `row_id`;
`assign<T>` / `unassign<T>` add or drop a column on a row; `erase` removes a row.

**Why.** Columns are stored densely and independently, so a column only allocates for rows that have it
(true sparsity), and iterating one column is cache-friendly. `row_id` is a generational handle
(`index` + `generation`), so a stale handle to an erased-then-reused slot is detected rather than
silently aliasing a new row (the ABA problem).

**How.**

```cpp
soatable::soa_table<Name, Age> t;
auto id = t.insert();
t.assign<Age>(id, 30);
t.erase(id);
assert(!t.is_valid(id));            // Generation bumped; the handle is now invalid.
```

### 2. Querying: select, view, and structured bindings

**What.** `select<Cols...>()` and `view<Cols...>()` iterate the rows that have all requested columns.
Use `std::optional<T>` in `select<>()` to include a column when present without requiring it.

**Why.** Both drive iteration off the **smallest** required column and check the rest via a per-row
signature bitset, so a selective join scans far fewer rows than a full table scan. `view<>()` yields a
`row_view` proxy that models the tuple protocol, so structured bindings give **real references** with no
`.get()`; `select<>()` is the lower-level form (and the one that supports optional columns).

**How.**

```cpp
for (auto [id, pos, vel] : t.view<Position, Velocity>()) pos.x += vel.x;     // real references
for (auto [id, name, age] : t.select<Name, std::optional<Age>>()) { /* age may be empty */ }
```

### 3. Accessing values safely

**What.** Three accessor styles, so you pick your failure mode:

| Accessor              | On missing column                        | Use when                         |
| --------------------- | ---------------------------------------- | -------------------------------- |
| `get<T>(id)`          | throws `std::out_of_range`               | you expect it to be there        |
| `try_get<T>(id)`      | returns `nullptr`                        | you want a cheap presence check  |
| `get_expected<T>(id)` | returns `std::expected<…, access_error>` | hot paths / no-exceptions builds |

**Why.** Predictable error handling matters for finance and aerospace code that avoids exceptions on hot
paths. `const` accessors yield `const` references end-to-end so a table is safely shareable read-only.

### 4. Sorting and batch loading

**What.** `sort_by_column<T>(cmp)`, `sort_by_multi(...)` (primary/secondary keys), and
`sort_by_column_parallel<T>(cmp)` physically reorder every column. `insert_batch(n)` / `assign_batch<T>`
load many rows at once.

**Why.** Physical reordering keeps a column contiguous in a meaningful order (e.g. by timestamp) so later
spans and rolling windows are correct. The parallel sort reorders columns concurrently above a size
threshold and runs serially below it, where task-launch overhead would dominate.

### 5. Zero-copy spans, validity bitmaps, serialization

**What.** `column<T>()` returns a `std::span` over a column's dense values; `row_indices<T>()` maps each
back to its row. `validity<T>()` returns a packed, Arrow-style presence `bitmap`. Opt-in
`<soatable/serialize.hpp>` `save()`/`load()` snapshot a trivially-copyable table to a versioned,
schema-checked byte buffer.

**Why.** Spans are the keystone: hand a column straight to BLAS, an FFT, a SIMD kernel, or (later) numpy
with **no copy**. Validity bitmaps enable branchless masked iteration and are the bridge to Arrow.
Serialization gives simulation checkpoints and tick-store snapshots that round-trip handles and column
order.

**How.**

```cpp
std::span<Price> prices = t.column<Price>();        // contiguous, 64-byte aligned
auto bytes = soatable::save(t);                     // #include <soatable/serialize.hpp>
soatable::soa_table<...> restored;
soatable::load(restored, bytes);
```

### 6. The compute layer

**What.** Opt-in `<soatable/compute.hpp>`:

- single-column ufuncs over spans, `transform`, `reduce`, `inclusive_scan`, `count_if`;
- policy-agnostic column helpers, `transform_column`, `reduce_column`, `count_column_if`;
- broadcasting and subsets, `add_scalar`, `transform_if`, `transform_masked`, `transform_strided`;
- cross-column row-wise compute, `assign_from<Out, In...>()` (e.g. `pnl = price * qty`);
- chunk-parallel, `transform_column_parallel`, `for_each_chunk`.

**Why.** Express a vectorized operation once and let the compiler auto-vectorize over the 64-byte-aligned
storage. Cross-column math goes through the `select`/`view` join because independent sparse columns are
not row-aligned in storage, so `assign_from` only touches rows that have all inputs.

**How.**

```cpp
soatable::compute::assign_from<Pnl, Price, Qty>(t,
    [](const Price& p, const Qty& q) { return Pnl{p.value * q.value}; });
double gross = soatable::compute::reduce_column<Pnl>(t, 0.0,
    [](double acc, const Pnl& p) { return acc + p.value; });
```

### 7. Queries and aggregation

**What.** Opt-in `<soatable/query.hpp>`: `select_where<Cols...>(t, predicate)` filters the join by a row
predicate; `group_reduce` / `group_sum` / `group_count` aggregate by a projected key.

**Why.** Analytics ergonomics, `where(...).select(...)` and group-by, without leaving C++ and without a
query engine. Filtering composes lazily on top of the smallest-driver scan.

**How.**

```cpp
auto volume = soatable::query::group_sum<Symbol, Notional>(t,
    [](const Symbol& s){ return s.value; }, [](const Notional& n){ return n.value; });
```

### 8. Storage policies and allocators

**What.** The layout is a policy on the table type, with the row API unchanged:

- `soa_table<...>`, flat, contiguous, 64-byte SIMD-aligned (default).
- `aosoa_table<Tile, ...>` / `chunked_soa_table<Chunk, ...>`, tiled (AoSoA / Arrow record-batch)
  storage; growth never copies existing chunks. `column_tiles<T>()` views either layout as per-tile
  aligned spans.
- `custom_soa_table<Allocator, ...>`, any allocator; opt-in `<soatable/pmr.hpp>` `pmr_soa_table` uses a
  `std::pmr` arena / monotonic / pool resource.
- opt-in `<soatable/mmap.hpp>` `mmap_soa_table`, demand-paged memory-mapped storage so a column can
  exceed physical RAM, paged in lazily by the OS.

**Why.** Match storage to the workload without rewriting your code: SIMD-aligned contiguous for analytics,
tiled for bounded reallocation and streaming, arena/pmr for deterministic real-time allocation, mmap for
out-of-core datasets.

### 9. Concurrency

**What.** Opt-in `<soatable/concurrent.hpp>` `synchronized_table<Table>` wraps a table behind a
`std::shared_mutex` with scoped `read()` / `write()` callbacks.

**Why.** Many strategy threads can read a market-data table while one thread ingests, with the lock held
for the duration of each callback so access is always synchronized.

```cpp
soatable::synchronized_table<Table> shared;
shared.write([](Table& t){ t.insert(); });
auto n = shared.read([](const Table& t){ return t.size(); });   // many readers concurrently
```

### 10. Domain helpers: compression, time-series, units, dynamic schema

- **Compact storage** (core): `quantized_float` (lossy fixed-range floats in N bits), `packed_bits`
  (bit-fields in an integer), `delta_value` (track a value via scaled deltas), `dirty_mask` (flag set).
- **Time-series** (`<soatable/timeseries.hpp>`): `rolling_sum` / `rolling_mean` / `deltas`,
  `rolling_mean_column`, and `for_each_dirty` for incremental recompute of changed rows.
- **Dimensional safety** (`<soatable/units.hpp>`): `quantity<T, Dimension>` makes adding metres to seconds
  a **compile error** while multiply/divide combine dimensions, usable directly as a column type.
- **Runtime schema evolution** (`<soatable/dynamic.hpp>`): `dynamic_table` adds/removes typed columns at
  runtime, type-checks access without RTTI, and carries per-column metadata (e.g. unit labels).

### 11. Portability: freestanding and reflection

- **Freestanding / no-exceptions:** define `SOATABLE_NO_EXCEPTIONS` to redirect every internal `throw` to
  a terminating handler and rely on `get_expected<T>()` / `try_get<T>()`; the library uses no RTTI. A CI
  leg builds this mode with `-fno-exceptions` for embedded / flight / kernel targets.
- **C++26 reflection path** (`<soatable/reflect.hpp>`): `table_for<Struct>` builds a table from a struct.
  Today you write a one-line `column_list` specialization; the `SOATABLE_HAS_REFLECTION`-gated automatic
  path activates when a toolchain ships P2996 reflection.

## The opt-in headers at a glance

| Header                    | Provides                                            | Pulls in                       |
| ------------------------- | --------------------------------------------------- | ------------------------------ |
| `soatable/soatable.hpp`   | the container, spans, validity, compression helpers | nothing extra                  |
| `soatable/compute.hpp`    | ufuncs, broadcasting, chunk-parallel compute        | `<future>`, `<thread>`         |
| `soatable/query.hpp`      | `select_where`, group-by aggregation                | `<unordered_map>`              |
| `soatable/serialize.hpp`  | versioned `save()` / `load()`                       |,                              |
| `soatable/pmr.hpp`        | `pmr_soa_table`                                     | `<memory_resource>`            |
| `soatable/mmap.hpp`       | `mmap_soa_table` (out-of-core)                      | `<sys/mman.h>` / `<windows.h>` |
| `soatable/concurrent.hpp` | `synchronized_table`                                | `<shared_mutex>`               |
| `soatable/timeseries.hpp` | rolling windows, deltas, dirty scans                |,                              |
| `soatable/units.hpp`      | dimensional `quantity`                              |,                              |
| `soatable/dynamic.hpp`    | runtime `dynamic_table`                             | `<unordered_map>`              |
| `soatable/reflect.hpp`    | `table_for<Struct>`                                 |,                              |

## Domain cookbooks

Runnable, worked examples (built as `soatable_cookbook_*` targets):

- [Finance, tick store](example/cookbook/finance_tick_store.cpp): ingest trades, derive P&L per row,
  aggregate notional volume per symbol, filter large trades.
- [Game / ECS](example/cookbook/game_ecs.cpp): a sparse-component world with a motion system over
  `view<Position, Velocity>()` and a data-less tag.
- [Aerospace, telemetry](example/cookbook/aerospace_telemetry.cpp): `delta_value` altitude tracking,
  `dirty_mask` per-frame flags, and a validity bitmap.
- [Scientific data](example/cookbook/scientific_table.cpp): zero-copy span compute and `quantized_float`
  storage.

A broader [feature tour](example/soatable_example.cpp) exercises most headers in one program.

## Installation

SoaTable is header-only and requires a C++23 compiler (GCC 13+, Clang 18+, MSVC) and CMake 3.25+.

### CMake

```cmake
find_package(soatable CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE soatable::soatable)
```

### Conan / vcpkg

```sh
conan create .                                                   # header-only recipe at the repo root
vcpkg install soatable --overlay-ports=packaging/vcpkg/ports     # overlay port
```

### Single-header drop-in

The core `soatable.hpp` is self-contained. To emit a stamped copy for vendoring:

```sh
python scripts/amalgamate.py            # writes dist/soatable.hpp
```

The opt-in add-on headers (`compute`, `query`, ...) are intentionally separate so you pay only for what
you include; pass `--with compute,query` to bundle specific ones next to the core.

## Benchmarks

The suite covers insertion, erase-churn, single-column and parallel sort, sparse selection across
density sweeps, the select driver heuristic, SoA-vs-AoSoA selection, and compute throughput, with
Array-of-Structures and hand-rolled columnar baselines:

```sh
./scripts/run_benchmarks.sh             # builds the `bench` preset, writes bench/results.json
```

Representative selective-join results (250,000 rows, Release):

| Method                              | Time          |
| ----------------------------------- | ------------- |
| SoaTable `select` (smallest driver) | ~168,000 ns   |
| Forced largest-column driver        | ~1,547,000 ns |
| Hand-rolled columnar scan           | ~1,058,000 ns |
| AoS branch scan                     | ~1,303,000 ns |

The ~9x gap between the smallest-driver and forced-largest-driver runs validates the heuristic of driving
iteration off the smallest required column. Numbers are machine dependent; reproduce with the harness
above.

## Building, testing, docs

```sh
cmake --preset clang        # or: gcc, msvc, asan-ubsan, bench
cmake --build --preset clang
ctest --preset clang
```

CI builds GCC 13 / Clang 18 / MSVC plus an AddressSanitizer + UndefinedBehaviorSanitizer leg, all through
these presets. Build the Doxygen API site locally with:

```sh
cmake -S . -B build-docs -DSOATABLE_BUILD_DOCUMENTATION=ON
cmake --build build-docs --target docs
```

## Versioning and naming

SoaTable follows [Semantic Versioning](https://semver.org). The public API uses std-style lowercase names
(`soa_table`, `row_handle`, `column_vector`, `delta_value`, `dirty_mask`); the previous PascalCase
spellings remain as `[[deprecated]]` aliases for one migration window. See [CHANGELOG.md](CHANGELOG.md)
for the history and [CONTRIBUTING.md](CONTRIBUTING.md) to get started.

## License

MIT, see [LICENSE](LICENSE).
