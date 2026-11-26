"""Make lifted x87 memory operands byte-aligned for ARM32 (issue #7).

The lifter's x87 path emits raw float/double pointers instead of MEMF/MEMD.
An x86 object may place a double at +0x5f3; VSTR/VLDR cannot access that
address on ARM32. The runtime already supplies aligned(1) scalar aliases.
Only build copies are rewritten; original game files and generated inputs stay intact.
"""
import argparse
from pathlib import Path


def fix_text(text):
    for scalar, alias in (("double", "recomp_f64_una"), ("float", "recomp_f32_una")):
        text = text.replace(f"*({scalar}*)ADDR(", f"*({alias}*)ADDR(")
    return text


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = fix_text(args.source.read_text(encoding="utf-8"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != result:
        args.output.write_text(result, encoding="utf-8")
