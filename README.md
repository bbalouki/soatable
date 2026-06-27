# SoaTable

[![Build](https://github.com/bbalouki/soatable/actions/workflows/ci.yaml/badge.svg)](https://github.com/bbalouki/soatable/actions/workflows/ci.yaml)
![C++](https://img.shields.io/badge/C%2B%2B-23-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.25%2B-green.svg)
![License](https://img.shields.io/badge/License-MIT-yellow.svg)
![Header-only](https://img.shields.io/badge/Header--only-Yes-blue)
![Dependencies](https://img.shields.io/badge/Core%20dependencies-None-brightgreen)

**SoaTable is a header-only C++23 library for storing tables of data so that the computer can read
them as fast as physically possible.** 

You still think in terms of rows, "here is one trade", "here
is one spaceship", "here is one game character", but underneath, SoaTable stores the data in the
shape the hardware actually prefers. The result is a familiar, row-shaped way of working that runs
dramatically faster on the kinds of questions real programs ask.

This document is written to be read start to finish, by anyone. You do not need to open a single
source file to understand what SoaTable does, why it is built the way it is, or how to use every one
of its features. Where code appears, the paragraph above it already explains the idea in plain
words, so you can read the prose and skip the code if you like.

## Table of contents

- [Start here: the problem, in plain words](#start-here-the-problem-in-plain-words)
- [The mental model](#the-mental-model)
- [The philosophy](#the-philosophy)
- [Is SoaTable the right tool for you?](#is-soatable-the-right-tool-for-you)
- [Quick start](#quick-start)
- [The guided tour](#the-guided-tour)
  - [1. The table: rows, handles, and sparse columns](#1-the-table-rows-handles-and-sparse-columns)
  - [2. Reading values safely](#2-reading-values-safely)
  - [3. Asking questions: views and selects](#3-asking-questions-views-and-selects)
  - [4. A friendlier handle: `row_handle`](#4-a-friendlier-handle-row_handle)
  - [5. Sorting and bulk loading](#5-sorting-and-bulk-loading)
  - [6. Zero-copy columns and validity bitmaps](#6-zero-copy-columns-and-validity-bitmaps)
  - [7. Compact storage helpers](#7-compact-storage-helpers)
  - [8. The compute layer: math over columns](#8-the-compute-layer-math-over-columns)
  - [9. Queries and group-by](#9-queries-and-group-by)
  - [10. Choosing how memory is laid out](#10-choosing-how-memory-is-laid-out)
  - [11. Sharing a table across threads](#11-sharing-a-table-across-threads)
  - [12. Time-series helpers](#12-time-series-helpers)
  - [13. Units that catch mistakes at compile time](#13-units-that-catch-mistakes-at-compile-time)
  - [14. Runtime schemas: the dynamic table](#14-runtime-schemas-the-dynamic-table)
  - [15. Saving and loading](#15-saving-and-loading)
  - [16. Building a table from a struct](#16-building-a-table-from-a-struct)
  - [17. Running without exceptions](#17-running-without-exceptions)
- [The opt-in headers at a glance](#the-opt-in-headers-at-a-glance)
- [Domain cookbooks](#domain-cookbooks)
- [Installation](#installation)
- [Benchmarks: what the speed actually is](#benchmarks-what-the-speed-actually-is)
- [Building, testing, and docs](#building-testing-and-docs)
- [Versioning and naming](#versioning-and-naming)
- [Contributing](#contributing)
- [License](#license)

## Start here: the problem, in plain words

Picture a filing cabinet where every drawer holds folders, and each folder is one record, one
person, one trade, one sensor reading. To find the average age of everyone in the cabinet, you have
to pull out every single folder, flip past the name, the address, the phone number, and finally read
the one line you care about. You touched everything to use almost nothing.

Computers have exactly this problem, and it is the single biggest reason programs run slower than
they should. When a program reads even one number from memory, the hardware does not fetch just that
number, it fetches a whole **cache line**, a fixed-size chunk of nearby bytes, on the bet that you
will want the neighbours too. If your records are stored as whole folders sitting next to each other
(the normal way, called **Array of Structures**, or AoS), then reading one field drags all the
other fields of that record along for the ride, wasting most of every fetch.

```cpp
// The normal layout: each record is a folder; folders sit back to back.
struct Particle { double x, y, vx, vy; std::string name; };
std::vector<Particle> particles;   // x,y,vx,vy,name | x,y,vx,vy,name | ...
```

SoaTable turns the cabinet on its side. Instead of one drawer per record, it keeps **one drawer per
field**: all the names together in one drawer, all the ages together in another, all the prices in a
third. This is called a **Structure of Arrays**, or SoA. Now "average age" reads a single tight
drawer end to end, and every byte the hardware fetches is one you actually wanted.

```text
  Array of Structures (folders)        Structure of Arrays (SoaTable)
  -----------------------------        ------------------------------
  [name age city][name age city] ...   names:  [name][name][name] ...
  [name age city][name age city] ...   ages:   [age ][age ][age ] ...
                                        cities: [city][city][city] ...
```

That one change is worth a lot, but SoaTable goes further with two refinements that matter in real
programs:

- **The drawers are independent and sparse.** Not every record has every field. A free game
  character might have a position but no weapon; a trade might have a price but no settlement note.
  In SoaTable, a field's drawer only stores the records that actually have that field, so you never
  pay memory for the blanks.
- **Every record keeps a stable ticket.** When you add a record you get back a small, permanent
  **handle**. You can delete records, add new ones, and re-sort the whole table, and your handle
  still points at the right record, or correctly tells you it is gone. No dangling indices, no
  surprises.

## The mental model

If you remember four ideas, you understand SoaTable:

1. **A column is a field's drawer.** You declare your columns up front as small types
   (`struct Price { double value; };`). Each becomes its own dense, tightly packed array.
2. **A row is a record, reached by a handle.** `insert()` creates an empty record and hands you a
   `row_id`. You then `assign` whatever fields that record happens to have.
3. **Columns are optional per row.** A record carries only the fields you gave it. Asking for a
   field a record does not have is a well-defined "not present", never a crash or garbage.
4. **Questions start from the smallest drawer.** When you ask for "every record that has both a
   price and a quantity", SoaTable looks at the smaller of the two drawers and checks each of those
   against the other. Starting small means it examines far fewer records, which is where most of the
   speed comes from.

## The philosophy

SoaTable is opinionated, and the opinions are what make it fast and trustworthy. They are worth
stating plainly, because they explain every design choice you will meet below.

- **You pay only for what you use.** The core is a single header with no dependencies. Everything
  beyond the table itself, math, queries, on-disk storage, threading, units, lives in a separate
  header you include _only if you reach for it_. A feature you never touch costs you nothing: not
  memory, not binary size, not compile time.
- **The row API never leaves you.** No matter which advanced storage or layout you pick underneath,
  the way you insert, assign, read, and query stays identical. You choose performance trade-offs
  without rewriting your program.
- **Predictability over cleverness.** SoaTable is used in places where a surprise is unacceptable,
  trading systems that cannot stall, flight software that cannot throw. So it offers exception-free
  error paths, deterministic memory strategies, and a build mode with no exceptions at all. When a
  clever shortcut and a predictable one disagree, predictability wins.
- **Stable identity is sacred.** Handles survive deletion, insertion, and sorting. A stale handle is
  _detected_, never silently pointed at the wrong record.

## Is SoaTable the right tool for you?

**Reach for it when:**

- **Games and simulations (Entity-Component-System worlds).** Thousands of entities, each with a
  sparse mix of components, stable identity across frames, and systems that walk a few components at
  a time.
- **Finance.** Tick and portfolio tables, columnar analytics, group-by aggregation, compact storage,
  and fast snapshots.
- **Engineering and aerospace telemetry.** Frame stores with optional channels, smoothly changing
  signals, recompute-only-what-changed workflows, unit safety, and a no-exceptions build for flight
  software.
- **Scientific and data work.** Hand a whole column straight to a math kernel or a numerical library
  with no copying, run vectorized operations, or memory-map columns that are larger than RAM.

**Prefer a plain `std::vector<YourStruct>` when** the table is tiny, every record always has every
field, and every pass over the data touches every field anyway. SoaTable's advantage grows with
size, with sparsity, and with how selective your questions are.

## Quick start

Here is a complete, runnable program. Read the comments and you have the whole core idea in your
hands.

```cpp
#include <soatable/soatable.hpp>
#include <print>

// Columns are tiny named types. The name is what makes a column distinct.
struct Health { float value; };
struct Name   { std::string value; };
struct Poisoned {};                 // A field with no data: just a yes/no tag.

int main() {
    soatable::soa_table<Health, Name, Poisoned> table;

    const auto id = table.insert();      // Create a record; get back a stable handle.
    table.assign<Name>(id, "Warrior");   // This record has a Name ...
    table.assign<Health>(id, 100.0f);    // ... and a Health, but no Poisoned tag.

    // Walk every record that has both Health and Name. The structured binding
    // gives you real references: writing through them edits the table directly.
    for (auto [row, health, name] : table.view<Health, Name>()) {
        health.value -= 10.0f;
        std::println("{} has {} health", name.value, health.value);
    }

    // Optional columns: include Poisoned when present, without requiring it.
    for (auto [row, name, poison] : table.select<Name, std::optional<Poisoned>>()) {
        if (poison) std::println("{} is poisoned!", name.get().value);
    }
}
```

## The guided tour

Each section below opens with the everyday idea, then shows it in code. Together they cover every
feature SoaTable offers.

### 1. The table: rows, handles, and sparse columns

**The idea.** A table is a fixed set of columns. You add records with `insert()`, attach fields with
`assign`, drop a field with `unassign`, and remove a record with `erase`. The handle you get back
(`row_id`) is a _generational_ ticket: it remembers not just _where_ a record lives but _which_
record, so if a slot is reused by a later record, your old handle correctly reports itself invalid
instead of silently pointing at the newcomer. (This defeats a classic bug known as the ABA problem.)

```cpp
soatable::soa_table<Name, Age> table;

const auto id = table.insert();
table.assign<Age>(id, Age{30});
table.contains<Age>(id);      // true: this record has an Age
table.unassign<Age>(id);      // drop just that field
table.erase(id);              // remove the whole record
table.is_valid(id);           // false: the handle knows its record is gone
```

A column with no data members (like `Poisoned` above) is a **tag**: it answers a yes/no question and
costs almost nothing. This is the natural way to mark state, "is frozen", "is poisoned", "needs
review", without inventing a boolean field.

### 2. Reading values safely

**The idea.** Different situations want different answers to "what if the field isn't there?".
SoaTable gives you three readers so you can pick the failure mode that fits, rather than forcing one
on you.

| Reader                | When the field is missing                | Use it when                             |
| --------------------- | ---------------------------------------- | --------------------------------------- |
| `get<T>(id)`          | throws an exception                      | you are confident it is there           |
| `try_get<T>(id)`      | returns `nullptr`                        | you want a cheap "is it there?" check   |
| `get_expected<T>(id)` | returns a `std::expected` value-or-error | hot paths, or builds with no exceptions |

```cpp
if (auto* hp = table.try_get<Health>(id)) {   // no exception, just a pointer check
    hp->value -= 5.0f;
}
```

Reading through a `const` table gives you `const` references all the way down, so a table is safe to
share read-only across a program without anyone accidentally mutating it.

### 3. Asking questions: views and selects

**The idea.** Most real work is "for every record that has _these_ fields, do something". That is
what `view` and `select` are for. Both walk only the records that have _all_ the requested fields,
and both start from the smallest of those columns so they examine as few records as possible.

- **`view<Cols...>()`** is the everyday workhorse. Its structured bindings hand you **real
  references**, so you read and write fields directly with no ceremony.
- **`select<Cols...>()`** is the more flexible form. It is the one that understands
  `std::optional<T>`, letting a column be included _when present_ without being required.

```cpp
// Move every entity that has both a Position and a Velocity.
for (auto [id, pos, vel] : world.view<Position, Velocity>()) {
    pos.x += vel.x;
    pos.y += vel.y;
}

// Include Age when the record has it; the record is still listed if it doesn't.
for (auto [id, name, age] : table.select<Name, std::optional<Age>>()) {
    if (age) { /* this record has an Age */ }
}
```

With `select`, fields come wrapped so you read them with `.get()`; with `view`, the binding is the
reference itself.

### 4. A friendlier handle: `row_handle`

**The idea.** Carrying a `row_id` plus the table it belongs to everywhere can be tedious.
`row_handle` bundles the two together so you can act on a record directly, and a default-constructed
handle is safely "unbound" (it reports itself invalid rather than misbehaving).

```cpp
soatable::row_handle<Name, Age> handle{ table.insert(), table };
handle.assign<Name>("Dora");
handle.assign<Age>(40);
handle.get<Name>().value;     // "Dora"
handle.contains<Age>();       // true
handle.erase();               // remove the record through the handle
```

### 5. Sorting and bulk loading

**The idea.** Sometimes order matters, ticks by timestamp, players by score. SoaTable can physically
reorder every column together so the data stays meaningfully ordered (which later lets time-series
windows and column views be correct). And when you are loading lots of records at once, batch
operations avoid the per-record overhead.

```cpp
// Sort by one column.
table.sort_by_column<Age>([](const Age& a, const Age& b) { return a.value < b.value; });

// Sort by a primary key, breaking ties with a secondary key.
table.sort_by_multi(
    std::pair<Name, std::function<bool(const Name&, const Name&)>>{
        {}, [](const Name& a, const Name& b) { return a.value < b.value; } },
    std::pair<Age, std::function<bool(const Age&, const Age&)>>{
        {}, [](const Age& a, const Age& b) { return a.value < b.value; } });

// Sort in parallel above a size threshold (serial below it, where threads would not pay off).
table.sort_by_column_parallel<Age>([](const Age& a, const Age& b) { return a.value < b.value; });

// Bulk-load many records, then fill a column from a sequence.
const auto ids = table.insert_batch(3);
std::array<Age, 3> ages{ Age{20}, Age{30}, Age{40} };
table.assign_batch<Age>(ids, ages.begin());
```

### 6. Zero-copy columns and validity bitmaps

**The idea.** Because each column is one dense, contiguous, aligned array, SoaTable can hand it to
you as a `std::span`, a borrowed window over the real storage, with **no copying**. That column can
go straight into a SIMD kernel, an FFT, a linear-algebra library, or any code that wants a plain
array of numbers. Alongside it, a **validity bitmap** is a compact yes/no record of which rows have
the column, perfect for fast "how many", "which ones", and branch-free masked loops. (It is also the
natural bridge to the Apache Arrow data format.)

```cpp
std::span<const Price> prices = table.column<Price>();   // borrowed, aligned, no copy
const auto valid = table.validity<Price>();              // packed presence bitmap
std::println("{} prices, {} rows actually priced", prices.size(), valid.count());
```

### 7. Compact storage helpers

**The idea.** Some data does not need a full-size number. A confidence score between 0 and 1, a few
boolean flags, a value that only ever drifts a little, these can be squeezed into far fewer bytes,
which means more of them fit in cache and everything runs faster. These helpers live in the core
header and can be used directly as column types.

- **`quantized_float`** stores a floating-point value, within a known range, in a small integer (for
  example 8 bits), trading a little precision for a lot of space.
- **`packed_bits`** packs several small fields into the bits of one integer.
- **`delta_value`** tracks a value that changes gently by storing scaled _deltas_ rather than full
  numbers, ideal for smooth signals like altitude.
- **`dirty_mask`** is a set of on/off flags, the clean way to record "which parts of this record
  changed" so you can recompute only those.

```cpp
// A confidence in [0, 1] kept in a single byte.
using Confidence = soatable::quantized_float<std::uint8_t, 0, 1000, 8>;
Confidence c{0.75};
double back = c.get();        // ~0.75, dequantized on read

// Smoothly varying altitude tracked as deltas.
soatable::delta_value<float> altitude{0.0F};
altitude.apply_delta(altitude.get_delta(101.0F));   // nudge toward 101 m
float metres = altitude.get();

// Per-record flags.
enum class Subsystem : std::uint32_t { avionics = 1, propulsion = 2, thermal = 4 };
soatable::dirty_mask<Subsystem> flags;
flags.mark_dirty(Subsystem::thermal);
bool changed = flags.is_dirty(Subsystem::thermal);  // true
```

### 8. The compute layer: math over columns

**The idea.** Once your data is in columns, doing math over it should be one line, and it should be
fast because the compiler can vectorize a tight loop over an aligned array. Include
`<soatable/compute.hpp>` for that. It covers single-column transforms and reductions, scalar
broadcasting, masked and strided variants, chunk-parallel versions, and, crucially, **cross-column**
math like `pnl = price * qty` that walks the join of the inputs and only touches records that have
all of them.

```cpp
#include <soatable/compute.hpp>

// notional = price * qty, computed for every record that has both.
soatable::compute::assign_from<Notional, Price, Qty>(
    book, [](const Price& p, const Qty& q) { return Notional{ p.value * q.value }; });

// Sum a column with a reduction.
double gross = soatable::compute::reduce_column<Notional>(
    book, 0.0, [](double acc, const Notional& n) { return acc + n.value; });
```

### 9. Queries and group-by

**The idea.** Analytics ergonomics, "filter, then group, then total", without dragging in a database
or query engine. Include `<soatable/query.hpp>`. `select_where` filters the join by any condition you
write; `group_sum`, `group_count`, and `group_reduce` aggregate records into buckets keyed by
whatever you project out of a column. All of it composes on top of the smallest-drawer scan, so it
stays fast.

```cpp
#include <soatable/query.hpp>

// Total notional volume per symbol.
auto volume = soatable::query::group_sum<Symbol, Notional>(
    ticks,
    [](const Symbol& s){ return s.value; },     // group key
    [](const Notional& n){ return n.value; });   // value to sum

// Only the large trades.
for (auto row : soatable::query::select_where<Symbol, Notional>(
        ticks, [](auto trade){ return trade.template get<Notional>().value > 10000.0; })) {
    // row.template get<Symbol>().value ...
}
```

### 10. Choosing how memory is laid out

**The idea.** The default layout, one flat, contiguous, SIMD-aligned array per column, is best for
analytics. But other workloads want other trade-offs, and SoaTable lets you pick the layout _without
changing a line of your row code_. The choice is part of the table's type:

- **`soa_table<...>`** (default): flat, contiguous, 64-byte aligned. Best for scanning and number
  crunching.
- **`aosoa_table<Tile, ...>` / `chunked_soa_table<Chunk, ...>`**: storage split into fixed-size tiles
  or chunks, so growth never has to copy the existing data. Good for streaming and bounded latency.
  `column_tiles<T>()` views the data as per-tile aligned spans.
- **`custom_soa_table<Allocator, ...>`**: bring your own allocator. The opt-in `<soatable/pmr.hpp>`
  gives you `pmr_soa_table`, which draws memory from a `std::pmr` arena, monotonic buffer, or pool,
  for deterministic, fragmentation-free allocation in real-time code.
- **`<soatable/mmap.hpp>` `mmap_soa_table`**: column storage is memory-mapped and paged in by the
  operating system on demand, so a single column can be far larger than physical RAM.

```cpp
// Tiled storage in chunks of 4; growth never copies existing chunks.
soatable::aosoa_table<4, Price> tiled;
for (int i = 0; i < 10; ++i)
    tiled.assign<Price>(tiled.insert(), Price{ static_cast<double>(i) });
auto chunks = tiled.column_tiles<Price>();   // per-tile aligned spans
```

### 11. Sharing a table across threads

**The idea.** A common pattern is "many readers, one writer", lots of worker threads reading a market
data table while one thread ingests updates. `<soatable/concurrent.hpp>` wraps any table in a
`synchronized_table` that lets many readers in at once but gives writers exclusive access, all
through scoped callbacks so the locking is automatic and correct.

```cpp
#include <soatable/concurrent.hpp>

soatable::synchronized_table<Table> shared;
shared.write([](Table& t){ t.insert(); });             // exclusive
auto n = shared.read([](const Table& t){ return t.size(); });  // shared with other readers
```

### 12. Time-series helpers

**The idea.** When a column is ordered by time, you often want rolling windows and differences, a
20-tick moving average, the change since the last reading, and you want to recompute only the rows
that actually changed. `<soatable/timeseries.hpp>` provides exactly these, operating on a column's
dense order (so sort by your time key first).

```cpp
#include <soatable/timeseries.hpp>

// 2-period rolling mean of the Price column, in its stored order.
auto means = soatable::timeseries::rolling_mean_column<Price>(
    book, [](const Price& p){ return p.value; }, 2);

// Visit only the records flagged changed, and clear the flags as you go.
using Status = soatable::dirty_mask<Subsystem>;   // a dirty_mask used directly as a column
soatable::timeseries::for_each_dirty<Status>(frames, [](soatable::row_id, Status& s){
    // recompute this record, then s.reset();
});
```

There are also free-function forms (`rolling_sum`, `rolling_mean`, `deltas`) that work on any span of
values.

### 13. Units that catch mistakes at compile time

**The idea.** Adding metres to seconds is always a bug, so it should never compile. `<soatable/units.hpp>`
gives you `quantity<T, Dimension>` types where multiplication and division combine units correctly
(distance / time gives a velocity) while adding mismatched units is a **compile error**. These can be
used directly as column types, so unit safety follows your data into the table.

```cpp
#include <soatable/units.hpp>
namespace un = soatable::units;

auto speed = un::length<>{100.0} / un::duration<>{4.0};  // a velocity, by construction
double v = speed.value();                                 // 25.0
// un::length<>{1.0} + un::duration<>{1.0};   // <-- would not compile, on purpose
```

### 14. Runtime schemas: the dynamic table

**The idea.** Sometimes you do not know the columns until the program is running, loading a CSV whose
headers you have not seen, or letting a user define fields. `<soatable/dynamic.hpp>` gives a
`dynamic_table` whose columns are added and removed by name at runtime, type-checked on every access
(without RTTI), and each column can carry string metadata such as a unit label.

```cpp
#include <soatable/dynamic.hpp>

soatable::dynamic_table dyn;
dyn.add_column<double>("altitude");
dyn.set_metadata("altitude", "unit", "metres");

const auto row = dyn.insert_row();
dyn.set<double>(row, "altitude", 1280.0);
double a = *dyn.get<double>(row, "altitude");   // 1280.0
```

### 15. Saving and loading

**The idea.** To checkpoint a simulation or snapshot a tick store, you want to write the whole table
to bytes and read it back exactly, handles, generations, and column order included.
`<soatable/serialize.hpp>` does this for tables of plain, trivially-copyable columns, wrapping the
data in a versioned, schema-checked header so a load can _reject_ a buffer that does not match,
rather than silently corrupting your data. (The format is native byte order, meant for snapshots read
back on the same kind of machine.)

```cpp
#include <soatable/serialize.hpp>

auto bytes = soatable::save(pod_table);                 // -> std::vector<std::byte>
soatable::soa_table<Price, Qty> restored;
auto status = soatable::load(restored, bytes);
bool ok = (status == soatable::serialize_status::ok);   // also: bad_magic, schema_mismatch, ...
```

### 16. Building a table from a struct

**The idea.** You may already have a plain struct describing a record and would rather not list its
fields twice. `<soatable/reflect.hpp>` lets you derive the table type from the struct. Until C++26
reflection ships in compilers, you write a single one-line mapping; once a compiler supports
reflection, even that line becomes unnecessary and the mapping is automatic.

```cpp
#include <soatable/reflect.hpp>

struct ParticleSchema {};   // a tag naming your schema
template <> struct soatable::columns_of<ParticleSchema>
    : soatable::column_list<Position, Velocity> {};

using ParticleTable = soatable::table_for<ParticleSchema>;   // == soa_table<Position, Velocity>
```

### 17. Running without exceptions

**The idea.** Embedded, kernel, and flight targets often forbid C++ exceptions entirely. Define
`SOATABLE_NO_EXCEPTIONS` and every place that would `throw` instead calls a terminating handler; you
lean on the non-throwing readers (`try_get`, `get_expected`) for normal error handling. SoaTable uses
no RTTI either, and a dedicated CI leg builds the whole library with `-fno-exceptions` to keep this
path honest.

## The opt-in headers at a glance

The core, `soatable/soatable.hpp`, gives you the table, spans, validity bitmaps, and the compression
helpers, with no extra dependencies. Everything else is a header you add only when you want it.

| Header                    | What it adds                                      | Extra standard headers it pulls in |
| ------------------------- | ------------------------------------------------- | ---------------------------------- |
| `soatable/soatable.hpp`   | the table, spans, validity, compression helpers   | nothing extra                      |
| `soatable/compute.hpp`    | column math, broadcasting, chunk-parallel compute | `<future>`, `<thread>`             |
| `soatable/query.hpp`      | `select_where`, group-by aggregation              | `<unordered_map>`                  |
| `soatable/serialize.hpp`  | versioned `save()` / `load()`                     | nothing extra                      |
| `soatable/pmr.hpp`        | `pmr_soa_table` (arena / pool allocation)         | `<memory_resource>`                |
| `soatable/mmap.hpp`       | `mmap_soa_table` (larger-than-RAM columns)        | `<sys/mman.h>` / `<windows.h>`     |
| `soatable/concurrent.hpp` | `synchronized_table` (many readers, one writer)   | `<shared_mutex>`                   |
| `soatable/timeseries.hpp` | rolling windows, deltas, dirty scans              | nothing extra                      |
| `soatable/units.hpp`      | dimensional `quantity` types                      | nothing extra                      |
| `soatable/dynamic.hpp`    | runtime `dynamic_table`                           | `<unordered_map>`                  |
| `soatable/reflect.hpp`    | `table_for<Struct>`                               | nothing extra                      |

## Domain cookbooks

These are complete, runnable programs (built as `soatable_cookbook_*` targets) that put the pieces
together for a real domain:

- [Finance, a tick store](example/cookbook/finance_tick_store.cpp): ingest trades, derive P&L per
  record, total notional volume per symbol, and filter the large trades.
- [Game / ECS](example/cookbook/game_ecs.cpp): a sparse-component world with a motion system over
  `view<Position, Velocity>()` and a data-less tag for frozen entities.
- [Aerospace, telemetry](example/cookbook/aerospace_telemetry.cpp): `delta_value` altitude tracking,
  `dirty_mask` per-frame flags, and a validity bitmap over temperature readings.
- [Scientific data](example/cookbook/scientific_table.cpp): zero-copy span compute and
  `quantized_float` storage.

A broader [feature tour](example/soatable_example.cpp) exercises most headers in one program.

## Installation

SoaTable is header-only and needs a C++23 compiler (GCC 13+, Clang 18+, or MSVC from VS 2022) and
CMake 3.25+.

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

The core `soatable.hpp` is self-contained. To emit a stamped, vendorable copy:

```sh
python scripts/amalgamate.py            # writes dist/soatable.hpp
```

The opt-in add-on headers are kept separate on purpose, so you pay only for what you include. Pass
`--with compute,query` to bundle specific ones next to the core.

### Debugger visualizers

SoaTable ships pretty-printers that render its types (tables, handles, `delta_value`,
`quantized_float`, ...) as readable summaries in a debugger. When you consume the library through
CMake, the Visual Studio (`.natvis`) visualizer is embedded into your program automatically, with no
setup. The GDB and LLDB printers are opt-in: follow the short setup in
[Visualizers](tools/visualizers/README.md).

## Benchmarks: what the speed actually is

The whole point of SoaTable is speed on selective questions, so the claim is measured, not asserted.
The suite covers insertion, erase churn, single-column and parallel sort, sparse selection across a
sweep of densities, the smallest-drawer heuristic, SoA-versus-tiled selection, and compute
throughput, each against an Array-of-Structures baseline and a hand-written columnar baseline.

```sh
./scripts/run_benchmarks.sh             # builds the `bench` preset, writes bench/results.json
```

A representative result, a selective join over 250,000 rows in a Release build:

| Method                                            | Time          |
| ------------------------------------------------- | ------------- |
| SoaTable `select` (starts from smallest drawer)   | ~168,000 ns   |
| Same, but forced to start from the largest drawer | ~1,547,000 ns |
| Hand-rolled columnar scan                         | ~1,058,000 ns |
| Array-of-Structures branch scan                   | ~1,303,000 ns |

The roughly 9x gap between starting from the smallest drawer and being forced to start from the
largest is the whole thesis of the library in one number: examine fewer records, finish sooner.
Numbers are machine dependent; reproduce them with the harness above.

## Building, testing, and docs

```sh
cmake --preset clang        # or: gcc, msvc, asan-ubsan, bench
cmake --build --preset clang
ctest --preset clang
```

CI builds GCC 13, Clang 18, and MSVC, plus an AddressSanitizer + UndefinedBehaviorSanitizer leg, all
through these same presets, so a green local run should match CI. Build the Doxygen API site locally
with:

```sh
cmake -S . -B build-docs -DSOATABLE_BUILD_DOCUMENTATION=ON
cmake --build build-docs --target docs
```

## Versioning and naming

SoaTable follows [Semantic Versioning](https://semver.org). The public API uses std-style lowercase
names (`soa_table`, `row_handle`, `column_vector`, `delta_value`, `dirty_mask`); the earlier
PascalCase spellings remain as `[[deprecated]]` aliases for one migration window so existing code
keeps compiling while you move.

## Contributing

Contributions are welcome. [CONTRIBUTING](CONTRIBUTING.md) explains the philosophy behind the
project and how to make a change that fits it, the principles, the local build and sanitizer
workflow, and what a good pull request looks like.

## License

MIT, see [LICENSE](LICENSE).
