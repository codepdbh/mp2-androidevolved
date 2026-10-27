"""Bind isolated executable probes to exact bytes of the user's ELF."""
import hashlib
from pathlib import Path
import struct
p=Path('private/disc/extracted/SLES_522.56')
d=p.read_bytes()
if hashlib.sha256(d).hexdigest()!='9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba':
    raise SystemExit('Wrong ELF')
for address,words in [(0x10dcc8,(0x03e00008,0x2402ffff)),
                      (0x1b1c20,(0x3c021194,0x03e00008,0x00000000,0x00000000))]:
    actual=struct.unpack_from('<'+'I'*len(words),d,address-0x100000+0x80)
    if actual!=words: raise SystemExit(f'Original bytes differ at {address:#x}: {actual}')
    print(f'{address:#010x}: '+ ' '.join(f'{x:08x}' for x in actual))
for caller,target in [(0x123a08,0x10dcc8),(0x1ab680,0x1b1c20)]:
    word=struct.unpack_from('<I',d,caller-0x100000+0x80)[0]
    if word>>26!=3 or (word&0x3ffffff)<<2!=target:
        raise SystemExit('Expected original direct call not found')
    print(f'Original JAL {caller:#010x} -> {target:#010x}')
