#!/usr/bin/env python3
"""Seeds the catalog missed that the code itself names: switch arms and code-address immediates.

  code_seeds.py <analysis.exe> <catalog.json> <out.json> [--merge seeds.json ...]

Immediates: `mov eax, 0x448AC0` (at 0x448C0B) hands a callback to an indirect
call, and nothing else names it; a bogus padding start covered it and the boot
stopped at "ICALL to unlifted guest address 0x00448AC0". Every `mov r32, imm32`
and `push imm32` in catalogued code whose value lands in .text and decodes as
a function body (pcrecomp's probe) is seeded; run_lift.py's restore step keeps
it only if no called function covers it mid-instruction.

Switch arms:

sub_005C3E7D dispatches through `jmp [eax*4 + 0x5C4C6E]`, a 288-entry table
inside .text; the catalog knew only part of it and the boot stopped at
"ITAIL to unlifted guest address 0x005C4A7F". For every such indirect jump in
every catalogued function, read the table while its entries lie between the
function start and the table, and seed each target that is not already a
block start or jump target. A seed inside a function lifts as an alias entry: the lifted switch
tail-calls it with the same registers, as the original jump would run it.
"""
import json, os, struct, sys
import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
PCRECOMP = os.environ.get("PCRECOMP", os.path.normpath(os.path.join(HERE, "..", "..", "external", "pcrecomp")))
for sub in ("pe", "disasm"):
    sys.path.insert(0, os.path.join(PCRECOMP, "tools", sub))
from pe_analyze import analyze_pe   # noqa: E402
import disasm32                     # noqa: E402


def main(argv):
    exe, catalog, out = argv[0], argv[1], argv[2]
    merge = argv[argv.index("--merge") + 1:] if "--merge" in argv else []
    data = open(exe, "rb").read()
    cat = json.load(open(catalog))
    base = cat["image_base"]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    info = analyze_pe(exe)
    dis = disasm32.Disassembler(data, info.image_base, info.sections)
    catalogued = {f["address"] for f in cat["functions"]}
    found = set()
    immediates = set()
    tables = 0
    for f in cat["functions"]:
        lo, hi = f["address"], f["end"]
        if hi <= lo or hi - lo > 0x40000:
            continue
        known = set(f["jump_targets"]) | {b[0] for b in f["blocks"]}
        for b in f["blocks"]:
            start, end = b[0], b[1]
            for ins in md.disasm(data[start - base:end - base], start):
                if ins.mnemonic in ("mov", "push") and ins.operands:
                    imm = ins.operands[-1]
                    if imm.type == capstone.x86.X86_OP_IMM and ins.size >= 5:
                        v = imm.imm & 0xFFFFFFFF
                        if info.code_start <= v < info.code_end and v not in catalogued:
                            immediates.add(v)
                if ins.mnemonic != "jmp" or not ins.operands:
                    continue
                op = ins.operands[0]
                if op.type != capstone.x86.X86_OP_MEM or op.mem.scale != 4 or not op.mem.disp or op.mem.base:
                    continue
                table = op.mem.disp & 0xFFFFFFFF
                off = table - base
                if not (0 <= off < len(data) - 4):
                    continue
                tables += 1
                # MSVC emits the table right after the function's code, so an arm
                # lies between the function start and the table. The catalogued
                # `end` is not a bound: sub_005C2A3C ends at 0x5C2AC6 there but
                # its arms reach 0x5C3951 (the boot stopped on that ITAIL).
                top = min(table, lo + 0x10000) if table > lo else hi
                for k in range(1024):
                    if off + 4 * k + 4 > len(data):
                        break
                    v = struct.unpack_from("<I", data, off + 4 * k)[0]
                    if not (lo <= v < top):
                        break
                    if v not in known:
                        found.add(v)
    probed = {v for v in immediates - found if dis.probes_as_function_body(v)}
    seeds = [{"address": v, "note": "switch arm"} for v in sorted(found)]
    seeds += [{"address": v, "note": "code immediate"} for v in sorted(probed)]
    found |= probed
    have = set(found)
    for path in merge:
        for e in json.load(open(path)):
            a = e.get("address") if isinstance(e, dict) else e
            a = int(a, 0) if isinstance(a, str) else a
            if a not in have:
                have.add(a)
                seeds.append(e if isinstance(e, dict) else {"address": a})
    json.dump(seeds, open(out, "w"), indent=1)
    print(f"[*] {tables} switch tables read; {len(found) - len(probed)} unknown arms, "
          f"{len(probed)} of {len(immediates)} code-address immediates decode as code; {len(seeds)} seeds -> {out}")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    main(sys.argv[1:])
