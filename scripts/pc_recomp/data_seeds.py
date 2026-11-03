#!/usr/bin/env python3
"""Function seeds from ALIGNED code pointers in the data sections of Speed.exe.

  data_seeds.py <analysis.exe> <out.json> [--merge other_seeds.json ...]

Speed.exe (NFSU1 1.4) keeps no relocations and no RTTI, so pcrecomp's catalog
finds data-only functions with its unaligned byte scan alone. That scan also
"finds" starts in inter-function padding (0x694793: `00 00 a3 ...` decodes into
the next function and reaches its `ret`), and such a bogus start swallows the
real function after it: the CRT initializer 0x6947A0, referenced only from the
_initterm table at 0x6D4458, was then rejected as interior and the boot stopped
at "ICALL to unlifted guest address 0x006947A0".

Compilers lay function-pointer tables (initializer lists, callbacks, vtables)
out as aligned dwords, so an aligned dword in .rdata/.data that lands in .text
and decodes as a function body (pcrecomp's probes_as_function_body) is strong
evidence of a real entry. These are passed as explicit seeds, which the
catalog enters even when a bogus neighbour already covers the address.
"""
import json, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PCRECOMP = os.environ.get("PCRECOMP", os.path.normpath(os.path.join(HERE, "..", "..", "external", "pcrecomp")))
for sub in ("pe", "disasm"):
    sys.path.insert(0, os.path.join(PCRECOMP, "tools", sub))
from pe_analyze import analyze_pe   # noqa: E402
import disasm32                     # noqa: E402


def main(argv):
    exe, out = argv[0], argv[1]
    merge = argv[argv.index("--merge") + 1:] if "--merge" in argv else []
    info = analyze_pe(exe)
    data = open(exe, "rb").read()
    dis = disasm32.Disassembler(data, info.image_base, info.sections)
    found, probed = set(), 0
    for s in info.sections:
        if s.is_code:
            continue
        base = info.image_base + s.virtual_address
        blob = dis.read_bytes(base, s.raw_size) or b""
        for off in range(0, len(blob) - 3, 4):
            va = struct.unpack_from("<I", blob, off)[0]
            if info.code_start <= va < info.code_end and va not in found:
                probed += 1
                if dis.probes_as_function_body(va):
                    found.add(va)
    seeds = [{"address": va, "note": "aligned data pointer"} for va in sorted(found)]
    known = {va for va in found}
    for path in merge:
        for e in json.load(open(path)):
            a = e.get("address") if isinstance(e, dict) else e
            a = int(a, 0) if isinstance(a, str) else a
            if a not in known:
                known.add(a)
                seeds.append(e if isinstance(e, dict) else {"address": a})
    json.dump(seeds, open(out, "w"), indent=1)
    print(f"[*] {probed} aligned code pointers probed, {len(found)} decode as code; "
          f"{len(seeds)} seeds with merges -> {out}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1:])
