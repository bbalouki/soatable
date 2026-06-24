# FEATURES.md

> The **net-new capabilities** that would take SoaTable from a strong sparse-column container to
> _"the numpy of Structure-of-Arrays"_ — portable, domain-agnostic, and best-in-class. For
> hardening of what already exists, see [IMPROVEMENTS.md](IMPROVEMENTS.md).

## Vision

numpy won by being three things at once: a **cache-friendly contiguous array**, a **rich compute
layer** over that array, and a **universal interchange format** every language and tool speaks.
SoaTable already nails the _layout_ primitive (sparse columns + generational handles). To become the
numpy of SoA it needs the **compute layer**, the **interchange story**, and the **portability** that
let finance, engineering, aerospace, games, and data science all reach for it.

Each feature is framed as **What / Use cases / Sketch / Priority / Effort**, where priority is **P0**
(foundational), **P1** (high value), **P2** (differentiator), and effort is **S / M / L**.

Bar-setting references: [soagen](https://marzer.github.io/soagen/) (SIMD alignment, contiguous
allocation, tuple rows, swizzling, binary serialization, `.natvis`),
[Apache Arrow](https://arrow.apache.org/docs/format/Columnar.html) (columnar interchange, 64-byte
alignment, null bitmaps), [EnTT](https://github.com/skypjack/entt) (sparse-set ECS), AoSoA hybrid
layouts, and emerging **C++26 reflection**-based SoA generation.

---

## Roadmap at a glance

| Phase | Theme                                   | Priority | Target |
| ----- | --------------------------------------- | -------- | ------ |
| A     | Foundational parity (layout primitives) | P0 / P1  | v0.5   |
| B     | Compute layer ("the numpy part")        | P1       | v0.7   |
| C     | Portability & interop                   | P1 / P2  | v0.8   |
| D     | Scale & concurrency                     | P2       | v0.9   |
| E     | Domain enablers & developer experience  | P2       | v1.0   |

---

## Phase A — Foundational parity with best-in-class (P0 / P1)

### A.1 Zero-copy column spans

- **What:** A `column<T>() -> std::span<T>` (and `const` form) exposing a column's dense data as a
  contiguous, mutable view.
- **Use cases:** Hand a column straight to BLAS/LAPACK (finance risk matrices), an FFT (engineering
  signal processing), or a SIMD kernel — no copy. This _is_ the "numpy array" primitive.
- **Sketch:** Return a span over the existing dense `m_data`; pair with a `row_ids()` span so callers
  can map dense positions back to handles. Document that erase/sort can invalidate spans.
- **Priority:** P0 · **Effort:** S

### A.2 SIMD-aligned columns and optional AoSoA layout

- **What:** Per-column over-alignment (32 / 64 bytes) plus an opt-in tiled **AoSoA** storage mode.
- **Use cases:** Vectorized physics/particle updates (aerospace, games), columnar analytics. Cited
  layout-driven gains of 40–60% on real workloads; AoSoA gets SoA throughput with better cache
  locality.
- **Sketch:** An aligned allocator behind the column vectors + `std::assume_aligned` in hot loops; a
  policy template selecting `SoA` vs `AoSoA<tile_size>` so the API is unchanged. Aligns with Arrow's
  64-byte recommendation.
- **Priority:** P0 · **Effort:** L

### A.3 First-class null / validity bitmaps

- **What:** Surface per-column presence as an Arrow-style validity bitmap, complementing the existing
  per-row signature.
- **Use cases:** Sparse financial panels, sensor logs with dropouts, any "mostly-empty" column — and a
  prerequisite for Arrow interchange (A.4).
- **Sketch:** Expose `validity<T>() -> bitmap_view`; let bulk ops consume it for branchless masked
  iteration. **Priority:** P1 · **Effort:** M

### A.4 Serialization & persistence

- **What:** (1) A trivially-copyable fast path that dumps/loads columns as raw bytes (like soagen's
  `data()`/size), and (2) a portable, **versioned** on-disk format. (3) An optional Arrow / Parquet
  bridge.
- **Use cases:** Snapshot a simulation (aerospace), persist a tick store (finance), move tables between
  C++/Python/Rust/Spark without re-encoding (Arrow is the lingua franca of analytics).
- **Sketch:** `static_assert(std::is_trivially_copyable_v<T>)`-gated fast path; a schema header carrying
  version + column types for the portable path; a thin adapter mapping columns to Arrow `Array`s behind
  an optional dependency. **Priority:** P1 · **Effort:** L

---

## Phase B — Compute layer, "the numpy part" (P1)

### B.1 Vectorized column ops (ufuncs)

- **What:** `transform`, `reduce`, `scan`, `filter`, and element-wise arithmetic over columns, with a
  `std::execution` / SIMD backend.
- **Use cases:** `pnl = price * qty` across a million rows (finance); `magnitude = sqrt(vx²+vy²+vz²)`
  (engineering) — expressed once, vectorized automatically.
- **Sketch:** Free functions over `std::span` columns (A.1) dispatching to parallel/SIMD algorithms;
  optionally fuse element-wise chains. **Priority:** P1 · **Effort:** L

### B.2 Query / expression DSL

- **What:** Composable predicates and projections layered on `select<>()`, with filter push-down and
  groupby / aggregate primitives.
- **Use cases:** `table.where(price > 100 && active).select<Symbol, Pnl>()`; group telemetry by region
  and average — analytics ergonomics without leaving C++.
- **Sketch:** Lightweight expression templates compiling to the existing smallest-driver scan plus a
  predicate; `group_by<Key>().agg(sum<Value>())` building on B.1. **Priority:** P1 · **Effort:** L

### B.3 Masked & strided views, broadcasting

- **What:** Views that iterate a subset (a mask or stride) of a column and broadcast scalars over a
  column where it reads naturally.
- **Use cases:** Apply a correction to flagged rows only; add a scalar bias to a whole column.
- **Sketch:** A `masked_span` / `strided_span` adapter feeding the B.1 ops. **Priority:** P2 · **Effort:** M

---

## Phase C — Portability & interop (P1 / P2)

### C.1 Language bindings

- **What:** A First-class **Python** bindings (pybind11).
- **Use cases:** This is the literal "numpy" forcing function — let data scientists `import soatable`
  and get zero-copy numpy arrays via the buffer protocol.
- **Sketch:** Python module mapping columns to numpy arrays
  through A.1 spans + the buffer protocol. **Priority:** P1 · **Effort:** L

### C.2 Custom allocators

- **What:** Allocator-aware columns: `std::pmr`, arena/bump, and hooks for GPU/pinned memory.
- **Use cases:** Deterministic, fragmentation-free allocation for real-time/aerospace; arena reuse in
  game frames; staging buffers for GPU offload.
- **Sketch:** Thread an allocator template/parameter through `ColumnVector`; default unchanged.
  **Priority:** P1 · **Effort:** M

### C.3 Freestanding / no-exceptions subset

- **What:** A compile mode with no exceptions, no RTTI, and minimal heavy includes (ties to
  [IMPROVEMENTS.md](IMPROVEMENTS.md) 0.2), surfacing `std::expected`-style error returns.
- **Use cases:** Embedded flight software, microcontrollers, kernel/driver contexts where the STL
  exception machinery is banned.
- **Sketch:** Guard exception paths behind `SOATABLE_NO_EXCEPTIONS`; provide error-code/`expected`
  accessors. **Priority:** P2 · **Effort:** M

### C.4 C++26 reflection path

- **What:** When the toolchain supports it, generate columns directly from a plain `struct` — no manual
  type list — while keeping the explicit type-list API as the portable fallback.
- **Use cases:** `SoaTable<Particle>` from `struct Particle { float x, y, z; ... };` with zero
  boilerplate, matching the ergonomics modern users expect.
- **Sketch:** A reflection adapter enumerating members into columns behind a feature test.
  **Priority:** P2 · **Effort:** L

---

## Phase D — Scale & concurrency (P2)

### D.1 Concurrent / thread-safe table

- **What:** A variant supporting concurrent readers with safe writers — sharded storage and/or
  snapshot (MVCC) read views.
- **Use cases:** A market-data table read by many strategy threads while one thread ingests; an ECS
  world iterated by parallel systems.
- **Sketch:** Reader-friendly snapshots with copy-on-write column versions, or per-shard locking with a
  stable global handle space. **Priority:** P2 · **Effort:** L

### D.2 Chunked column storage

- **What:** Store each column as a sequence of fixed-size chunks (Arrow-style record batches) instead of
  one monolithic vector.
- **Use cases:** Bounds reallocation/copy cost on growth, enables streaming and parallelism by chunk,
  and is the natural unit for Arrow interchange (A.4).
- **Sketch:** A `chunked_column<T>` with the same span/iteration surface; `select` iterates chunk by
  chunk. **Priority:** P2 · **Effort:** L

### D.3 Out-of-core / memory-mapped columns

- **What:** Back columns with `mmap`ed files so datasets exceed RAM.
- **Use cases:** Multi-billion-row historical tick stores; large engineering datasets on workstations.
- **Sketch:** A memory-mapped allocator (composes with C.2) for trivially-copyable columns; lazy
  paging. **Priority:** P2 · **Effort:** L

---

## Phase E — Domain enablers & developer experience (P2)

### E.1 Domain cookbooks & ready-made examples

- **What:** End-to-end examples proving "literally anyone can use it": finance (tick / portfolio
  tables), ECS/game world, aerospace telemetry, scientific data table.
- **Use cases:** Adoption — concrete recipes turn curiosity into usage faster than reference docs.
- **Sketch:** One worked example per domain under `example/`, each linked from the README.
  **Priority:** P2 · **Effort:** M

### E.2 Debugger visualizers

- **What:** MSVC `.natvis` and GDB/LLDB pretty-printers that render a table as rows×columns.
- **Use cases:** Inspecting a sparse table in a debugger is otherwise painful; soagen ships `.natvis`
  for exactly this. **Priority:** P2 · **Effort:** S

### E.3 Runtime schema evolution & typed metadata

- **What:** Add/remove columns at runtime, attach typed metadata, and carry **units** on columns.
- **Use cases:** Evolving schemas without recompiling; dimensional-safety for engineering (reject
  adding metres to seconds at the boundary).
- **Sketch:** A type-erased dynamic-column layer alongside the static one; optional units tags checked
  at op boundaries. **Priority:** P2 · **Effort:** L

### E.4 Time-series helpers

- **What:** Higher-level helpers (rolling windows, deltas, dirty-region scans) built on the existing
  `DeltaValue` and `DirtyMask`.
- **Use cases:** Smooth telemetry tracking (aerospace), incremental recompute of changed rows (games,
  finance). **Priority:** P2 · **Effort:** M

---

## Sequencing notes

- **A.1 (spans)** is the keystone: B (compute), C.1 (Python/numpy), and A.4 (serialization) all build
  on contiguous column views, so land it first.
- **A.4 + D.2** together unlock the Arrow interchange story that makes SoaTable a true ecosystem
  citizen.
- **C.1 (Python bindings)** is the single highest-leverage adoption lever toward the "numpy of SoA"
  identity and should be prioritized once A.1/B.1 exist.
- Every feature here assumes the Phase 0/1 correctness and test work in
  [IMPROVEMENTS.md](IMPROVEMENTS.md) is in place first — new surface area on an unverified core just
  multiplies risk.
