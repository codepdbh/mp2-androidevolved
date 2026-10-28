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
        # Interleaved bodies (another function inside the span, or the entry not first) are still
        # translated over the full span: overlapping translations execute identically, and the
        # entry table prefers function starts. Spans beyond any real function size stay out.
        if high-low>0x8000 or low in merged: skipped.append(entry); continue
        merged[low]={'Name':f'mp2_{low:08X}','Start':f'0x{low:08X}','End':f'0x{high:08X}','Size':str(high-low)}
        continue
    merged[entry]={'Name':f'mp2_{entry:08X}','Start':f'0x{entry:08X}','End':f'0x{high:08X}','Size':str(high-low)}
# Address-taken entries (pointer tables) become functions ending at the next known start.
import struct
import tomllib
elf=(root/'private/disc/extracted/SLES_522.56').read_bytes()
word=lambda a: struct.unpack_from('<I',elf,a-0x100000+0x80)[0]
added=0
for table in tomllib.loads((root/'config/mp2-address-taken.toml').read_text())['tables']:
    if word(table['end'])!=0: raise SystemExit(f"{table['name']}: table is not zero-terminated")
    for slot in range(table['start'],table['end'],4):
        entry=word(slot)
        if entry in merged: continue
        following=[a for a in sorted(set(merged)|set(ranges)) if a>entry]
        end=min(following[0] if following else entry+0x100,entry+0x2000)
        merged[entry]={'Name':f'mp2_{entry:08X}','Start':f'0x{entry:08X}','End':f'0x{end:08X}','Size':str(end-entry)}
        added+=1
# Uncovered code between known bodies: functions reached only through pointers (jalr,
# vtables, callbacks). Each gap is split after every `jr $ra` + delay slot; zero padding is
# skipped. Translating a data word is harmless: it only runs if the guest jumps to it.
covered=sorted([(int(r['Start'],0),int(r['End'],0)) for r in merged.values()]+
               [p for parts in ranges.values() for p in parts])
gap_functions=0
cursor=0x100000
for start,end in covered+[(0x5b2c40,0x5b2c40)]:
    if start>cursor:
        a=cursor
        while a<start:
            while a<start and word(a)==0: a+=4
            if a>=start: break
            b=a
            while b<start and word(b)!=0x03e00008: b+=4
            closed=b<start
            b=min(b+8,start)
            # Large region with no return, or larger than any real function (max ~20KB): data, not code.
            if (not closed and b-a>0x2000) or b-a>0x8000: a=b; continue
            if a not in merged:
                merged[a]={'Name':f'mp2_{a:08X}','Start':f'0x{a:08X}','End':f'0x{b:08X}','Size':str(b-a)}
                gap_functions+=1
            a=b
    cursor=max(cursor,end)
# Entry points inside a known body: JAL targets and code pointers stored in data (vtables,
# callbacks) that sit right after a function end (jr $ra / tail j + delay slot, then padding).
# Each splits the body that contains it. Switch jump-table targets do not follow an end.
def follows_end(t):
    a=t-4
    while a>0x100000 and word(a)==0: a-=4
    return any(word(e)==0x03e00008 or (word(e)>>26)==2 for e in (a-4,a))
entries=set()
for a in range(0x100000,0x5b2c3c,4):
    w=word(a)
    if (w>>26)==3:
        t=((a+4)&0xf0000000)|((w&0x3ffffff)<<2)
        if 0x100000<=t<0x5b2c3c: entries.add(t)
for a in range(0x54d000,0x5c0d00,4):
    v=word(a)
    if 0x100000<=v<0x5b2c3c and v%4==0 and follows_end(v): entries.add(v)
split=0
ordered=sorted(merged)
for t in sorted(entries):
    if t in merged: continue
    i=bisect.bisect_left(ordered,t)-1
    owner=ordered[i] if i>=0 else None
    if owner is None or int(merged[owner]['End'],0)<=t: continue
    bisect.insort(ordered,t)
    end=int(merged[owner]['End'],0)
    merged[owner]['End']=f'0x{t:08X}'; merged[owner]['Size']=str(t-owner)
    merged[t]={'Name':f'mp2_{t:08X}','Start':f'0x{t:08X}','End':f'0x{end:08X}','Size':str(end-t)}
    split+=1
print(f'{split} bodies split at entry points')
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
print(f'{len(merged)} functions ({gap_functions} gap, {added} address-taken, {len(merged)-len(contiguous)-added-gap_functions} merged fragments, {len(skipped)} fragments skipped); '
      f'{changed} files changed, {len(removed)} removed')
