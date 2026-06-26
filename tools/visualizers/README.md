# SoaTable debugger visualizers

This folder contains **debugger pretty-printers** for SoaTable. They are a developer convenience: they
change only what _you_ see when you pause a program in a debugger and inspect a SoaTable value. They
have no effect on your built program, its speed, or its behaviour.

If you have never used debugger visualizers before, this page explains what they are, why they help,
and how to turn them on for your debugger.

## Why this exists

SoaTable deliberately stores data in a clever, non-obvious shape. A record's fields are scattered
across separate column arrays, rows are reached through generational handles, and some values are
compressed (`quantized_float`), encoded as deltas (`delta_value`), or packed into bits
(`dirty_mask`). That layout is what makes SoaTable fast, but it also means a raw debugger shows you
the _internal machinery_ instead of the _meaning_.

For example, without these visualizers, stopping at a breakpoint and expanding a table shows nested
tuples of vectors, bitsets, and a free list; a `row_id` shows as two separate integer members; and a
`delta_value` or `quantized_float` shows as a meaningless raw integer. You end up reverse-engineering
the data structure in your head every time.

With the visualizers loaded, the same values render as short, readable summaries:

```text
  Raw debugger view                          With the visualizers
  -------------------------------            -----------------------------
  row_id { index=3, generation=2 }     ->    row[3] gen=2
  delta_value { m_value=101, ... }     ->    delta_value(101)
  dirty_mask { m_mask=4 }              ->    dirty_mask(0x4)
  bitmap { m_words={...}, m_size=64 }  ->    bitmap(bits=64)
  basic_soa_table { m_columns=... }    ->    soa_table(rows=128)
```

A table summary expands further into its live columns and per-row metadata, so you can drill into the
data without manually walking the storage.

## What gets a nicer view

| Type              | What the summary tells you                                                                                          |
| ----------------- | ------------------------------------------------------------------------------------------------------------------- |
| `row_id`          | the row index and its generation (so stale handles are obvious)                                                     |
| `delta_value`     | the current decoded value, not the raw stored delta                                                                 |
| `quantized_float` | the dequantized floating-point value, not the packed integer                                                        |
| `dirty_mask`      | which flags are set                                                                                                 |
| `bitmap`          | how many bits it tracks                                                                                             |
| `column_vector`   | a column's dense values                                                                                             |
| `basic_soa_table` | the live-row count, expanding into columns and row metadata (this is the type behind `soa_table` and `aosoa_table`) |
| `row_handle`      | the bound row and the table it belongs to                                                                           |

The two files do this for different debuggers:

- `soatable.natvis` for Visual Studio / MSVC (covers all of the types above).
- `soatable_gdb.py` for GDB (covers the most common ones: `row_id`, `delta_value`, `dirty_mask`,
  `bitmap`, and the table). LLDB can reuse the same field names; see below.

## Visual Studio / MSVC (`.natvis`)

If you consume SoaTable through CMake (`find_package`, `FetchContent`, or vcpkg's CMake integration),
**there is nothing to do**, the `.natvis` is attached to the `soatable::soatable` target and Visual
Studio embeds it into your program's debug information automatically. Just build with debug info and
the nicer views appear.

If you are _not_ building through SoaTable's CMake target (for example you vendored a single
amalgamated header), point Visual Studio at the file in any one of these ways:

- Add `soatable.natvis` to your `.vcxproj` or solution, or
- Copy it into your per-user visualizers folder, where VS auto-loads it:

  ```
  %USERPROFILE%\Documents\Visual Studio 2022\Visualizers\
  ```

Changes to a `.natvis` are picked up on the next debugging step, no rebuild required, which makes it
easy to experiment.

## GDB

GDB does not load pretty-printers automatically, you register them once from your GDB startup file.
Add the following to `~/.gdbinit` (for all your projects) or to a project-local `.gdbinit`, adjusting
the path to wherever this folder lives:

```gdb
python
import sys
sys.path.insert(0, "/path/to/soatable/tools/visualizers")
import soatable_gdb
soatable_gdb.register()
end
```

After installing SoaTable, the folder is typically:

- `<install-prefix>/share/soatable/visualizers/` (CMake and vcpkg installs), or
- `<package>/res/soatable/visualizers/` (Conan packages), or
- `tools/visualizers/` if you are working from a checkout of the repository.

Once registered, GDB applies the summaries to any SoaTable value you print or inspect.

## LLDB

LLDB uses a different mechanism from GDB, but the underlying field names are identical, so a set of
one-line `type summary add` commands gives you the same readable views. Copy and paste the whole
block below into your `~/.lldbinit` (create the file if it does not exist); LLDB runs it at the start
of every session, so the summaries are always on.

```
# SoaTable LLDB pretty-printers. Paste into ~/.lldbinit.
type summary add -s "row[${var.index}] gen=${var.generation}" soatable::row_id
type summary add -s "bitmap(bits=${var.m_size})" soatable::bitmap
type summary add -x "^soatable::delta_value<.+>$" -s "delta_value(${var.m_value})"
type summary add -x "^soatable::dirty_mask<.+>$" -s "dirty_mask(0x${var.m_mask%x})"
type summary add -x "^soatable::basic_soa_table<.+>$" -s "soa_table(rows=${var.m_alive_count})"
```

The `-x` flag marks a type name as a regular expression, which is how the templated types
(`delta_value<...>`, `dirty_mask<...>`, and the `basic_soa_table<...>` behind `soa_table` /
`aosoa_table`) are matched. These cover the same types as the GDB script; to summarize another type,
add a line referencing its members (the field names are `index`, `generation`, `m_value`, `m_mask`,
`m_size`, and `m_alive_count`).

## Good to know

- These are **best-effort developer aids**. They are not compiled, not linked into your program, and
  not exercised by CI (they need a live debugger session to test), so treat them as a convenience
  rather than a guaranteed contract.
- They render SoaTable's _current_ internal field names. If those internals change, a printer may
  need a small update; the displayed values are derived from members like `m_value` and
  `m_alive_count`.
