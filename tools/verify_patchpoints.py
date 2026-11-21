#!/usr/bin/env python3
"""Check every record of kernel/patchpoints.def against the vendor fixtures.

For each patch point the symbol must exist once as a function in the fixture
module, symbol+offset must lie inside the function, be 4-byte aligned, hold
exactly expected_insn and carry no relocation.

  verify_patchpoints.py [--def kernel/patchpoints.def] [--fixtures tests/fixtures/vendor]
  verify_patchpoints.py --bias 4 --expect-all-fail     # negative control
"""
import argparse
import re
import struct
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = Path(__file__).resolve().parent.parent
PP_RE = re.compile(
    r'^PP\(\s*(\w+)\s*,\s*"(\w+)"\s*,\s*"([\w.]+)"\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*,'
    r'\s*(0x[0-9a-fA-F]{1,8})\s*,\s*(\w+)\s*\)\s*$')


def parse_def(path):
    points = []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        s = line.strip()
        if not s.startswith("PP("):
            continue
        m = PP_RE.match(s)
        if not m:
            raise SystemExit(f"{path}:{n}: malformed record: {s}")
        pid, mod, sym, off, insn, handler = m.groups()
        points.append(dict(id=pid, module=mod, symbol=sym, offset=int(off, 0),
                           insn=int(insn, 16), handler=handler, line=n))
    if not points:
        raise SystemExit(f"{path}: no PP records")
    ids = [p["id"] for p in points]
    if len(set(ids)) != len(ids):
        raise SystemExit(f"{path}: duplicate ids in {ids}")
    return points


class Module:
    def __init__(self, path):
        self.path = path
        self.elf = ELFFile(open(path, "rb"))
        if self.elf["e_machine"] != "EM_AARCH64" or not self.elf.little_endian:
            raise SystemExit(f"{path}: not a little-endian AArch64 ELF")
        self.symtab = self.elf.get_section_by_name(".symtab")
        if self.symtab is None:
            raise SystemExit(f"{path}: no .symtab")

    def functions(self, name):
        return [s for s in self.symtab.iter_symbols()
                if s.name == name and s["st_info"]["type"] == "STT_FUNC"
                and isinstance(s["st_shndx"], int)]

    def relocs_at(self, shndx, start, end):
        hits = []
        for sec in self.elf.iter_sections():
            if isinstance(sec, RelocationSection) and sec["sh_info"] == shndx:
                hits += [r for r in sec.iter_relocations() if start <= r["r_offset"] < end]
        return hits


def check(point, mod, bias):
    off = point["offset"] + bias
    syms = mod.functions(point["symbol"])
    if len(syms) != 1:
        return f"{len(syms)} function symbols named {point['symbol']}", None
    sym = syms[0]
    if off % 4:
        return f"offset {off:#x} not 4-byte aligned", None
    if off + 4 > sym["st_size"]:
        return f"offset {off:#x} outside symbol size {sym['st_size']:#x}", None
    sec = mod.elf.get_section(sym["st_shndx"])
    addr = sym["st_value"] + off
    word = struct.unpack_from("<I", sec.data(), addr)[0]
    where = f"{sec.name}+{addr:#x}"
    if word != point["insn"]:
        return f"insn {word:#010x}, expected {point['insn']:#010x}", where
    rel = mod.relocs_at(sym["st_shndx"], addr, addr + 4)
    if rel:
        return f"relocation(s) at {[hex(r['r_offset']) for r in rel]}", where
    return None, where


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--def", dest="deffile", type=Path, default=ROOT / "kernel/patchpoints.def")
    ap.add_argument("--fixtures", type=Path, default=ROOT / "tests/fixtures/vendor")
    ap.add_argument("--bias", type=lambda v: int(v, 0), default=0,
                    help="add this to every offset (negative control)")
    ap.add_argument("--expect-all-fail", action="store_true",
                    help="exit 0 only if every point fails (negative control)")
    args = ap.parse_args()

    points = parse_def(args.deffile)
    mods = {}
    failed = 0
    for p in points:
        mod = mods.get(p["module"]) or mods.setdefault(p["module"], Module(args.fixtures / f"{p['module']}.ko"))
        err, where = check(p, mod, args.bias)
        name = f"{p['id']} {p['module']}:{p['symbol']}+{p['offset'] + args.bias:#x}"
        if err is None:
            print(f"ok   {name} {where} {p['insn']:#010x}")
        else:
            failed += 1
            print(f"FAIL {name} {where or '-'}: {err}")
    total = len(points)
    print(f"passed {total - failed} failed {failed} total {total}")
    if args.expect_all_fail:
        if failed != total:
            print(f"negative control: {total - failed} point(s) still passed with bias {args.bias:#x}")
            return 1
        print("negative control: every point failed as expected")
        return 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
