"""GDB pretty-printers for SoaTable.

Load from your ~/.gdbinit (or a project .gdbinit) with:

    python
    import sys
    sys.path.insert(0, "/path/to/soatable/tools/visualizers")
    import soatable_gdb
    soatable_gdb.register()
    end

LLDB users can achieve the equivalent with `type summary add` / a synthetic provider; the field
names referenced below (index, generation, m_value, m_mask, m_size, m_alive_count) are the same.
"""

import gdb  # type: ignore
import gdb.printing  # type: ignore


class RowIdPrinter:
    def __init__(self, val):
        self._val = val

    def to_string(self):
        index = int(self._val["index"])
        generation = int(self._val["generation"])
        return "row[{}] gen={}".format(index, generation)


class DeltaValuePrinter:
    def __init__(self, val):
        self._val = val

    def to_string(self):
        return "delta_value({})".format(self._val["m_value"])


class DirtyMaskPrinter:
    def __init__(self, val):
        self._val = val

    def to_string(self):
        return "dirty_mask(0x{:x})".format(int(self._val["m_mask"]))


class BitmapPrinter:
    def __init__(self, val):
        self._val = val

    def to_string(self):
        return "bitmap(bits={})".format(int(self._val["m_size"]))


class TablePrinter:
    def __init__(self, val):
        self._val = val

    def to_string(self):
        alive = int(self._val["m_alive_count"])
        return "soa_table(rows={})".format(alive)


def build_pretty_printer():
    printer = gdb.printing.RegexpCollectionPrettyPrinter("soatable")
    printer.add_printer("row_id", "^soatable::row_id$", RowIdPrinter)
    printer.add_printer("delta_value", "^soatable::delta_value<.*>$", DeltaValuePrinter)
    printer.add_printer("dirty_mask", "^soatable::dirty_mask<.*>$", DirtyMaskPrinter)
    printer.add_printer("bitmap", "^soatable::bitmap$", BitmapPrinter)
    printer.add_printer("soa_table", "^soatable::basic_soa_table<.*>$", TablePrinter)
    return printer


def register(objfile=None):
    """Register the SoaTable pretty-printers with GDB (globally by default)."""
    gdb.printing.register_pretty_printer(objfile, build_pretty_printer(), replace=True)
