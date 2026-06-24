# Changelog

All notable changes to SoaTable are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Foundational layout primitives (FEATURES Phase A) and the start of the compute layer (Phase B).

### Added

- Opt-in `<soatable/compute.hpp>` vectorized column operations: single-column span ufuncs
  (`transform`, `reduce`, `inclusive_scan`, `count_if`), policy-agnostic table helpers
  (`transform_column`, `reduce_column`, `count_column_if`) over `column_tiles<T>()`, and cross-column
  row-wise `assign_from<Out, In...>()` for expressions like `pnl = price * qty` (B.1).

- `column<T>()` (and a `const` overload) returning a `std::span` over a column's dense values, plus
  `row_indices<T>()` and `make_row_id()` to map dense positions back to stable handles (A.1).
- `validity<T>()` returning an Arrow-style packed `bitmap` of per-row column presence, with
  `for_each_set` for branchless masked iteration (A.3).
- Opt-in `<soatable/serialize.hpp>` with `save()` / `load()`: a versioned, schema-checked binary
  snapshot for trivially-copyable columns that round-trips handles, generations, and column order
  (A.4).
- SIMD-aligned column storage: dense column data is over-aligned to `simd_alignment` (64 bytes,
  matching Apache Arrow) via an internal aligned allocator, so `column<T>()` spans are ready for
  aligned SIMD loads (A.2).
- Opt-in AoSoA tiled storage: `aosoa_table<TileSize, Columns...>` stores each column's dense values
  in fixed-size, individually over-aligned tiles (bounded reallocation; per-tile SIMD), selected via
  a storage policy on the new `basic_soa_table<Storage, Columns...>`. `soa_table` is the unchanged
  contiguous default. `column_tiles<T>()` gives a uniform per-tile span view across both policies
  (A.2).

## [0.3.0] - 2026-06-24

API ergonomics and a deliberate naming migration (IMPROVEMENTS Phase 2).

### Added

- `row_view`, a reference-semantic row proxy that models the tuple protocol, plus
  `view<Columns...>()` (and a `const` overload) so structured bindings yield real references
  without unwrapping `std::reference_wrapper`.
- `get_expected<T>()` non-throwing accessors returning
  `std::expected<std::reference_wrapper<T>, access_error>`, with an `access_error` enum, for hot
  paths and no-exceptions builds.
- Compile-time const-correctness tests covering `get` / `try_get` / `get_expected` / `select`.

### Changed (breaking)

- Public types renamed to std-style lowercase: `SoaTable` -> `soa_table`, `RowHandle` -> `row_handle`,
  `ColumnVector` -> `column_vector`, `DeltaValue` -> `delta_value`, `DirtyMask` -> `dirty_mask`.
  The previous PascalCase spellings remain as `[[deprecated]]` aliases for this migration window and
  will be removed in a future major release.

### Documentation

- Documented the accessor error-handling contract (throw vs `nullptr` vs `std::expected`).

## [0.2.0] - 2026-06-24

Correctness hardening and a real test suite (IMPROVEMENTS Phase 0 and Phase 1).

### Added

- Comprehensive unit-test suite under `tests/unittests/` (move-only and throwing column types,
  free-list churn, ABA stale-handle, large tables, select edge cases, compression round-trips, a
  fixed-seed invariant/property test, compile-fail contracts, and an include-hygiene test).
- AddressSanitizer + UndefinedBehaviorSanitizer CI leg and a strict-warning compile gate.

### Changed

- `column_vector` now uses an unsigned `size_type` index with an explicit `npos` sentinel instead of
  a signed index with a `-1` tombstone, removing all signed/unsigned mixing.
- `emplace` (new-value path) and `insert` (grow path) now provide a strong exception guarantee; every
  public mutator documents its guarantee.
- Saturated row slots are now retired instead of recycled, closing the ABA generation-wraparound hole.

### Removed (breaking)

- The `sstd` namespace alias is now opt-in behind `SOATABLE_ENABLE_SSTD_ALIAS`.
- Dropped the unused `<iostream>` include from the public header.

## [0.1.0]

Initial release: header-only C++23 sparse Structure-of-Arrays table with generational `row_id`
handles, smallest-driver `select<>()`, batch operations, single / multi / parallel sorting, and the
`quantized_float` / `packed_bits` / `delta_value` / `dirty_mask` helpers.

[0.3.0]: https://github.com/bbalouki/soatable/releases/tag/v0.3.0
[0.2.0]: https://github.com/bbalouki/soatable/releases/tag/v0.2.0
[0.1.0]: https://github.com/bbalouki/soatable/releases/tag/v0.1.0
