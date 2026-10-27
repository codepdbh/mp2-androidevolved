"""Build a private analysis ELF for the exact kernel patch copied by original InitAlarm.
This never edits the original ELF/ISO. The payload is byte-for-byte from that ELF.
"""
import hashlib
import json
from pathlib import Path
import struct
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('--kind',choices=['alarm','tlb'],default='alarm')
kind=parser.parse_args().kind
root=Path(__file__).resolve().parents[1]
original=(root/'private/disc/extracted/SLES_522.56').read_bytes()
if hashlib.sha256(original).hexdigest()!='9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba': raise SystemExit('Wrong original ELF')
source,target,length=(0x54d6a0,0x80076000,0x740) if kind=='alarm' else (0x54c868,0x80075000,0x330)
payload=original[source-0x100000+128:source-0x100000+128+length]
data=bytearray(0x1000)+payload
strings=b'\0.text\0.shstrtab\0'; stroff=len(data); data+=strings
while len(data)%4:data+=b'\0'
shoff=len(data)
data+=bytes(40)
data+=struct.pack('<10I',1,1,6,target,0x1000,length,0,0,16,0)
data+=struct.pack('<10I',7,3,0,0,stroff,len(strings),0,0,1,0)
ident=b'\x7fELF\x01\x01\x01'+bytes(9)
data[:52]=struct.pack('<16sHHIIIIIHHHHHH',ident,2,8,1,target,52,shoff,0x20924000,52,32,1,40,3,2)
data[52:84]=struct.pack('<8I',1,0x1000,target,target&0x1fffffff,length,length,5,0x1000)
path=root/f'private/{kind}-kernel.elf'; path.write_bytes(data)
plan=dict(source=source,target=target,length=length,payload_sha256=hashlib.sha256(payload).hexdigest(),elf_sha256=hashlib.sha256(data).hexdigest())
(root/f'private/{kind}-kernel-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
print('Private copied-code analysis ELF prepared:',json.dumps(plan))
