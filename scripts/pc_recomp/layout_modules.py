#!/usr/bin/env python3
"""Asigna a cada módulo de MP2 una base invitada fija y escribe config/pc/modules.json.

  layout_modules.py <pe_inventory.json> <modules.json>

Todas las DLL del motor piden 0x10000000 y el código recompilado lleva
direcciones absolutas, así que cada DLL se recompila como si estuviera cargada
en su base fija. El JSON guarda solo metadatos (hash, tamaños, bases) y es la
fuente común del pipeline, del host de Windows y del cargador de Android.
"""
import json, sys

START = 0x00600000      # MaxPayne2.exe termina en 0x56E000
ALIGN = 0x10000
# Orden de dependencias: quien importa va después de lo que importa.
GUEST = ["msvcr71.dll", "msvcp71.dll", "mfc71.dll", "rlmfc.dll", "grphmfc.dll", "e2mfc.dll",
         "T_GraphMFC.dll", "KF2MFC.dll", "PSysLibMFC.dll", "sndmfc.dll", "X_HelpersMFC.dll",
         "X_SharedDBMFC.dll", "X_MessageMFC.dll", "X_Inputmfc.dll", "X_PhysicalSimulationMFC.dll",
         "X_LevelMFC.dll", "X_LevelRuntimeMFC.dll", "X_AudioMFC.dll", "X_ModesMFC.dll",
         "X_BasicModesMFC.dll", "X_GameObjectsMFC.dll", "e2driver/binkw32.dll",
         "e2driver/e2_d3d8_driver_mfc.dll"]
EXCLUDED = {
    "BugReport.exe": "informador de errores externo; no participa en el juego",
    "SysInfo.dll": "solo la usa BugReport.exe",
    "d3d9.dll": "cargador ASI de terceros, no es parte del juego",
    "scripts/MaxPayne2.WidescreenFix.asi": "mod de terceros; se evalúa después de un arranque limpio",
    "unins000.exe": "desinstalador",
    "DirectX/dxwebsetup.exe": "instalador de DirectX",
    "eax.dll": "sndmfc importa EAX por ordinal: pendiente de decidir puente nativo o lift",
    "oleacc.dll": "dependencia de accesibilidad de mfc71; el motor no la importa",
}


def tag(name):
    return name.rsplit("/", 1)[-1].rsplit(".", 1)[0].lower()


def main(inventory, out):
    mods = {m["file"]: m for m in json.load(open(inventory, encoding="utf-8"))["modules"]}
    exe = mods["MaxPayne2.exe"]
    rows = [{"file": "MaxPayne2.exe", "tag": "exe", "prefix": "", "guest_base": exe["image_base"],
             "preferred_base": exe["image_base"], "size_of_image": exe["size_of_image"],
             "entry_rva": exe["entry_rva"], "sha256": exe["sha256"]}]
    base = START
    for name in GUEST:
        m = mods[name]
        rows.append({"file": name, "tag": tag(name), "prefix": tag(name) + "_", "guest_base": base,
                     "preferred_base": m["image_base"], "size_of_image": m["size_of_image"],
                     "entry_rva": m["entry_rva"], "sha256": m["sha256"]})
        base = (base + m["size_of_image"] + ALIGN - 1) & ~(ALIGN - 1)
    unknown = set(mods) - {r["file"] for r in rows} - set(EXCLUDED)
    if unknown:
        sys.exit(f"módulos sin clasificar: {sorted(unknown)}")
    doc = {"guest_end": base, "modules": rows, "excluded": EXCLUDED}
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(doc, fh, indent=1, ensure_ascii=False)
    print(f"{len(rows)} módulos invitados, fin del layout {base:#x}")
    for r in rows:
        print(f"  {r['guest_base']:#010x} {r['size_of_image']:#9x} {r['file']}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
