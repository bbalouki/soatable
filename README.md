# SoaTable

A header-only, general-purpose C++20 columnar database table and data-frame library. It combines the cache-locality benefits of a **Structure of Arrays (SoA)** layout with the relational flexibility of optional columns and stable addressing, making it suitable for fields such as finance, physical simulations, telemetry tracking, and game engines .

Rather than arranging data as an Array of Structures (AoS)—which often pulls unnecessary fields into the CPU cache during iteration—`SoaTable` packs matching attributes into contiguous memory pools (`column_vector`) . It uses sparse-set mapping to allow columns to be optional, sparse, and easily joinable in $O(1)$ time.

---

## Key Technical Features

- **Stable Generational Handles (`row_id`):** Systematically mitigates index invalidation and the ABA problem . Rows are referenced via a generational coordinate `{index, generation}` which safely flags stale handles after deletion.
- **Implicit Free List Reclamation:** Deleted rows are marked as inactive and immediately repurposed inside an inline free list. This allows $O(1)$ allocations and recycling without requiring supplementary heap allocation tracking.
- **Sparse Column Vector Pools:** Homogeneous column data is packed contiguously in memory . Deletion uses a transactional **Swap-and-Pop** mechanic, keeping storage dense to maximize CPU L1/L2 cache prefetching efficiency.
- **Smallest-Set Driver View Joins (`select<...>()`):** Automatically evaluates the active sizes of all requested columns at runtime, selecting the smallest dataset to drive query iteration . This reduces lookup overhead in wide, sparsely populated schemas.
- **Zero-Overhead Bitmask Filtering:** Instead of executing branched logical evaluations, the query system resolves matching records via compile-time calculated bitmasks and fast bitwise evaluations.
- **Lossless/Lossy Data Compression Tools:** Built-in domain-agnostic helpers for `quantized_float` mapping, bitwise packing (`packed_bits`), relative tracking (`delta_value`), and state change monitoring (`dirty_mask`).
- **Asynchronous Physical Permutation Sorting:** Supports sorting the table's state. Columns can be rearranged sequentially or concurrently across thread boundaries using asynchronous tasks.

---

## Complexity Reference

| Operation                               | Time Complexity          | Space Complexity | Cache Efficiency                               |
| :-------------------------------------- | :----------------------- | :--------------- | :--------------------------------------------- |
| **Row Insertion** (`insert`)            | $O(1)$                   | $O(1)$           | High (contiguously appended metadata)          |
| **Row Deletion** (`erase`)              | $O(1)$                   | $O(1)$           | High (recycles indices via free list)          |
| **Column Assignment** (`assign`)        | $O(1)$                   | $O(1)$           | High (appends contiguously)                    |
| **Column Retrieval** (`get`)            | $O(1)$                   | $O(1)$           | High (dense array lookup)                      |
| **Column Deletion** (`unassign`)        | $O(1)$                   | $O(1)$           | High (swap-and-pop compression)                |
| **Range View Joins** (`select`)         | $O(N_{\text{smallest}})$ | $O(1)$           | Optimal (iterates through packed driver array) |
| **Permutation Sort** (`sort_by_column`) | $O(N \log N)$            | $O(N)$           | Temporary buffering required during swap       |

---

## Installation

`SoaTable` is a header-only library with no external dependencies beyond a modern C++20 standard library.

Simply copy `soatable.hpp` into your project's include directory:

```bash
cp path/to/soatable.hpp my_project/include/
```

Configure your compiler to compile with the **C++20 standard** or newer (e.g., `-std=c++20` or `-std=c++23`).

---

## Basic Usage & API Guide

### 1. Defining Schemas and Instantiating Tables

To avoid type collision in variadic parameter packs, all column types in an `soa_table` must be unique. The recommended pattern is to define lightweight wrapper structs:

```cpp
#include "soatable.hpp"

// Define distinct attributes/columns
struct SecurityID { std::string ticker; };
struct AskPrice   { double value; };
struct BidPrice   { double value; };
struct DailyVolume { uint64_t count; };

// Instantiate the table with the target schema
using MarketTable = sstd::soa_table<SecurityID, AskPrice, BidPrice, DailyVolume>;
MarketTable table;

// Reserve memory to prevent reallocations during time-critical operations
table.reserve(10000);
```

### 2. Allocating Rows and Managing Data Lifecycles

