#!/usr/bin/env python3
"""Une el código recompilado de todos los módulos para el host de validación.

  merge_dispatch.py <salida_dir>

native32 busca en una sola tabla ordenada (recomp_dispatch_table). Cada módulo se
recompila con su prefijo, así que aquí se genera una tabla única por VA, la lista
de módulos y su orden de carga, que se comprueba contra los imports reales:
quien importa debe ir después de lo que importa (el orden en que corre DllMain).

La salida deriva de los binarios del juego: va en work/pc (ignorado por Git).
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main(out):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    doc = json.load(open(ROOT / "config" / "pc" / "modules.json", encoding="utf-8"))
    mods = doc["modules"]
    inv = {m["file"]: m for m in json.load(open(ROOT / "work" / "pc" / "pe_inventory.json", encoding="utf-8"))["modules"]}
    names = {pathlib.PurePosixPath(m["file"]).name.lower(): i for i, m in enumerate(mods)}

    # MaxPayne2.exe va el último: su entrada corre cuando todo lo demás está listo.
    order = [m for m in mods if m["tag"] != "exe"] + [m for m in mods if m["tag"] == "exe"]
    position = {pathlib.PurePosixPath(m["file"]).name.lower(): i for i, m in enumerate(order)}
    for m in order:
        me = position[pathlib.PurePosixPath(m["file"]).name.lower()]
        for dll in inv[m["file"]]["imports"]:
            dep = position.get(dll.lower())
            if dep is not None and dep >= me:
                sys.exit(f"orden inválido: {m['file']} importa {dll}, que se carga después")

    # Lo que el EXE arrastra por imports estáticos; el resto se carga con LoadLibrary.
    static, pending = set(), ["maxpayne2.exe"]
    while pending:
        name = pending.pop()
        if name in static:
            continue
        static.add(name)
        file = mods[names[name]]["file"]
        pending += [d.lower() for d in inv[file]["imports"] if d.lower() in names]

    entries = {}
    for m in mods:
        gen = ROOT / "work" / "pc" / "gen" / m["tag"]
        cat = json.load(open(ROOT / "work" / "pc" / "modules" / m["tag"] / "catalog.json"))
        for f in cat["functions"]:
            entries[f["address"]] = m["tag"]
        if not (gen / f"{m['prefix']}recomp_funcs.h").exists():
            sys.exit(f"falta el lift de {m['tag']}")

    with open(out / "mp2_dispatch.c", "w", newline="\n") as fh:
        fh.write('/* GENERADO por merge_dispatch.py. NO EDITAR, NO SUBIR. */\n#include "recomp_types.h"\n\n')
        for va in sorted(entries):
            fh.write(f"void sub_{va:08X}(void);\n")
        fh.write("\nconst recomp_dispatch_entry_t recomp_dispatch_table[] = {\n")
        for va in sorted(entries):
            fh.write(f"    {{ 0x{va:08X}u, sub_{va:08X} }},\n")
        fh.write(f"}};\nconst uint32_t recomp_dispatch_count = {len(entries)};\n")

    with open(out / "mp2_modules_gen.h", "w", newline="\n") as fh:
        fh.write("/* GENERADO por merge_dispatch.py. NO EDITAR, NO SUBIR. */\n")
        fh.write("typedef struct { const char* name; uint32_t base, size, entry; int is_static; } mp2_module_t;\n")
        fh.write(f"#define MP2_GUEST_END 0x{doc['guest_end']:08X}u\n")
        fh.write("static const mp2_module_t g_mp2_modules[] = {\n")
        for m in order:
            name = pathlib.PurePosixPath(m["file"]).name
            fh.write(f'    {{ "{name}", 0x{m["guest_base"]:08X}u, 0x{m["size_of_image"]:X}u, '
                     f'0x{m["guest_base"] + m["entry_rva"]:08X}u, {int(name.lower() in static)} }},\n')
        fh.write("};\n")
    dynamic = sorted(set(names) - static)
    print(f"{len(entries)} funciones en {len(mods)} módulos; estáticos {len(static)}; dinámicos {dynamic}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
