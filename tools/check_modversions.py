#!/usr/bin/env python3
"""Check a module's imported CRCs and vermagic against the running kernel's tables.

  check_modversions.py [--allow-missing] <module.ko> <symvers> [<symvers> ...]

Every entry of the module's __versions section must be present in one of the
symvers tables with the same CRC, and the vermagic after the release string
must equal VERMAGIC_TAIL. The kernel compares exactly these with
CONFIG_MODVERSIONS=y, so a module that passes here loads as far as symbol
versions are concerned.

--allow-missing skips imports that no table lists (used for the vendor module,
which also imports from modules we have no table for); listed ones must match.
"""
import struct
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

VERMAGIC_TAIL = "SMP preempt mod_unload modversions aarch64"
ENTRY = 64  # struct modversion_info: unsigned long crc; char name[64 - 8]


def load_tables(paths):
    table = {}
    for path in paths:
        for n, line in enumerate(Path(path).read_text().splitlines(), 1):
            if not line.strip():
                continue
            f = line.split("\t")
            if len(f) < 4:
                raise SystemExit(f"{path}:{n}: expected crc, symbol, module, export[, namespace]")
            crc, sym = int(f[0], 16), f[1]
            if sym in table and table[sym][0] != crc:
                raise SystemExit(f"{path}:{n}: {sym} has CRC {crc:#010x} here and "
                                 f"{table[sym][0]:#010x} in {table[sym][1]}")
            table[sym] = (crc, path)
    return table


def read_module(path):
    elf = ELFFile(open(path, "rb"))
    sec = elf.get_section_by_name("__versions")
    if sec is None:
        raise SystemExit(f"{path}: no __versions section (built without CONFIG_MODVERSIONS?)")
    data = sec.data()
    if len(data) % ENTRY:
        raise SystemExit(f"{path}: __versions size {len(data)} is not a multiple of {ENTRY}")
    versions = []
    for i in range(0, len(data), ENTRY):
        crc = struct.unpack_from("<Q", data, i)[0]
        name = data[i + 8:i + ENTRY].split(b"\0", 1)[0].decode()
        versions.append((name, crc))
    modinfo = elf.get_section_by_name(".modinfo")
    info = {}
    if modinfo is not None:
        for item in modinfo.data().split(b"\0"):
            if b"=" in item:
                k, v = item.decode(errors="replace").split("=", 1)
                info.setdefault(k, v)
    return versions, info


def main(argv):
    args = argv[1:]
    allow_missing = "--allow-missing" in args
    args = [a for a in args if a != "--allow-missing"]
    if len(args) < 2:
        print(__doc__)
        return 2
    ko, tables = args[0], args[1:]
    table = load_tables(tables)
    versions, info = read_module(ko)
    failed = skipped = 0
    for name, crc in versions:
        want = table.get(name)
        if want is None and allow_missing:
            skipped += 1
            print(f"skip {name}: not in any table")
        elif want is None:
            failed += 1
            print(f"FAIL {name}: CRC {crc:#010x} not in any table")
        elif want[0] != crc:
            failed += 1
            print(f"FAIL {name}: CRC {crc:#010x}, kernel has {want[0]:#010x} ({want[1]})")
        else:
            print(f"ok   {name} {crc:#010x}")
    total = len(versions) - skipped

    vermagic = info.get("vermagic", "")
    tail = vermagic.split(" ", 1)[1].strip() if " " in vermagic else ""
    total += 1
    if tail == VERMAGIC_TAIL:
        print(f"ok   vermagic '{vermagic}'")
    else:
        failed += 1
        print(f"FAIL vermagic '{vermagic}': tail must be '{VERMAGIC_TAIL}'")

    print(f"passed {total - failed} failed {failed} total {total}" + (f" skipped {skipped}" if skipped else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
