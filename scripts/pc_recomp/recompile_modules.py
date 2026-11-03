#!/usr/bin/env python3
"""Recompila los módulos de MP2 con el flujo de NFS Carbon (prepare_modules.py).

  recompile_modules.py [--jobs N] [--game DIR] [--phase catalog|lift|all] [tag ...]

Para cada DLL: copia relocalizada a su base invitada (work/pc/guest/<nombre>),
semillas (entrada, exports, punteros relocalizados que decodifican como código),
catálogo, filtro de falsos arranques y lift a work/pc/gen/<tag>. MaxPayne2.exe no
tiene relocalizaciones: sus semillas salen de punteros alineados en datos y de
inmediatos en código (data_seeds.py + code_seeds.py).

Todo lo que se escribe deriva de binarios comerciales y queda en work/ (ignorado
por Git). Los originales no se escriben nunca.
"""
import argparse, concurrent.futures as cf, json, os, pathlib, re, shutil, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
PCRECOMP = pathlib.Path(os.environ.get("PCRECOMP", ROOT / "external" / "pcrecomp"))
for sub in ("pe", "disasm"):
    sys.path.insert(0, str(PCRECOMP / "tools" / sub))

# Instrucciones que MSVC no emite en código Win32: delatan datos decodificados (Carbon).
NOT_CODE = {"arpl", "insb", "insd", "insw", "outsw", "outsb", "outsd", "bound", "daa", "das", "aaa", "aas",
            "aam", "aad", "popaw", "pushaw", "sldt", "lcall", "ljmp", "into", "salc", "hlt", "cli", "sti",
            "iretd", "int1", "rdmsr", "str", "les", "lds", "rsm", "lldt", "lsl", "bnd",
            "loop", "loopne", "loope", "in", "out"}


def run(script, *args, log=None):
    cmd = [sys.executable, str(HERE / script), *(str(a) for a in args)]
    with open(log, "a", encoding="utf-8") as out:
        out.write("$ " + " ".join(cmd[1:]) + "\n")
        out.flush()
        subprocess.run(cmd, cwd=ROOT, check=True, stdout=out, stderr=subprocess.STDOUT)


def relocation_sites(pe):
    """VA de cada puntero absoluto que el enlazador marcó en la imagen."""
    base = pe.OPTIONAL_HEADER.ImageBase
    pe.parse_data_directories([pefile_dir("IMAGE_DIRECTORY_ENTRY_BASERELOC")])
    return {base + e.rva for b in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []) for e in b.entries if e.type == 3}


def pefile_dir(name):
    import pefile
    return pefile.DIRECTORY_ENTRY[name]


def respects_relocations(ins, sites):
    """Una relocalización dentro de una instrucción debe ser su disp32 o su imm32.

    Si no lo es, esos cuatro bytes son un puntero leído como código: datos.
    """
    overlapping = [s for s in range(ins.address - 3, ins.address + ins.size) if s in sites]
    if not overlapping:
        return True
    operands = set()
    if ins.disp_size == 4:
        operands.add(ins.address + ins.disp_offset)
    if ins.imm_size == 4:
        operands.add(ins.address + ins.imm_offset)
    return all(s in operands for s in overlapping)


def probe_seeds(image, seeds_path, out_path):
    from pe_analyze import analyze_pe
    from disasm32 import Disassembler
    import pefile
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    info = analyze_pe(str(image))
    raw = open(image, "rb").read()
    dis = Disassembler(raw, info.image_base, info.sections)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    sites = relocation_sites(pefile.PE(str(image), fast_load=True))

    def decodes_as_msvc(va):
        # Hasta el primer ret o salto incondicional. mfc71 guarda datos en .text
        # y un puntero a ellos que pasa la sonda tapa la función real que sigue:
        # 0x6EA8CC (loopne...) cubría el inicializador 0x6EA8DC.
        window = dis.read_bytes(va, 256) or b""
        zeros = 0
        for ins in md.disasm(window, va):
            zeros += ins.bytes[:2] == b"\x00\x00"
            if ins.mnemonic.split()[-1] in NOT_CODE or zeros >= 2 or not respects_relocations(ins, sites):
                return False
            if ins.mnemonic in ("ret", "retf", "jmp"):
                break
        return True

    seeds = json.load(open(seeds_path))
    keep = [e for e in seeds if not e["note"].startswith("relocated")
            or (dis.probes_as_function_body(e["address"]) and decodes_as_msvc(e["address"]))]
    json.dump(keep, open(out_path, "w"), indent=1)
    return len(seeds), len(keep)


