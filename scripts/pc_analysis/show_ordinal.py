#!/usr/bin/env python3
"""Muestra el código original y el C recompilado de un export.

  show_ordinal.py <dll> <ordinal|nombre> [instrucciones]
"""
import json, pathlib, re, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main(dll, key, count):
    pe = pefile.PE(str(ROOT / "work" / "pc" / "guest" / dll))
    base = pe.OPTIONAL_HEADER.ImageBase
    img = bytes(pe.get_memory_mapped_image())
    va = None
    for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if str(e.ordinal) == key or (e.name and e.name.decode() == key):
            va = base + e.address
    print(f"{dll} {key} -> {va:#x}")
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    for n, i in enumerate(md.disasm(img[va - base:va - base + count * 15], va)):
        if n >= count:
            break
        print(f"{i.address:08X} {i.mnemonic} {i.op_str}")
    tag = dll.rsplit(".", 1)[0].lower()
    head = f"void sub_{va:08X}(void)"
    for path in (ROOT / "work" / "pc" / "gen" / tag).glob("*recomp_[0-9]*.c"):
        text = path.read_text(encoding="utf-8", errors="replace")
        at = text.find(head)
        if at >= 0:
            end = text.find("\nvoid sub_", at + 10)
            print(text[at:end][:6000])
            break


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 30)
