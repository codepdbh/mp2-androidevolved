"""Generate private glue for the whole-program build: declarations, terminal guards
for direct calls into untranslated (fragmented) bodies, and the entry/resume PC table.

Original translated bodies are compiled unchanged, one translation unit each.
"""
import csv
import hashlib
import json
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
elf=root/'private/disc/extracted/SLES_522.56'
with elf.open('rb') as f:
    if hashlib.file_digest(f,'sha256').hexdigest()!='9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba':
        raise SystemExit('Wrong ELF')
for name,digest in [('alarm','66cefb213e583fbf3dbded8a2d5daa6ed4b309495bfd45c1ae98228f43132aa0'),
                    ('tlb','2617a9e6d3922e97165b475a65639cd75da57e0af786b302e058abb63cc30afd')]:
    if hashlib.sha256((root/f'private/{name}-kernel.elf').read_bytes()).hexdigest()!=digest:
        raise SystemExit(f'{name} copied-payload ELF differs from the verified PAL bytes')

groups=[('private/generated/full','private/ghidra/mp2-functions-merged.csv'),
        ('private/generated/alarm-kernel','private/ghidra-kernel/kernel-functions.csv'),
        ('private/generated/tlb-kernel','private/ghidra-tlb-kernel/kernel-functions.csv')]
functions={}
for directory,mapping in groups:
    with (root/mapping).open() as f:
        ranges={int(r['Start'],0):int(r['End'],0) for r in csv.DictReader(f)}
    for source in sorted((root/directory).glob('mp2_*.cpp')):
        name=source.stem; address=int(name.rsplit('_0x',1)[1],16)
        text=source.read_text()
        end=ranges.get(address)
        if end is None or f'// Address: 0x{address:x} - 0x{end:x}' not in text: raise ValueError(f'Function-map mismatch: {name}')
        entries={address,*(int(v,16) for v in re.findall(r'case 0x([0-9a-fA-F]+)u:',text))}
        calls=set(re.findall(r'\b(mp2_[0-9A-Fa-f]+_0x[0-9a-f]+)\(rdram, ctx, runtime\)',text))
        functions[name]=dict(address=address,end=end,entries=entries,calls=calls,source=source)

# One owner per PC: a function start wins, then the body that contains the PC.
owner={}
starts={d['address']:n for n,d in functions.items()}
for name,data in functions.items():
    for pc in data['entries']:
        if pc in starts: chosen=starts[pc]
        elif data['address']<=pc<data['end']: chosen=name
        else: continue
        owner.setdefault(pc,chosen)
missing=sorted({c for d in functions.values() for c in d['calls'] if c not in functions})

out=root/'private/generated/full-glue'
out.mkdir(parents=True,exist_ok=True)
def write_if_changed(path,text):
    # Every translated body includes these headers; rewriting identical bytes would
    # needlessly rebuild ~17k translation units.
    if not path.exists() or path.read_text(encoding='ascii')!=text: path.write_text(text,encoding='ascii')
declare=lambda names:['#pragma once','#include <cstdint>','struct R5900Context;','class MP2GuestRuntime;']+\
    [f'void {n}(uint8_t*,R5900Context*,MP2GuestRuntime*);' for n in names]
called=sorted({c for d in functions.values() for c in d['calls']})
write_if_changed(out/'ps2_recompiled_functions.h','\n'.join(declare(called))+'\n')
write_if_changed(out/'all_functions.h','\n'.join(declare(sorted(functions)+missing))+'\n')
write_if_changed(out/'ps2_recompiled_stubs.h','#pragma once\n#include "ps2_runtime.h"\n#include "ps2_syscalls.h"\n')
lines=['// Generated whole-program glue. No original instruction is translated here.',
       '#include "all_functions.h"']
for n in missing:
    address=int(n.rsplit('_0x',1)[1],16)
    lines+=[f'void {n}(uint8_t*,R5900Context* cpu,MP2GuestRuntime* runtime) {{ runtime->stopMissing(cpu,0x{address:x}u,cpu->pc); }}']
lines+=['extern const MP2GuestEntry mp2rGuestEntries[]={']
lines+=[f'{{0x{pc:x}u,&{owner[pc]}}},' for pc in sorted(owner)]
lines+=['};','extern const std::size_t mp2rGuestEntryCount=sizeof(mp2rGuestEntries)/sizeof(mp2rGuestEntries[0]);']
(out/'register_full.cpp').write_text('\n'.join(lines)+'\n',encoding='ascii')
sources=[d['source'].relative_to(root).as_posix() for _,d in sorted(functions.items())]
(out/'sources.cmake').write_text('set(MP2R_GAME_SOURCES\n'+''.join(f'  "${{MP2R_ROOT}}/{s}"\n' for s in sources)+')\n',encoding='ascii')
summary=dict(functions=len(functions),entries=len(owner),guarded_missing=missing,
             elf_sha256='9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba')
(out/'manifest.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print(f'Prepared {len(functions)} original functions, {len(owner)} entry/resume PCs, {len(missing)} terminal guards')
