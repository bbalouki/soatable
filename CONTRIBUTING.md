# Contributing to SoaTable

Thanks for your interest in improving SoaTable. This guide covers how to build, test, and submit
changes. See [IMPROVEMENTS.md](IMPROVEMENTS.md) and [FEATURES.md](FEATURES.md) for the roadmap.

## Prerequisites

- A C++23 compiler: GCC 13+, Clang 18+, or MSVC (VS 2022).
- CMake 3.25 or newer.
- Optional tooling: `clang-format`, `clang-tidy`, `doxygen`, and a sanitizer-capable Clang.

GoogleTest and Google Benchmark are fetched automatically via `FetchContent` when not found.

## Building and testing with presets

The repository ships a `CMakePresets.json`. The common workflows:

```sh
# Release build + tests (pick the preset matching your compiler)
cmake --preset clang        # or: gcc, msvc
cmake --build --preset clang
ctest --preset clang

# Address + UndefinedBehavior sanitizers (Clang)
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan
ctest --preset asan-ubsan

# Benchmarks
cmake --preset bench
cmake --build --preset bench
```

The CI matrix (GCC 13, Clang 18, Clang ASan+UBSan, MSVC) consumes these same presets, so a green
local preset run should match CI.

## Coding standards

- Follow the rules in [CLAUDE.md](CLAUDE.md): C++23, `m_`-prefixed private members, `snake_case`
  functions/variables and types, `PascalCase` reserved for nothing in the public API (the container
  API is std-style lowercase), trailing return types, brace initialization, and `static_cast` over
  C-style casts. No magic literals.
- The public header must stay warning-clean under `-Wall -Wextra -Wconversion -Wsign-conversion
  -Werror` (Clang/GCC) and `/W4 /WX` (MSVC). The `soatable_warning_check` target enforces this.
- Format with `clang-format` (a `format` target is available when `clang-format` is installed):

  ```sh
  cmake --build build --target format
  ```

- `clang-tidy` checks (`cppcoreguidelines-*`, `modernize-*`, `readability-*`) are configured in
  `.clang-tidy`.

## Tests

- Add tests alongside any change, under `tests/unittests/`, one file per surface area.
- Use real implementations; only mock external dependencies.
- Use deterministic inputs (fixed seeds), never nondeterministic randomness.
- Negative compile-time contracts go under `tests/compile_fail/` and are run as `WILL_FAIL` tests.
- All tests must pass under the sanitizer preset before submitting.

## Pull requests

1. Branch from `main`.
2. Keep the change focused; update [CHANGELOG.md](CHANGELOG.md) under the appropriate section.
3. Mark any backward-incompatible public API change as breaking and follow SemVer; provide a
   deprecated alias and a migration note where feasible.
4. Ensure CI is green (all compilers + the sanitizer leg) and that new public API has Doxygen `///`
   comments.
5. Open the PR against `main` with a clear description of the motivation and approach.
