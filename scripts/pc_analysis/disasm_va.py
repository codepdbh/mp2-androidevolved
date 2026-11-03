#!/usr/bin/env python3
"""Desensambla direcciones invitadas de MP2 y anota exports e imports.

  disasm_va.py <VA> [instrucciones]      (la VA decide el módulo, por config/pc/modules.json)
"""
import json, pathlib, re, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main(va, count):
    mods = json.load(open(ROOT / "config" / "pc" / "modules.json", encoding="utf-8"))["modules"]
    mod = next(m for m in mods if m["guest_base"] <= va < m["guest_base"] + m["size_of_image"])
    pe = pefile.PE(str(ROOT / "work" / "pc" / "guest" / pathlib.PurePosixPath(mod["file"]).name))
    base = pe.OPTIONAL_HEADER.ImageBase
    img = bytes(pe.get_memory_mapped_image())
    names = {}
    for e in getattr(getattr(pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", []):
        names[base + e.address] = e.name.decode() if e.name else f"#{e.ordinal}"
    for d in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        for i in d.imports:
            names[i.address] = d.dll.decode() + "!" + (i.name.decode() if i.name else f"#{i.ordinal}")
    print(f"{mod['file']} base {base:#x}")
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    for n, ins in enumerate(md.disasm(img[va - base:va - base + count * 15], va)):
        if n >= count:
            break
        notes = [names[int(h, 16)] for h in re.findall(r"0x[0-9a-f]+", ins.op_str) if int(h, 16) in names]
        strs = []
        for h in re.findall(r"0x[0-9a-f]+", ins.op_str):
            v = int(h, 16)
            if base <= v < base + len(img):
                s = img[v - base:v - base + 60].split(b"\0")[0]
                if len(s) >= 4 and all(32 <= c < 127 for c in s):
                    strs.append(s.decode())
        print(f"{ins.address:08X} {ins.mnemonic} {ins.op_str}" + (f"   ; {notes}" if notes else "") + (f"   ; {strs}" if strs else ""))


if __name__ == "__main__":
    main(int(sys.argv[1], 16), int(sys.argv[2]) if len(sys.argv) > 2 else 40)
