"""Generate private glue/entry maps for an explicitly selected original-code set."""
import csv
import hashlib
import json
from pathlib import Path
import re
import tomllib
import argparse
import os

root=Path(__file__).resolve().parents[1]
elf=root/'private/disc/extracted/SLES_522.56'
with elf.open('rb') as f:
    if hashlib.file_digest(f,'sha256').hexdigest()!='9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba':
        raise SystemExit('Wrong ELF')
args=argparse.ArgumentParser()
args.add_argument('--map-only',action='store_true')
args=args.parse_args()
selection=tomllib.loads((root/'config/initialization-functions.toml').read_text())['selection']
addresses=selection['addresses']
with (root/'private/ghidra/mp2-functions-only.csv').open() as f:
    known={int(row['Start'],0):row for row in csv.DictReader(f)}
with (root/'private/ghidra/initialization-only.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=['Name','Start','End','Size']); writer.writeheader()
    for address in sorted(addresses): writer.writerow(known[address])
if args.map_only:
    print('Wrote selected Ghidra function map'); raise SystemExit(0)
selected={}
sources={address:root/selection.get('generated_dir','private/generated/ghidra') for address in addresses}
with (root/'private/ghidra-kernel/kernel-functions.csv').open() as f:
    for row in csv.DictReader(f):
        address=int(row['Start'],0)
        if not 0x80076000<=address<int(row['End'],0)<=0x80076740:
            raise ValueError('Alarm function outside verified copied payload')
        known[address]=row
        sources[address]=root/'private/generated/alarm-kernel'
kernel_elf=root/'private/alarm-kernel.elf'
kernel_sha256=hashlib.sha256(kernel_elf.read_bytes()).hexdigest()
if kernel_sha256!='66cefb213e583fbf3dbded8a2d5daa6ed4b309495bfd45c1ae98228f43132aa0':
    raise ValueError('Alarm analysis ELF differs from the verified PAL copied payload')
tlb_sha256=hashlib.sha256((root/'private/tlb-kernel.elf').read_bytes()).hexdigest()
if tlb_sha256!='2617a9e6d3922e97165b475a65639cd75da57e0af786b302e058abb63cc30afd':
    raise ValueError('TLB analysis ELF differs from the verified PAL copied payload')
with (root/'private/ghidra-tlb-kernel/kernel-functions.csv').open() as f:
    for row in csv.DictReader(f):
        address=int(row['Start'],0)
        if not 0x80075000<=address<int(row['End'],0)<=0x80075300:
            raise ValueError('TLB function outside copied code')
        known[address]=row
        sources[address]=root/'private/generated/tlb-kernel'
for address in sources:
    row=known[address]
    name=f"{row['Name']}_0x{address:x}"
    source=sources[address]/f'{name}.cpp'
    text=source.read_text()
    end=int(row['End'],0)
    if f'// Address: 0x{address:x} - 0x{end:x}' not in text: raise ValueError('Function-map mismatch')
    entries={address,*[int(v,16) for v in re.findall(r'case 0x([0-9a-fA-F]+)u:',text)]}
    calls=set(re.findall(r'\b(mp2_[0-9A-Fa-f]+_0x[0-9a-f]+)\(rdram, ctx, runtime\)',text))
    selected[name]=dict(address=address,end=end,entries=sorted(entries),calls=sorted(calls),sha256=hashlib.sha256(source.read_bytes()).hexdigest())
out=root/'private/generated/initialization'
out.mkdir(parents=True,exist_ok=True)
lines=['// Generated glue only. Original translated function bodies are included unchanged.']
for name in selected:
    lines += [f'void {name}(uint8_t*,R5900Context*,MP2InitializationRuntime*);']
for name in selected:
    source=sources[selected[name]['address']]/f'{name}.cpp'
    lines += ['#include "'+os.path.relpath(source,out).replace('\\','/')+'"']
missing=sorted({call for data in selected.values() for call in data['calls'] if call not in selected})
for name in missing:
    address=int(name.rsplit('_0x',1)[1],16)
    lines += [f'void {name}(uint8_t*,R5900Context* cpu,MP2InitializationRuntime* runtime) {{',
              f'    runtime->stopMissing(cpu,0x{address:x}u,0u);', '}']
lines += ['static void registerSelected(MP2InitializationRuntime& runtime) {']
for name,data in selected.items():
    for entry in data['entries']:
        lines += [f'    runtime.bind(0x{entry:x}u,&{name});']
lines += ['}']
(out/'selected_functions.inc').write_text('\n'.join(lines)+'\n',encoding='ascii')
(out/'selection.json').write_text(json.dumps(dict(functions=selected,guarded_missing=missing,alarm_analysis_sha256=kernel_sha256,tlb_analysis_sha256=tlb_sha256),indent=2)+'\n',encoding='utf-8')
print(f'Prepared {len(selected)} original functions, {sum(len(v["entries"]) for v in selected.values())} exact entry/resume PCs, {len(missing)} terminal guards')
