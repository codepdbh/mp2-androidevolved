#!/usr/bin/env python3
"""Inventario PE reproducible de la instalación de PC de Max Payne 2.

  pe_inventory.py <carpeta_del_juego> <salida.json>

Lee cada EXE/DLL/ASI sin modificarlo y escribe en JSON: hash, cabeceras,
secciones, imports, exports, relocalizaciones, TLS, recursos y marcadores de
empaquetado o protección. La salida deriva de archivos comerciales: va en
work/pc (ignorado por Git). Los informes de docs/ se generan desde ese JSON.
"""
import hashlib, json, math, os, sys

import pefile

MACHINES = {0x14C: "x86", 0x8664: "x64"}
# Secciones que delatan empaquetadores o DRM de la época.
SUSPECT_SECTIONS = {
    ".securom": "SecuROM", ".dsstext": "SecuROM", "stxt371": "SafeDisc", "stxt774": "SafeDisc",
    ".txt": "SafeDisc?", "UPX0": "UPX", "UPX1": "UPX", ".aspack": "ASPack", ".adata": "ASPack",
    ".petite": "Petite", ".nsp0": "NsPack", "PEC2": "PECompact", ".bind": "SteamStub",
}


def entropy(data):
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    n = len(data)
    return -sum(c / n * math.log2(c / n) for c in counts if c)


def version_info(pe):
    out = {}
    for fi in getattr(pe, "FileInfo", None) or []:
        for entry in fi:
            for table in getattr(entry, "StringTable", []) or []:
                for k, v in table.entries.items():
                    out[k.decode("latin1")] = v.decode("latin1", "replace")
    return out


def resource_summary(pe):
    out = {}
    root = getattr(pe, "DIRECTORY_ENTRY_RESOURCE", None)
    if root is None:
        return out
    for typ in root.entries:
        name = pefile.RESOURCE_TYPE.get(typ.id, str(typ.name or typ.id))
        out[name] = len(typ.directory.entries) if hasattr(typ, "directory") else 0
    return out


def inspect(path, rel):
    raw = open(path, "rb").read()
    pe = pefile.PE(data=raw, fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY[n] for n in (
            "IMAGE_DIRECTORY_ENTRY_IMPORT", "IMAGE_DIRECTORY_ENTRY_EXPORT",
            "IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT", "IMAGE_DIRECTORY_ENTRY_TLS",
            "IMAGE_DIRECTORY_ENTRY_RESOURCE", "IMAGE_DIRECTORY_ENTRY_BASERELOC",
            "IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG", "IMAGE_DIRECTORY_ENTRY_DEBUG")])
    opt = pe.OPTIONAL_HEADER
    doc = {
        "file": rel.replace("\\", "/"),
        "size": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "machine": MACHINES.get(pe.FILE_HEADER.Machine, hex(pe.FILE_HEADER.Machine)),
        "is_dll": bool(pe.FILE_HEADER.Characteristics & 0x2000),
        "relocs_stripped": bool(pe.FILE_HEADER.Characteristics & 0x0001),
        "timestamp": pe.FILE_HEADER.TimeDateStamp,
        "image_base": opt.ImageBase,
        "size_of_image": opt.SizeOfImage,
        "entry_rva": opt.AddressOfEntryPoint,
        "subsystem": opt.Subsystem,
        "linker": f"{opt.MajorLinkerVersion}.{opt.MinorLinkerVersion}",
        "dll_characteristics": opt.DllCharacteristics,
        "version": version_info(pefile.PE(data=raw)),
        "resources": resource_summary(pe),
        "has_tls": hasattr(pe, "DIRECTORY_ENTRY_TLS"),
        "has_load_config": hasattr(pe, "DIRECTORY_ENTRY_LOAD_CONFIG"),
    }
    doc["sections"] = [{
        "name": s.Name.rstrip(b"\0").decode("latin1"),
        "rva": s.VirtualAddress, "vsize": s.Misc_VirtualSize, "rawsize": s.SizeOfRawData,
        "flags": s.Characteristics,
        "exec": bool(s.Characteristics & 0x20000000),
        "write": bool(s.Characteristics & 0x80000000),
        "entropy": round(entropy(s.get_data()), 3),
    } for s in pe.sections]
    ep = opt.AddressOfEntryPoint
    doc["entry_section"] = next((s["name"] for s in doc["sections"]
                                 if s["rva"] <= ep < s["rva"] + max(s["vsize"], s["rawsize"])), None)
    doc["protection_markers"] = sorted({SUSPECT_SECTIONS[s["name"]] for s in doc["sections"]
                                        if s["name"] in SUSPECT_SECTIONS})
    doc["high_entropy_code"] = [s["name"] for s in doc["sections"] if s["exec"] and s["entropy"] > 7.2]

    imports = {}
    for attr, kind in (("DIRECTORY_ENTRY_IMPORT", "static"), ("DIRECTORY_ENTRY_DELAY_IMPORT", "delay")):
        for entry in getattr(pe, attr, []) or []:
            dll = entry.dll.decode("latin1")
            rows = imports.setdefault(dll, {"kind": kind, "names": [], "ordinals": []})
            for imp in entry.imports:
                if imp.name:
                    rows["names"].append(imp.name.decode("latin1"))
                else:
                    rows["ordinals"].append(imp.ordinal)
    doc["imports"] = imports

    exports = []
    exp = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
    if exp:
        for sym in exp.symbols:
            exports.append({"ordinal": sym.ordinal, "rva": sym.address,
                            "name": sym.name.decode("latin1") if sym.name else None,
                            "forwarder": sym.forwarder.decode("latin1") if sym.forwarder else None})
    doc["exports"] = exports

    highlow = other = 0
    for block in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []) or []:
        for e in block.entries:
            if e.type == 3:
                highlow += 1
            elif e.type != 0:
                other += 1
    doc["relocations"] = {"highlow": highlow, "other": other}

    if doc["has_tls"]:
        tls = pe.DIRECTORY_ENTRY_TLS.struct
        doc["tls"] = {"start": tls.StartAddressOfRawData, "end": tls.EndAddressOfRawData,
                      "callbacks": tls.AddressOfCallBacks, "zero_fill": tls.SizeOfZeroFill}
    return doc


def main(root, out):
    modules, errors = [], []
    for base, _, names in os.walk(root):
        for name in sorted(names):
            if not name.lower().endswith((".exe", ".dll", ".asi")):
                continue
            path = os.path.join(base, name)
            rel = os.path.relpath(path, root)
            try:
                modules.append(inspect(path, rel))
            except pefile.PEFormatError as err:
                errors.append({"file": rel, "error": str(err)})
    modules.sort(key=lambda m: m["file"].lower())
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    json.dump({"root": os.path.basename(os.path.normpath(root)), "modules": modules, "errors": errors},
              open(out, "w", encoding="utf-8"), indent=1)
    print(f"{len(modules)} módulos, {len(errors)} errores -> {out}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
