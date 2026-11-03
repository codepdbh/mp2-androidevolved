#!/usr/bin/env python3
"""Reconocimiento del arranque de MaxPayne2.exe: línea de comandos, registro y diálogos.

  exe_startup_recon.py <MaxPayne2.exe> <salida.txt>

Busca cadenas y las funciones que las referencian con un inmediato. Escribe en
work/pc (ignorado por Git), porque cita cadenas y código del juego.
"""
import re, struct, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

WORDS = rb"(?i)dialog|developer|screenshot|preload|window|fullscreen|software\\|remedy|settings|display|startup|options|language|\.ini$|cd\.ini|novideo|nosound|skip"


def main(path, out):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories()
    base = pe.OPTIONAL_HEADER.ImageBase
    img = bytes(pe.get_memory_mapped_image())
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    code = img[text.VirtualAddress:text.VirtualAddress + text.Misc_VirtualSize]
    code_va = base + text.VirtualAddress
    lines = []
    strings = {}
    for m in re.finditer(rb"[\x20-\x7e]{3,}", img[text.VirtualAddress + text.Misc_VirtualSize:]):
        va = base + text.VirtualAddress + text.Misc_VirtualSize + m.start()
        strings[va] = m.group()
    hits = {va: s for va, s in strings.items() if re.search(WORDS, s) and not s.startswith((b".?", b"?"))}
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    refs = {}
    for va in hits:
        pat = struct.pack("<I", va)
        i = code.find(pat)
        while i != -1:
            refs.setdefault(va, []).append(code_va + i)
            i = code.find(pat, i + 1)
    for va, s in sorted(hits.items()):
        lines.append(f"{va:08X} refs={[hex(r) for r in refs.get(va, [])[:6]]} {s.decode('latin1')}")
    # Recursos: diálogos y sus controles.
    lines.append("--- recursos")
    root = getattr(pe, "DIRECTORY_ENTRY_RESOURCE", None)
    for typ in root.entries if root else []:
        name = pefile.RESOURCE_TYPE.get(typ.id, str(typ.name))
        ids = [str(e.id if e.id is not None else e.name) for e in typ.directory.entries]
        lines.append(f"{name}: {ids[:60]}")
        if name == "RT_DIALOG":
            for e in typ.directory.entries:
                for lang in e.directory.entries:
                    d = lang.data.struct
                    blob = img[d.OffsetToData:d.OffsetToData + d.Size]
                    text16 = re.findall(rb"(?:[\x20-\x7e]\x00){3,}", blob)
                    lines.append(f"  dialog {e.id} lang {lang.id}: " + " | ".join(t.decode('utf-16le') for t in text16))
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"{len(hits)} cadenas, {len(refs)} con referencia -> {out}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
