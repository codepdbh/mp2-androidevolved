#!/usr/bin/env python3
"""Explica por qué una dirección no está en el catálogo de su módulo.

  why_uncatalogued.py <VA> [VA ...]
"""
import json, pathlib, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main(vas):
    mods = json.load(open(ROOT / "config" / "pc" / "modules.json", encoding="utf-8"))["modules"]
    for va in vas:
        mod = next(m for m in mods if m["guest_base"] <= va < m["guest_base"] + m["size_of_image"])
        work = ROOT / "work" / "pc" / "modules" / mod["tag"]
        cat = json.load(open(work / "catalog.json"))["functions"]
        seeds = {e["address"] for e in json.load(open(work / "seeds.json"))} if (work / "seeds.json").exists() else set()
        probed = {e["address"] for e in json.load(open(work / "seeds_probed.json"))} if (work / "seeds_probed.json").exists() else set()
        print(f"{va:#x} en {mod['file']}: catálogo={any(f['address'] == va for f in cat)} semilla={va in seeds} tras_filtro={va in probed}")
        for f in cat:
            for s, e, *_ in f["blocks"]:
                if s <= va < e:
                    print(f"  dentro del bloque {s:#x}-{e:#x} de la función {f['address']:#x} ({f['entry_kind']})")
        pe = pefile.PE(str(ROOT / "work" / "pc" / "guest" / pathlib.PurePosixPath(mod["file"]).name), fast_load=True)
        base = pe.OPTIONAL_HEADER.ImageBase
        img = bytes(pe.get_memory_mapped_image())
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        before = sorted(f["address"] for f in cat if f["address"] < va)[-3:]
        print(f"  funciones previas: {[hex(a) for a in before]}")
        print("  " + " ; ".join(f"{i.mnemonic} {i.op_str}" for i in list(md.disasm(img[va - base:va - base + 48], va))[:9]))


if __name__ == "__main__":
    main([int(a, 16) for a in sys.argv[1:]])
