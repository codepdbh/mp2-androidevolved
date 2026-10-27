"""Extract structural evidence and conservative leaf candidates, not a function map."""
import json
from pathlib import Path
import re
import struct

p=Path('private/disc/extracted/SLES_522.56')
d=p.read_bytes()
h=struct.unpack_from('<HHIIIIIHHHHHH',d,16)
sections=[]
raw=[struct.unpack_from('<IIIIIIIIII',d,h[5]+i*h[10]) for i in range(h[11])]
names=d[raw[h[12]][4]:raw[h[12]][4]+raw[h[12]][5]]
for s in raw:
    name=names[s[0]:].split(b'\0')[0].decode('ascii','replace')
    sections.append(dict(name=name,type=s[1],flags=s[2],address=hex(s[3]),offset=s[4],size=s[5]))
strings=[m.group().decode('ascii') for m in re.finditer(rb'[\x20-\x7e]{8,}',d)]
interesting=[s for s in strings if re.search(r'RenderWare|Rockstar|Vienna|rw[A-Z]|Rp[A-Z]|sce[A-Z]|spanish|espan|havok',s,re.I)]
leaves=[]
for s in raw:
    if not s[2]&4: continue
    for off in range(s[4],s[4]+s[5]-12,4):
        a,b,c=struct.unpack_from('<III',d,off)
        # One constant-return instruction followed by JR RA / NOP. Boundary
        # requires previous JR RA / NOP; still only a candidate until inspected.
        if b==0x03e00008 and c==0 and (a>>16 in (0x2402,0x3402)):
            leaves.append(dict(address=hex(s[3]+off-s[4]),words=[hex(a),hex(b),hex(c)],
                               return_immediate=a&65535))
reginfo=next((s for s in sections if s['name']=='.reginfo'),None)
gp=hex(struct.unpack_from('<I',d,reginfo['offset']+20)[0]) if reginfo and reginfo['size']==24 else None
out=dict(sections=sections,reginfo_gp=gp,interesting_strings=interesting,constant_leaf_candidates=leaves)
Path('private/elf_analysis.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
print(json.dumps(dict(sections=sections,candidate_count=len(leaves),candidates=leaves[:12],strings=interesting[:25]),indent=2))
