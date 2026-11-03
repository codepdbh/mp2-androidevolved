#!/usr/bin/env python3
"""Classify the .text bytes the function catalog does not cover.

Usage: coverage_gaps.py <analysis.exe> <functions.json> [relocs.txt]
Classes: padding (int3/nop/zero runs), reloc-data (a reloc slot inside the gap,
i.e. pointers/tables in .text), code-like (decodes cleanly and ends in ret/jmp),
unknown.
"""
import sys, json, collections, pefile, capstone
exe, cat = sys.argv[1], json.load(open(sys.argv[2]))
relocs = set(int(x, 16) for x in open(sys.argv[3]).read().split()) if len(sys.argv) > 3 else set()
pe = pefile.PE(exe, fast_load=True); B = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
t = [s for s in pe.sections if s.Name.startswith(b".text")][0]
lo0, hi0 = B + t.VirtualAddress, B + t.VirtualAddress + t.Misc_VirtualSize
iv = sorted((f["address"], f["end"]) for f in cat["functions"] if f["end"] > f["address"])
gaps, cur = [], lo0
for a, e in iv:
    if a > cur: gaps.append((cur, a))
    cur = max(cur, e)
if cur < hi0: gaps.append((cur, hi0))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
cls = collections.Counter(); nb = collections.Counter(); samples = collections.defaultdict(list)
rl = sorted(relocs)
import bisect
for a, e in gaps:
    b = img[a - B:e - B]
    if all(x in (0xCC, 0x90, 0x00) for x in b): k = "padding"
    elif relocs and bisect.bisect_left(rl, a) < len(rl) and rl[bisect.bisect_left(rl, a)] < e: k = "reloc-data (pointer tables in .text)"
    else:
        ins = list(md.disasm(b, a))
        dec = sum(i.size for i in ins)
        last = ins[-1].mnemonic if ins else ""
        if dec == len(b) and last in ("ret", "jmp"): k = "code-like (unreached)"
        elif dec == len(b) and b.rstrip(b"\xcc\x90") != b: k = "code-like (unreached)"
        else: k = "unknown/data"
    cls[k] += 1; nb[k] += e - a
    if len(samples[k]) < 8: samples[k].append("%08X+%d" % (a, e - a))
tot = hi0 - lo0
print("gaps", len(gaps), "bytes", sum(e - a for a, e in gaps), "of", tot)
for k in cls: print("  %-40s %6d gaps %9d bytes  e.g. %s" % (k, cls[k], nb[k], " ".join(samples[k][:5])))
json.dump({"gaps": [[a, e] for a, e in gaps], "classes": {k: [cls[k], nb[k]] for k in cls}}, open("work/coverage_gaps.json", "w"))
