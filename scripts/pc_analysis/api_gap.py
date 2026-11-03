#!/usr/bin/env python3
"""Compara la API que MP2 llama de verdad con la que ya implementa el runtime de Carbon.

  api_gap.py <api_usage.txt> <dir con guest_*.inc>

Una función cuenta como implementada si su nombre aparece entre comillas en las
tablas de imports del runtime. Es un indicio de partida, no una prueba de que su
comportamiento sirva para MP2.
"""
import collections, pathlib, re, sys


def main(usage, runtime):
    known = set()
    for path in pathlib.Path(runtime).glob("*"):
        if path.suffix in (".inc", ".cpp", ".h"):
            known |= set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]{2,})"', path.read_text(encoding="utf-8", errors="replace")))
    groups = collections.defaultdict(list)
    for line in open(usage, encoding="utf-8"):
        parts = line.split()
        if len(parts) < 2:
            continue
        name, calls = " ".join(parts[:-1]), int(parts[-1])
        if "!" in name:
            dll, func = name.split("!", 1)
        else:
            dll, func = name.split("+")[0], name
        groups[dll.lower()].append((func, calls))
    total = missing_total = 0
    for dll in sorted(groups):
        rows = sorted(groups[dll])
        missing = [(f, c) for f, c in rows if f not in known]
        total += len(rows)
        missing_total += len(missing)
        print(f"\n## {dll}: {len(rows)} usadas, {len(missing)} sin implementar en Carbon")
        for func, calls in missing:
            print(f"  {func:50} {calls}")
    print(f"\nTOTAL: {total} usadas, {missing_total} sin implementar")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
