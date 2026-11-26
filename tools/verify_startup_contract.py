#!/usr/bin/env python3
"""Check the vendor instruction sequences on which the startup adapter relies.

Unlike the single-instruction patch-point check, this checks the argument,
result, field and branch layout surrounding the new hooks. No hash comparison.
"""
import struct
from verify_patchpoints import Module, ROOT
from elftools.elf.relocation import RelocationSection


def words(mod, symbol, offset, expected):
    syms = mod.functions(symbol)
    assert len(syms) == 1, symbol
    sym = syms[0]
    assert offset + 4 * len(expected) <= sym["st_size"], symbol
    data = mod.elf.get_section(sym["st_shndx"]).data()
    actual = struct.unpack_from("<" + "I" * len(expected), data, sym["st_value"] + offset)
    assert actual == tuple(expected), (symbol, hex(offset), [hex(w) for w in actual])
    print(f"ok {symbol}+{offset:#x} ({len(expected)} instructions)")


def call(mod, symbol, offset, target):
    sym = mod.functions(symbol)[0]
    addr = sym["st_value"] + offset
    found = []
    for sec in mod.elf.iter_sections():
        if isinstance(sec, RelocationSection) and sec["sh_info"] == sym["st_shndx"]:
            table = mod.elf.get_section(sec["sh_link"])
            for reloc in sec.iter_relocations():
                if reloc["r_offset"] == addr:
                    found.append((reloc["r_info_type"], table.get_symbol(reloc["r_info_sym"]).name))
    assert found == [(283, target)], (symbol, hex(offset), found)  # R_AARCH64_CALL26
    print(f"ok {symbol}+{offset:#x} calls {target}")


def main():
    pdm = Module(ROOT / "tests/fixtures/vendor/pd_cp_manager.ko")
    sc = Module(ROOT / "tests/fixtures/vendor/sc8551.ko")
    main_sm = "pdm_main_sm"
    # Context base and caller's destinations for all six valid bits.
    words(pdm, main_sm, 0x4c, [0xd100e353])  # X19 = work-0x38
    for offset, target in [(0x50, "charger_dev_get_vbus"),
                           (0x68, "charger_dev_cp_get_vbatt"),
                           (0x84, "charger_dev_is_enabled"),
                           (0x94, "charger_dev_is_enabled")]:
        call(pdm, main_sm, offset, target)
    words(pdm, main_sm, 0x64, [0x91082341])  # VBAT destination at pdpm+0x240
    words(pdm, main_sm, 0x7c, [0x9107bb56, 0xaa1603e1])
    words(pdm, main_sm, 0x8c, [0x9107c355, 0xaa1503e1])
    words(pdm, main_sm, 0x6f4, [0xb9421a69, 0x7100113f, 0x540003e8])
    # Both numeric comparisons and their targets, including the high-gap load.
    words(pdm, main_sm, 0xe18, [0xb942626b, 0xb9429e6c, 0xb9423e6a,
          0x1b097d6b, 0x0b0c016c, 0x6b0c015f, 0x540007cd,
          0xb942a26c, 0x0b0b018b, 0x6b0b015f, 0x54000bea])
    words(pdm, main_sm, 0xa8c, [0x71383c5f, 0xb9028e62, 0xb9029263,
          0x5400006c, 0x5281c202, 0xb9028e62])
    # Guarding the master boolean branch preserves the existing retry branch.
    words(pdm, main_sm, 0x9d0, [0x394002c1, 0x34000281, 0x394002a8, 0x34000248])
    for off in (0xac0, 0x1030, 0x1280):
        call(pdm, main_sm, off, "adapter_dev_set_cap_xm")
    # SC8551 private chip pointer offset is taken from the actual getter.
    words(sc, "ops_sc8551_get_charge_enable", 0x2c, [0x52800181])
    words(sc, "ops_sc8551_get_charge_enable", 0x34, [0xf9407013])
    call(sc, "ops_sc8551_get_charge_enable", 0x40, "regmap_read")
    words(sc, "ops_sc8551_get_charge_enable", 0x44,
          [0x394013e8, 0x2a0003f4, 0x53077d08, 0x390002a8])
    words(sc, "sc8551_dump_important_regs", 0x24, [0x910013e2, 0x52800141])
    call(sc, "sc8551_dump_important_regs", 0x38, "regmap_read")
    words(sc, "sc8551_dump_important_regs", 0x40, [0x35000060])
    words(sc, "sc8551_dump_important_regs", 0x17c, [0xb94007e3])
    print("startup binary contract verified")


if __name__ == "__main__":
    main()
