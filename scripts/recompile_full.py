"""Whole-program recompilation with an extended function map.

Fragmented Ghidra bodies whose ranges start at the entry, lie between two other
function starts and are separated only by small unreachable gaps (alignment NOPs after
unconditional branches) are translated as one contiguous body. The recompiler writes
to a staging directory; only files whose bytes changed are copied, so unchanged
translation units keep their timestamps and are not rebuilt.
"""
import bisect
import collections
import csv
import filecmp
import shutil
import subprocess
import sys
from pathlib import Path

root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'scripts'))
from development import run

contiguous={int(r['Start'],0):r for r in csv.DictReader((root/'private/ghidra/mp2-functions-only.csv').open())}
ranges=collections.defaultdict(list)
for r in csv.DictReader((root/'private/ghidra/body-ranges.csv').open()):
    ranges[int(r['Entry'],0)].append((int(r['RangeStart'],0),int(r['RangeEndExclusive'],0)))
starts=sorted(set(contiguous)|set(ranges))
merged=dict(contiguous); skipped=[]
for entry,parts in ranges.items():
    if len(parts)<2: continue
    parts.sort(); low,high=parts[0][0],parts[-1][1]
    gaps=[parts[i+1][0]-parts[i][1] for i in range(len(parts)-1)]
    i=bisect.bisect_right(starts,low)
    if low!=entry or any(s<high for s in starts[i:i+1]) or max(gaps)>64:
        skipped.append(entry); continue
    merged[entry]={'Name':f'mp2_{entry:08X}','Start':f'0x{entry:08X}','End':f'0x{high:08X}','Size':str(high-low)}
mapping=root/'private/ghidra/mp2-functions-merged.csv'
with mapping.open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=['Name','Start','End','Size']); w.writeheader()
    for a in sorted(merged): w.writerow(merged[a])
(root/'private/ghidra/fragmented-skipped.txt').write_text(''.join(f'0x{a:08x}\n' for a in sorted(skipped)))

staging=root/'private/generated/full-staging'
shutil.rmtree(staging,ignore_errors=True); staging.mkdir(parents=True)
config=root/'private/mp2-full-staging.toml'
config.write_text((root/'config/mp2-full.toml').read_text()
    .replace('private/generated/full/','private/generated/full-staging/')
    .replace('private/ghidra/mp2-functions-only.csv','private/ghidra/mp2-functions-merged.csv'))
code=run(['build/ps2recomp/ps2xRecomp/Release/ps2_recomp.exe',str(config)],root/'private/logs/recompile-full.log','build')
if code: raise SystemExit(f'Recompilation failed ({code})')
target=root/'private/generated/full'; target.mkdir(exist_ok=True)
new={p.name for p in staging.iterdir()}
changed=0
for name in sorted(new):
    src,dst=staging/name,target/name
    if not dst.exists() or not filecmp.cmp(src,dst,shallow=False): shutil.copy2(src,dst); changed+=1
removed=[p for p in target.iterdir() if p.name not in new]
for p in removed: p.unlink()
shutil.rmtree(staging)
print(f'{len(merged)} functions ({len(merged)-len(contiguous)} merged fragments, {len(skipped)} fragments skipped); '
      f'{changed} files changed, {len(removed)} removed')
