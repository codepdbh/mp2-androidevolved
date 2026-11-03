#!/usr/bin/env python3
"""Genera docs/PC_GAME_BINARY_REPORT.md y docs/MODULE_DEPENDENCIES.md desde el inventario.

  write_reports.py [work/pc/pe_inventory.json]

Solo escribe metadatos (hashes, cabeceras, nombres de imports y exports contados).
"""
import collections, json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
NATIVE_ORDER = ["KERNEL32.DLL", "USER32.DLL", "GDI32.DLL", "ADVAPI32.DLL", "WINMM.DLL", "DINPUT.DLL", "DSOUND.DLL",
                "EAX.DLL", "OLE32.DLL", "OLEAUT32.DLL", "SHELL32.DLL", "WSOCK32.DLL"]


def short(name):
    return name.rsplit("/", 1)[-1]


def main(path):
    inv = json.load(open(path, encoding="utf-8"))
    layout = json.load(open(ROOT / "config" / "pc" / "modules.json", encoding="utf-8"))
    mods = {m["file"]: m for m in inv["modules"]}
    guest = {short(m["file"]).lower(): m for m in layout["modules"]}

    out = ["# Informe de binarios de PC de Max Payne 2", "",
           "Generado por `scripts/pc_analysis/write_reports.py` desde la copia local; no contiene código del juego.", "",
           "## Identidad de la versión", ""]
    exe = mods["MaxPayne2.exe"]
    out += [f"- `MaxPayne2.exe`: SHA-256 `{exe['sha256']}`, {exe['size']} bytes.",
            f"- Recurso de versión: FileVersion `{exe['version'].get('FileVersion')}`, ProductVersion `{exe['version'].get('ProductVersion')}`, compañía `{exe['version'].get('CompanyName')}`.",
            f"- Marca de tiempo PE `{exe['timestamp']:#x}` (17 de octubre de 2003). Cadena de compilación embebida: `Fri Oct 17 16:45:00 2003`.",
            "- Las DLL de Remedy tienen marcas `0x3f8fdc8e`–`0x3f8fde13` (el mismo día): no hay mezcla de versiones.",
            "- **v1.01:** el título de la ventana que crea el propio juego es `Max Payne 2 v1.01`.", "",
            "## Archivos de terceros en la instalación", "",
            "- `d3d9.dll` (cargador ASI) y `scripts/MaxPayne2.WidescreenFix.asi` son de terceros, de 2016: no se recompilan.",
            "",
            "## Módulos", "",
            "| Archivo | Tamaño | Base preferida | SizeOfImage | Entrada | Relocs | Exports | TLS | Enlazador |",
            "|---|---:|---|---:|---|---:|---:|---|---|"]
    for name in sorted(mods, key=str.lower):
        m = mods[name]
        relocs = "sin (.reloc eliminado)" if m["relocs_stripped"] else m["relocations"]["highlow"]
        out.append(f"| `{name}` | {m['size']} | `{m['image_base']:#010x}` | `{m['size_of_image']:#x}` | `{m['entry_rva']:#x}` "
                   f"({m['entry_section']}) | {relocs} | {len(m['exports'])} | {'sí' if m['has_tls'] else 'no'} | {m['linker']} |")
    out += ["", "## SHA-256", "", "| Archivo | SHA-256 |", "|---|---|"]
    out += [f"| `{n}` | `{mods[n]['sha256']}` |" for n in sorted(mods, key=str.lower)]
    out += ["", "## Secciones de los módulos del juego", ""]
    for name in sorted(mods, key=str.lower):
        if short(name).lower() not in guest:
            continue
        secs = ", ".join(f"{s['name']} ({s['vsize']:#x}, H={s['entropy']})" for s in mods[name]["sections"])
        out.append(f"- `{name}`: {secs}")
    (ROOT / "docs" / "PC_GAME_BINARY_REPORT.md").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")

    dep = ["# Dependencias entre módulos de Max Payne 2 (PC)", "",
           "Generado por `scripts/pc_analysis/write_reports.py` a partir de las tablas de imports reales.", "",
           "## Disposición de memoria invitada", "",
           "Todas las DLL del motor piden `0x10000000`; cada una se recompila y se mapea en su propia base fija (`config/pc/modules.json`).", "",
           "| Base invitada | SizeOfImage | Módulo |", "|---|---:|---|"]
    dep += [f"| `{m['guest_base']:#010x}` | `{m['size_of_image']:#x}` | `{m['file']}` |" for m in layout["modules"]]
    dep += ["", "Módulos que no se recompilan:", ""]
    dep += [f"- `{k}`: {v}" for k, v in layout["excluded"].items()]
    dep += ["", "## Imports entre módulos", "",
            "`N` son imports por nombre (C++ decorado) y `#N` por ordinal. El orden de esta tabla es el orden de DllMain, comprobado por `merge_dispatch.py`.", ""]
    rows, edges, surface = [], [], collections.defaultdict(lambda: collections.Counter())
    for m in layout["modules"]:
        info = mods[m["file"]]
        parts = []
        for dll, imp in info["imports"].items():
            count = len(imp["names"]) + len(imp["ordinals"])
            if dll.lower() in guest:
                parts.append(f"`{short(guest[dll.lower()]['file'])}` {len(imp['names'])}" + (f" #{len(imp['ordinals'])}" if imp["ordinals"] else ""))
                edges.append((short(m["file"]), short(guest[dll.lower()]["file"]), count))
            else:
                surface[dll.upper()][short(m["file"])] = count
        rows.append(f"- **{short(m['file'])}** → " + (", ".join(parts) if parts else "(nada del juego)"))
    dep += rows
    dep += ["", "## Grafo (capa de motor, sin CRT ni MFC)", "", "```mermaid", "graph TD"]
    runtime = {"msvcr71.dll", "msvcp71.dll", "mfc71.dll"}
    for src, dst, _ in edges:
        if dst.lower() not in runtime:
            dep.append(f"  {src.replace('.', '_')} --> {dst.replace('.', '_')}")
    dep += ["```", "", "## Superficie nativa total", "",
            "APIs fuera del juego que importan los módulos recompilados. Sin `mfc71.dll`, que por sí sola trae casi toda la superficie de USER32, GDI32, OLE y WinINet.", "",
            "| DLL del sistema | Importadores |", "|---|---|"]
    for dll in sorted(surface):
        dep.append(f"| `{dll}` | " + ", ".join(f"{k} ({v})" for k, v in surface[dll].items()) + " |")
    dep += ["", "## Carga dinámica observada en las cadenas", "",
            "- `e2mfc.dll` busca `e2driver\\*_driver_mfc.dll` y `*_cpu_mfc.dll` y usa `LoadLibrary`; aquí solo existe `e2_d3d8_driver_mfc.dll` y ningún `*_cpu_mfc.dll`.",
            "- `e2_d3d8_driver_mfc.dll` carga `D3D8.DLL` y resuelve `Direct3DCreate8` en ejecución: `d3d8.dll` no aparece como import estático. D3DX va enlazado estáticamente en el driver.",
            "- `MaxPayne2.exe` contiene `D3D9.DLL` y exige un adaptador compatible con DirectX 9: pendiente de ver qué consulta hace.",
            "- `sndmfc.dll` importa `DSOUND.dll` ordinal 2 y `EAX.DLL` ordinal 5.",
            "- `X_Inputmfc.dll` importa `DirectInputCreateA` y `SetWindowsHookExA`, y lee `input.txt`.", ""]
    (ROOT / "docs" / "MODULE_DEPENDENCIES.md").write_text("\n".join(dep) + "\n", encoding="utf-8", newline="\n")
    print("informes escritos")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ROOT / "work" / "pc" / "pe_inventory.json")
