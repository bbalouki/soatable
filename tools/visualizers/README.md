# SoaTable debugger visualizers

Pretty-printers that render SoaTable types in a debugger as compact summaries that expand into
columns and row metadata.

## Visual Studio / MSVC (`.natvis`)

`soatable.natvis` is auto-loaded when added to a project or solution. Alternatively, copy it to:

```
%USERPROFILE%\Documents\Visual Studio 2022\Visualizers\
```

It visualizes `row_id`, `delta_value`, `dirty_mask`, `quantized_float`, `bitmap`, `column_vector`,
`basic_soa_table` (the type behind `soa_table` / `aosoa_table`), and `row_handle`. A table shows its
alive-row count and expands into `columns` (each column's dense data) and `row_meta`.

## GDB

Add to `~/.gdbinit` (or a project `.gdbinit`):

```gdb
python
import sys
sys.path.insert(0, "/path/to/soatable/tools/visualizers")
import soatable_gdb
soatable_gdb.register()
end
```

## LLDB

LLDB uses a different API, but the field names are identical, so `type summary add` works, e.g.:

```
type summary add -s "row[${var.index}] gen=${var.generation}" soatable::row_id
```

These visualizers are best-effort developer-experience aids and are not exercised by CI (they need a
live debugger session).
