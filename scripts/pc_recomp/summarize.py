#!/usr/bin/env python3
"""Resume en Markdown las estadísticas de lift y las pruebas diferenciales ya ejecutadas."""
import collections, json, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    mods = json.load(open(ROOT / "config" / "pc" / "modules.json", encoding="utf-8"))["modules"]
    total = collections.Counter()
    print("| Módulo | Funciones | Líneas C | Fallos de lift | Instr. sin implementar | Excluidas (datos) |")
    print("|---|---:|---:|---:|---:|---:|")
    for m in mods:
        s = json.load(open(ROOT / "work" / "pc" / "gen" / m["tag"] / f"{m['prefix']}lift_stats.json"))
        print(f"| `{m['file']}` | {s['functions']} | {s['c_lines']} | {s['lift_failures']} | {s['unimplemented_total']} | {s['excluded']} |")
        for k in ("functions", "c_lines", "lift_failures", "unimplemented_total", "excluded", "icall_sites", "itail_sites"):
            total[k] += s[k]
    print(f"| **Total** | {total['functions']} | {total['c_lines']} | {total['lift_failures']} | {total['unimplemented_total']} | {total['excluded']} |")
    print(f"\nLlamadas indirectas {total['icall_sites']}, saltos indirectos {total['itail_sites']}.\n")

    print("| Módulo | Funciones | Casos | Coinciden | Fallan | Omitidos | Clases de fallo |")
    print("|---|---:|---:|---:|---:|---:|---|")
    sums = collections.Counter()
    for report in sorted((ROOT / "work" / "pc" / "difftest").glob("*/report.json")):
        r = json.load(open(report))
        kinds = collections.Counter(str(w)[:40] for f in r["failures"] for w in f["why"])
        print(f"| `{report.parent.name}` | {r['functions']} | {r['tested']} | {r['passed']} | {r['failed']} | {r['skipped']} | "
              + "; ".join(f"{k} ×{v}" for k, v in kinds.items()) + " |")
        for k in ("functions", "tested", "passed", "failed", "skipped"):
            sums[k] += r[k]
    print(f"| **Total** | {sums['functions']} | {sums['tested']} | {sums['passed']} | {sums['failed']} | {sums['skipped']} | |")


if __name__ == "__main__":
    main()
