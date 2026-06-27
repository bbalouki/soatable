# Contributing to SoaTable

## Why this library exists

Most data structures are designed for the programmer. SoaTable is designed for the machine. It
takes data that you _think about_ one row at a time and lays it out the way the hardware actually
_reads_ it: each column its own dense, aligned array, touched only when a query needs it. The result
is a row-oriented API sitting on top of memory the cache loves.

That single idea has a cost discipline behind it: **you pay only for what you use**. The core is one
header with no dependencies. Everything else, the compute layer, queries, mmap storage,
concurrency, units, lives in a header you include _only if you reach for it_. Nothing you do not use
should cost you a cycle, a byte, or a second of compile time.

This is not an abstract preference. The people reading these columns are writing game-engine entity
worlds, trading systems that cannot pause for a garbage collector, and flight software that cannot
throw. For them, "fast" and "correct" are not features to be added later; they are the reason the
library exists at all. If you want the shape of the problem and where SoaTable wins, the README's
"idea in 30 seconds" and "when SoaTable is the right tool" say it well. This document is about
something else: how to think while you work on it.

## Why contribute

Work like this is rare. There is no framework to hide behind, no allocator handed to you, no
dependency to blame. It is header-only C++23, zero core dependencies, and a public surface small
enough that one person can hold the whole thing in their head. That smallness is the gift: you can
understand a change _completely_ before you make it, and you can see the consequences immediately.

The feedback loops here are honest. Sanitizers tell you when you are wrong about memory.
Compile-fail tests tell you when an interface promises something it should reject. Benchmarks tell
you whether your clever idea was actually clever. Few codebases give you instruments this sharp, and
working against them is how you get genuinely good at systems programming.

You do not need to land a new subsystem to matter here. A failing edge case turned into a test, a
benchmark that exposes a regression, a header that compiles a little cleaner, a comment that finally
explains the _why_, these are real contributions. The bar is high, but the unit of progress is
small.

## The principles that guide a good contribution

These are not rules to pass a checklist; they are the judgment the codebase is built on. A good
change is one you can defend in these terms.

- **The core stays small; everything optional is opt-in.** New capability earns its own header that
  a user includes only when they want it, the way `compute`, `query`, `mmap`, and the rest already
  do. Adding to `soatable.hpp` is a decision to make _everyone_ carry your feature. Make that case
  deliberately, or keep it out of the core.

- **You pay only for what you use.** A feature nobody calls should cost nothing, not in runtime, not
  in binary size, not in compile time. Templates that instantiate lazily, abstractions that vanish
  under optimization: zero-overhead is the contract, not the aspiration.

- **Predictability over cleverness.** The no-exceptions build, the `std::expected` hot paths, the
  deterministic allocators, none of these exist for elegance. They exist because this code runs
  where a surprise is a crash, a missed trade, or a failed mission. When a clever path and a
  predictable path disagree, the predictable one wins.

- **Mechanical sympathy, not guesswork.** Understand the layout and the cache before you optimize,
  then measure. The smallest-driver join heuristic is the house example: it is not faster because it
  feels faster, it is faster because driving iteration off the smallest column scans far fewer rows,
  and the benchmark suite proves it. Profile, then change.

- **Correctness is demonstrated, not asserted.** A feature without tests is not finished, it is a
  hypothesis. Trust in this library is built out of unit tests, sanitizer-clean runs, and
  compile-fail contracts that prove the interface rejects what it should. If you cannot show it is
  correct, it is not.

- **Leave the public API as quiet as you found it.** The names are std-style and lowercase, the
  surface is intentionally minimal, and breaking changes follow SemVer with a deprecation alias and
  a migration note. The best API change is the one a user never has to notice.

## How to proceed

1. **Understand before you add.** Read the code you intend to touch and find the invariant it
   protects, the generational handle that defeats the ABA problem, the independence of columns, the
   per-row signature bitset that makes selective joins cheap. Most mistakes here are not typos; they
   are a change that quietly violates one of these. Know which one your change lives next to.

2. **Discuss direction early.** For anything beyond a fix, open an issue first and describe the
   problem and the shape of your approach. Design is cheaper in prose than in a rejected PR, and a
   short conversation often reveals the opt-in header your feature really wants to live in.

3. **Build and prove it locally.** The presets mirror CI, so a green local run should mean a green
   pipeline. At minimum, build and test on one compiler and run the sanitizer leg:

   ```bash
   cmake --preset clang        # or: gcc, msvc
   cmake --build --preset clang
   ctest --preset clang

   cmake --preset asan-ubsan   # Address + UndefinedBehavior sanitizers
   cmake --build --preset asan-ubsan
   ctest --preset asan-ubsan
   ```

   Keep the public header warning-clean (the `soatable_warning_check` target enforces
   `-Werror`/`/WX`) and run `cmake --build build --target format` before you push. The full compiler
   matrix and docs build live in the README's "Building, testing, docs".

4. **Make the change provable.** Add tests alongside it under `tests/unittests/`, one file per
   surface area, real implementations, deterministic inputs, never random seeds. Contracts that must
   _fail_ to compile belong in `tests/compile_fail/`. The sanitizer preset must stay clean.

5. **Keep the PR focused and honest.** One idea per pull request, branched from `main`, with a
   description of the motivation and the approach, not just the diff. If you touch the public API,
   follow SemVer and ship a `[[deprecated]]` alias with a migration note rather than a silent break.
   New public API gets Doxygen `///` comments. The full coding standard, naming, initialization,
   casting, the lot, lives in [CLAUDE](CLAUDE.md); write code that already reads like the code
   around it.

## A note on the bar

The standard here is high on purpose, and review can be exacting. None of it is about you; it is
about the code, which strangers will trust in places where mistakes are expensive. A small,
well-reasoned change that respects the principles above is always more welcome than a large one that
does not. Bring the small, sharp thing. We are glad you are here.