def not_code(image, catalog_path, base, out_path):
    """Falsos arranques: se recompilan como RECOMP_UNIMPL.

    Solo se mira el bloque de entrada. Examinar todos los bloques, como hace
    Carbon, condenaba funciones reales con un resto de datos decodificados al
    final: R_Script::parse (rlmfc), que importan cinco módulos. Una instrucción
    sospechosa en un bloque interior ya falla por sí sola al lifarla. Un export
    dentro de código tampoco es dato: _inp/_outp de msvcr71 usan in/out de verdad.
    """
    import pefile
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    pe = pefile.PE(str(image))
    img = pe.get_memory_mapped_image()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    sites = relocation_sites(pe)
    exports = {base + e.address for e in getattr(getattr(pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", [])}
    bad = []
    for f in json.load(open(catalog_path))["functions"]:
        if f["address"] in exports:
            continue
        entry = next((b for b in f["blocks"] if b[0] == f["address"]), None)
        if entry is None:
            continue
        zeros = 0
        for ins in md.disasm(img[entry[0] - base:entry[1] - base], entry[0]):
            zeros += ins.bytes[:2] == b"\x00\x00"
            if ins.mnemonic.split()[-1] in NOT_CODE or zeros >= 2 or not respects_relocations(ins, sites):
                bad.append(f["address"])
                break
    json.dump(bad, open(out_path, "w"))
    return len(bad)


def process_dll(mod, game, phase):
    tag, base, prefix = mod["tag"], mod["guest_base"], mod["prefix"]
    work = ROOT / "work" / "pc" / "modules" / tag
    guest = ROOT / "work" / "pc" / "guest"
    work.mkdir(parents=True, exist_ok=True)
    guest.mkdir(parents=True, exist_ok=True)
    log = work / "pipeline.log"
    image = guest / pathlib.PurePosixPath(mod["file"]).name
    catalog, data = work / "catalog.json", work / "not_code.json"
    t = time.time()
    if phase in ("catalog", "all"):
        log.write_text("", encoding="utf-8")
        seeds, probed = work / "seeds.json", work / "seeds_probed.json"
        run("prepare_module.py", game / mod["file"], hex(base), image, seeds, log=log)
        probe_seeds(image, seeds, probed)
        run("run_lift.py", "catalog", image, probed, catalog, "--seeds-only", log=log)
        not_code(image, catalog, base, data)
    if phase in ("lift", "all"):
        gen = ROOT / "work" / "pc" / "gen" / tag
        lift_args = ("lift", image, catalog, gen, "--prefix", prefix, "--exclude", data)
        run("run_lift.py", *lift_args, log=log)
        broken = undefined_label_functions(gen)
        if broken:
            excluded = json.load(open(data))
            json.dump(sorted(set(excluded) | broken), open(data, "w"))
            with open(log, "a", encoding="utf-8") as out:
                out.write(f"[*] {len(broken)} funciones con saltos a etiquetas inexistentes: excluidas\n")
            run("run_lift.py", *lift_args, log=log)
            still = undefined_label_functions(gen)
            if still:
                return f"{tag}: ERROR etiquetas sin definir tras excluir: {sorted(hex(a) for a in still)[:10]}"
    return f"{tag}: ok en {time.time() - t:.0f}s"


def undefined_label_functions(gen):
    """Funciones cuyo C no compila: saltan a una etiqueta que no existe.

    Pasa cuando se decodifican datos (mfc71 guarda sus datos dentro de .text): el
    salto cae en mitad de otra instrucción. Se recompilan como RECOMP_UNIMPL, así
    que si fueran código real fallarían de forma ruidosa al ejecutarse.
    """
    broken = set()
    for path in gen.glob("*recomp_[0-9]*.c"):
        text = path.read_text(encoding="utf-8", errors="replace")
        for chunk in re.split(r"(?m)^(?=void sub_[0-9A-F]{8}\(void\) \{)", text)[1:]:
            labels = set(re.findall(r"(?m)^(L_[0-9A-F]+):", chunk))
            if set(re.findall(r"goto (L_[0-9A-F]+);", chunk)) - labels:
                broken.add(int(chunk[9:17], 16))
    return broken


def process_exe(mod, game, phase):
    work = ROOT / "work" / "pc" / "modules" / "exe"
    guest = ROOT / "work" / "pc" / "guest"
    work.mkdir(parents=True, exist_ok=True)
    guest.mkdir(parents=True, exist_ok=True)
    log = work / "pipeline.log"
    image = guest / "MaxPayne2.exe"
    catalog, t = work / "catalog.json", time.time()
    if phase in ("catalog", "all"):
        log.write_text("", encoding="utf-8")
        shutil.copyfile(game / mod["file"], image)
        data_seeds, code_seeds = work / "seeds_data.json", work / "seeds_code.json"
        run("data_seeds.py", image, data_seeds, log=log)
        run("run_lift.py", "catalog", image, data_seeds, catalog, log=log)
        run("code_seeds.py", image, catalog, code_seeds, "--merge", data_seeds, log=log)
        run("run_lift.py", "catalog", image, code_seeds, catalog, log=log)
    if phase in ("lift", "all"):
        run("run_lift.py", "lift", image, catalog, ROOT / "work" / "pc" / "gen" / "exe", log=log)
    return f"exe: ok en {time.time() - t:.0f}s"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--game", type=pathlib.Path, default=ROOT / "Max Payne 2 The Fall of Max Payne")
    p.add_argument("--phase", choices=("catalog", "lift", "all"), default="all")
    p.add_argument("tags", nargs="*")
    a = p.parse_args()
    mods = json.load(open(ROOT / "config" / "pc" / "modules.json", encoding="utf-8"))["modules"]
    unknown = set(a.tags) - {m["tag"] for m in mods}
    if unknown:
        sys.exit(f"módulos desconocidos: {sorted(unknown)}")
    todo = [m for m in mods if not a.tags or m["tag"] in a.tags]
    # Primero los pequeños: un fallo de la herramienta aparece enseguida.
    todo.sort(key=lambda m: m["size_of_image"])
    failed = 0
    with cf.ThreadPoolExecutor(a.jobs) as pool:
        jobs = {pool.submit(process_exe if m["tag"] == "exe" else process_dll, m, a.game, a.phase): m["tag"] for m in todo}
        for job in cf.as_completed(jobs):
            try:
                print(job.result(), flush=True)
            except subprocess.CalledProcessError as err:
                failed += 1
                print(f"{jobs[job]}: FALLO ({err}); ver work/pc/modules/{jobs[job]}/pipeline.log", flush=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