Rows are allocated via an `insert` command and managed with `row_handle` proxies or direct queries.

```cpp
// Insert a row and obtain a handle
sstd::row_handle<SecurityID, AskPrice, BidPrice, DailyVolume> handle;
handle.id = table.insert();
handle.table = &table;

// Assign fields
handle.assign<SecurityID>("AAPL");
handle.assign<AskPrice>(182.50);
handle.assign<BidPrice>(182.45);
handle.assign<DailyVolume>(52000000ULL);

// Safe query checking
if (handle.contains<AskPrice>()) {
    double ask = handle.get<AskPrice>().value;
}

// Safely erasing a row
handle.erase(); // Frees the slot, invalidating all outstanding row_id copies
```

### 3. Executing SQL-Style Range Joins

The `select` function filters out irrelevant rows using bitwise signature testing and constructs a zero-overhead C++20 range view over the matched records.

```cpp
// Iterates only over rows that contain both AskPrice and DailyVolume
for (auto [id, ask, vol] : table.select<AskPrice, DailyVolume>()) {
    std::cout << "Row " << id.index << ": Ask $" << ask.value << " Vol: " << vol.count << "\n";

    // Values are structured as modifiable references
    ask.value += 0.05;
}
```

### 4. Physically Sorting the Columns

You can sort the table’s state based on a criteria from any column. The library applies permutations across all registered columns to maintain row alignment.

```cpp
// Sort sequentially
table.sort_by_column<AskPrice>([](const AskPrice& a, const AskPrice& b) {
    return a.value < b.value; // Sort ascending by ask price
});

// Or sort concurrently using multi-threaded permutations
table.sort_by_column_parallel<DailyVolume>([](const DailyVolume& a, const DailyVolume& b) {
    return a.count > b.count; // Sort descending by volume
});
```

---

## In-Depth Architecture Details

### Preventing Stale References (The ABA Problem)

When a row is erased, its index is queued for reuse in subsequent insertions . If an external system retains a raw index (e.g., `5`), it might access a newly inserted, unrelated record .

`SoaTable` resolves this by assigning a `row_id` containing a `generation` tracker .

```
Step 1: Row 5 created -> [Index: 5, Gen: 0] (Active)
Step 2: Row 5 erased  -> [Index: 5, Gen: 1] (Inactive, linked to free list)
Step 3: New insertion -> Slot 5 recycled. Handle is now [Index: 5, Gen: 1] (Active)

Result: An old handle pointing to [Index: 5, Gen: 0] is rejected as stale .
```

### Cache-Pack Compression (Swap-and-Pop)

When a column is unassigned or a row is deleted, sparse set structures use a **Swap-and-Pop** mechanism to shift data in the physical vectors . The last element in the active column array replaces the erased element. This maintains contiguity, preventing sparse gaps from causing memory fragmentation or cache-prefetch misses .

---

## Utility Tools

The library includes auxiliary compression tools designed to limit memory footprint and optimize network serialization:

### 1. `quantized_float`

Compresses floating-point intervals into arbitrary smaller integers (e.g., 8-bit or 16-bit) based on a specified range:

```cpp
// Compresses values between 0.0 and 100.0 into a single byte (uint8_t)
sstd::quantized_float<uint8_t, 0, 10000, 8> score = 85.5f;

double value = score.get(); // Restores approximate value
```

### 2. `delta_value`

Helps reduce network bandwidth usage for values that change incrementally over time by tracking differences relative to an initial anchor:

```cpp
sstd::delta_value<double, int16_t, 100> sensor_reading = 45.12;

int16_t delta = sensor_reading.get_delta(45.24); // Returns the encoded step
sensor_reading.apply_delta(delta);               // Updates state
```

---

## Best Practices & Guidelines

1. **Keep Types Unique:** Since `soa_table` indexes internal arrays by type, every column must be structurally unique. Use tag structs or unique type aliases to distinguish fields of the same underlying primitive type (e.g., wrap distinct `double` variables in separate structs).
2. **Pre-Reserve Storage:** Avoid vector reallocation spikes during performance-critical routines by calling `table.reserve()` with an estimated capacity .
3. **Avoid Persistent Raw References:** Do not store raw pointers to components across updates. Because `SoaTable` uses a **swap-and-pop** model for deletion, physical elements may shift inside their vectors. Always use `row_id` handles to safely query and modify data .
