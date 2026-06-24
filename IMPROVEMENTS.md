# IMPROVEMENTS.md

> A clear-eyed account of what should be **fixed, hardened, and strengthened** in the
> _existing_ SoaTable implementation. For _net-new_ capabilities, see [FEATURES.md](FEATURES.md).

SoaTable (v0.1.0) is a header-only C++23 Structure-of-Arrays (SoA) table. The core design is
strong: a sparse 3-vector [`ColumnVector`](include/soatable/soatable.hpp) (sparse → dense → data),
generational [`row_id`](include/soatable/soatable.hpp) handles that defeat the ABA problem, an
optimized `select<>()` join that drives iteration off the smallest required column, batch ops,
single / multi / parallel sorting, and compression helpers (`quantized_float`, `packed_bits`,
`DeltaValue`, `DirtyMask`).

The goal is to make SoaTable _"the numpy of SoA"_: portable, domain-agnostic (finance,
engineering, aerospace), and best-in-class. This document is the **hardening roadmap** to get the
existing foundation to production grade. Items are framed as **What / Why / Approach / Priority /
Effort**, where priority is **P0** (correctness/blocker), **P1** (important), **P2** (polish), and
effort is **S / M / L**.

Benchmarked against the landscape that defines "top tier": [soagen](https://marzer.github.io/soagen/)
(single contiguous allocation, per-column SIMD alignment, tuple-protocol rows, `.natvis`),
[Apache Arrow](https://arrow.apache.org/docs/format/Columnar.html) (columnar format, 64-byte
alignment, null bitmaps), [EnTT](https://github.com/skypjack/entt) (sparse-set ECS), and AoSoA
hybrid layouts.

---

## Roadmap at a glance

| Phase | Theme                              | Priority | Target |
| ----- | ---------------------------------- | -------- | ------ |
| 0     | Correctness & robustness           | P0       | v0.2   |
| 1     | Testing & quality gates            | P0 / P1  | v0.2   |
| 2     | API ergonomics & const-correctness | P1       | v0.3   |
| 3     | Build, packaging & docs            | P1 / P2  | v0.3   |
| 4     | Performance hardening              | P2       | v0.4   |

---

## Phase 0 — Correctness & robustness (P0)

### 0.1 Index width and signed/unsigned mixing

- **What:** `ColumnVector::m_sparse` is `std::vector<std::int32_t>` with `tombstone = -1`
  ([soatable.hpp:396-398](include/soatable/soatable.hpp#L396-L398)). Every access casts back to
  `std::size_t` (e.g. [:444](include/soatable/soatable.hpp#L444),
  [:490](include/soatable/soatable.hpp#L490)).
- **Why:** Signed dense indices cap a single column at ~2.1B rows and mix signed/unsigned
  arithmetic, which the project style guide (CLAUDE.md) explicitly forbids and which
  `-Wsign-conversion` should be flagging. Silent narrowing on huge tables is a latent corruption
  bug.
- **Approach:** Use an unsigned dense index (`std::uint32_t` or a `std::size_t` typedef) plus an
  explicit `static constexpr ... npos = (max)()` sentinel instead of `-1`. Introduce a single
  `using size_type = ...;` alias so a future 64-bit build is a one-line change. Keep `row_id`
  fields as documented public ABI.
- **Priority:** P0 · **Effort:** M

### 0.2 Remove `<iostream>` from the public header

- **What:** [soatable.hpp:12](include/soatable/soatable.hpp#L12) includes `<iostream>` even though
  the library has a `<print>` fast path ([:22-27](include/soatable/soatable.hpp#L22-L27)).
- **Why:** `<iostream>` injects a static `std::ios_base::Init` object into _every_ translation unit
  that includes the header, inflating binary size and startup cost — unacceptable for a header-only
  library meant to be embedded "everywhere" (including freestanding/aerospace targets).
- **Approach:** Drop the include if unused; if any streaming helper needs it, move that helper to an
  opt-in `<soatable/io.hpp>` guarded behind a macro. Audit the remaining heavy includes
  (`<future>`, `<ranges>`) for the same "pay only if you use it" principle.
- **Priority:** P0 · **Effort:** S

### 0.3 Document & test exception-safety guarantees

- **What:** `insert` / `assign` / `emplace` / `erase` / `reorder` mutate several parallel vectors
  with no stated guarantee. `reorder` ([:520-537](include/soatable/soatable.hpp#L520-L537)) builds
  fresh vectors, which is good, but the multi-column sort path mutates each `ColumnVector` in turn.
- **Why:** A throwing element constructor mid-operation can leave sparse/dense/data out of sync —
  silent corruption. CLAUDE.md requires a _consistent, stated_ guarantee (basic / strong / nothrow).
- **Approach:** Decide and document the guarantee per public method (`@note Strong exception
guarantee.`). Where strong is feasible (single-column assign), build-then-swap; where only basic
  is feasible, say so. Add tests with a column type whose constructor throws on the Nth element.
- **Priority:** P0 · **Effort:** M

### 0.4 ABA generation-counter wraparound

- **What:** `row_id::generation` is `std::uint32_t`
  ([soatable.hpp:44](include/soatable/soatable.hpp#L44)), incremented on each erase of a slot.
- **Why:** After 2^32 erase/reuse cycles on one slot, a stale handle can alias a live row — the
  exact ABA bug the generation counter exists to prevent. Rare, but it is a correctness hole that a
  "best-in-class" library must at least document and ideally detect.
- **Approach:** Document the bound explicitly. Optionally retire a slot (stop recycling) when its
  generation saturates, or widen to 64-bit generation behind the `size_type` work in 0.1.
- **Priority:** P0 · **Effort:** S

### 0.5 Namespace alias & macro hygiene

- **What:** A compatibility alias `sstd` and feature macro `SOATABLE_HAS_PRINT` are exposed at
  global/namespace scope.
- **Why:** Short global aliases and unprefixed-ish macros risk collisions in large downstream
  builds and ODR surprises if the macro is defined differently across TUs.
- **Approach:** Make `sstd` opt-in (only when `SOATABLE_ENABLE_SSTD_ALIAS` is defined). Ensure every
  macro is `SOATABLE_`-prefixed, `#undef`-clean, and never affects the public ABI. Add an
  include-guard / single-include test.
- **Priority:** P1 · **Effort:** S

---

## Phase 1 — Testing & quality gates (P0 / P1)

### 1.1 Close the coverage gap

- **What:** The suite is ~159 lines / ~7 tests ([tests/soatable_tests.cpp](tests/soatable_tests.cpp))
  covering insert/get, erase/validity, required & optional select, multi-column sort, batch ops, and
  `quantized_float`.
- **Why:** Large swaths of the public surface are untested: `sort_by_column_parallel`, `DeltaValue`,
  `packed_bits`, `DirtyMask`, the whole `RowHandle` wrapper, `shrink_to_fit`, free-list recycling
  under churn, optional-column edge cases (zero required columns, all-absent driver), empty and very
  large tables, and **move-only / non-trivial / throwing** column types.
- **Approach:** Add parametrized tests per public method; add a `MoveOnly` and a `ThrowsOnNth` column
  type; add a churn test (insert/erase millions of times, assert slot reuse and handle invalidation).
- **Priority:** P0 · **Effort:** L

### 1.2 Sanitizer CI jobs

- **What:** CI builds GCC 13 / Clang 18 / MSVC but runs no AddressSanitizer or
  UndefinedBehaviorSanitizer.
- **Why:** CLAUDE.md mandates ASan/UBSan; the int/size casts in 0.1 and the parallel sort are exactly
  the code that sanitizers catch.
- **Approach:** Add a Linux Clang matrix leg with `-fsanitize=address,undefined` running the full
  CTest suite. Treat sanitizer findings as build failures.
- **Priority:** P0 · **Effort:** S

### 1.3 Invariant / property tests

- **What:** No test asserts the structural invariant that sparse ↔ dense ↔ data stay consistent.
- **Why:** This single invariant underpins every operation; a property test over randomized
  insert/erase/assign/sort sequences is the highest-leverage safety net.
- **Approach:** After each randomized op, assert: every dense entry round-trips through sparse;
  `size()` equals alive count; no dangling tombstones. Seed deterministically (CLAUDE.md: no random
  values in unit tests — use fixed seeds).
- **Priority:** P1 · **Effort:** M

### 1.4 Compile-fail and round-trip tests

- **What:** Concepts/`static_assert`s (unique columns, registered type) have no negative tests; the
  compression helpers have no round-trip fuzzing.
- **Why:** Compile-time contracts regress silently; lossy compression needs documented, tested error
  bounds.
- **Approach:** Add a CMake-driven `try_compile` matrix asserting the bad cases fail. Add round-trip
  tests sweeping the full value range for `quantized_float` / `DeltaValue` and asserting the bound.
- **Priority:** P1 · **Effort:** M

---

## Phase 2 — API ergonomics & const-correctness (P1)

### 2.1 Friendlier row / select type

- **What:** `select<>()` yields `std::tuple<row_id, std::reference_wrapper<T>...>`
  ([detail::select_result_t](include/soatable/soatable.hpp#L172-L184)). Callers must `.get()` through
  `reference_wrapper`.
- **Why:** Reference-semantic _rows_ (tuple protocol + structured bindings, like soagen) read far more
  naturally: `for (auto row : t.select<A,B>()) row.a += row.b;`.
- **Approach:** Offer a thin row proxy that models the tuple protocol so structured bindings yield real
  references, while keeping the current tuple as a lower-level escape hatch.
- **Priority:** P1 · **Effort:** M

### 2.2 Const-correctness sweep

- **What:** Verify every accessor (`get`, `try_get`, `select`, `for_each_row`, `rows`) has a correct
  `const` overload and that const tables yield `const` references end to end.
- **Why:** A data structure marketed for analytics must be safely shareable as `const`.
- **Approach:** Add `static_assert`-style compile tests over a `const SoaTable&` to lock the contract.
- **Priority:** P1 · **Effort:** S

### 2.3 Error-handling contract

- **What:** `get<>` throws on a missing column/row while `try_get<>` returns `nullptr`; the contract is
  not centrally documented.
- **Why:** Predictable, hot-path failure handling matters for finance/aerospace users who avoid
  exceptions.
- **Approach:** Document the throw-vs-nullptr split once, prominently. Consider an
  `std::expected<T&,error>` accessor (C++23) for the no-exceptions audience tied to the freestanding
  work in [FEATURES.md](FEATURES.md).
- **Priority:** P1 · **Effort:** M

### 2.4 Naming consistency

- **What:** The API mixes `snake_case` (`row_id`, `select`, `insert`) with `PascalCase` (`SoaTable`,
  `RowHandle`, `ColumnVector`, `DeltaValue`, `DirtyMask`).
- **Why:** Inconsistent casing reads as unfinished and contradicts the project style guide.
- **Approach:** Pick one convention deliberately. A defensible choice is std-style lowercase for the
  container-facing API (it _is_ a numpy-like container) — but whatever is chosen, apply it uniformly
  and record the rationale. Treat renames as a breaking change per SemVer; provide deprecated aliases.
- **Priority:** P1 · **Effort:** M

---

## Phase 3 — Build, packaging & docs (P1 / P2)

### 3.1 CMake presets

- **What:** No `CMakePresets.json`; users pass flags by hand.
- **Why:** CLAUDE.md asks for presets; they make the GCC/Clang/MSVC + sanitizer + Release/Debug matrix
  reproducible and one-command.
- **Approach:** Add presets for each toolchain and the sanitizer leg; wire CI to consume them so CI and
  local builds can't drift. **Priority:** P1 · **Effort:** S

### 3.2 CHANGELOG.md and CONTRIBUTING.md

- **What:** Both are absent.
- **Why:** CLAUDE.md mandates both; a "top-tier" OSS project needs a versioned changelog (SemVer) and a
  contribution guide to attract and onboard contributors.
- **Approach:** Add `CHANGELOG.md` (Keep a Changelog format) starting at 0.1.0 and a `CONTRIBUTING.md`
  covering build, style (clang-format/tidy), test, and PR expectations. **Priority:** P1 · **Effort:** S

### 3.3 Distribution channels

- **What:** Install/export exists, but there is no vcpkg port, Conan recipe, or single-header
  amalgamation.
- **Why:** "Use it everywhere" requires friction-free adoption across ecosystems.
- **Approach:** Publish a vcpkg port and a Conan recipe; add a script that amalgamates the header for
  drop-in use. **Priority:** P2 · **Effort:** M

### 3.4 Reproducible benchmarks & API docs

- **What:** README cites ~162µs vs ~2.035ms for 250k rows but without machine specs or a one-command
  harness; there is no rendered API reference despite thorough Doxygen comments.
- **Why:** Unreproducible numbers undermine credibility; a published API site is table stakes.
- **Approach:** Add a `bench` preset that prints CPU/compiler/flags and emits a results file; publish a
  Doxygen site to GitHub Pages in CI. **Priority:** P2 · **Effort:** M

---

## Phase 4 — Performance hardening (P2)

### 4.1 Broaden the benchmark suite

- **What:** One scenario (sparse 3-column select) exists.
- **Why:** Insertion, erase-churn, sort, optional joins, and density sweeps are where regressions hide.
- **Approach:** Add benchmarks across those axes; add baselines against [soagen](https://marzer.github.io/soagen/)
  and a hand-rolled SoA so claims are comparative, not absolute. **Priority:** P2 · **Effort:** M

### 4.2 Parallel-sort strategy

- **What:** `sort_by_column_parallel` spawns one `std::async` per column.
- **Why:** One task per column under-utilizes wide machines and adds launch overhead for small tables
  or few columns.
- **Approach:** Back it with a thread pool; add a size threshold below which it runs serially; document
  the threading model and measure the crossover. **Priority:** P2 · **Effort:** M

### 4.3 Validate the `select` driver heuristic

- **What:** `select` drives iteration off the smallest required column.
- **Why:** The heuristic is sound in theory but unverified across density distributions; a wrong driver
  choice can dominate runtime.
- **Approach:** Add a benchmark that varies per-column density and confirms the chosen driver is optimal
  (or refine the heuristic with measured selectivity). **Priority:** P2 · **Effort:** M

---

## How to verify this roadmap

- Each P0/P1 item should become a tracked issue with the acceptance test named above.
- "Done" for Phase 0/1 = sanitizer CI green, invariant/property tests passing, exception-safety
  guarantees documented and tested.
- Re-run the benchmark suite (Phase 4) before/after any layout change to prove no regression.
