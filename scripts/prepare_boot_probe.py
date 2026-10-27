"""Select an explicitly bounded original startup prefix ending before kernel setup."""
import hashlib
from pathlib import Path
import struct

root=Path(__file__).resolve().parents[1]
elf=root/'private/disc/extracted/SLES_522.56'
d=elf.read_bytes()
if hashlib.sha256(d).hexdigest()!='9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba':
    raise SystemExit('Wrong ELF')
listing=(root/'private/ghidra/startup-disassembly.txt').read_text()
if '001001c0  move gp,a0' not in listing or '001001c8  syscall' not in listing:
    raise SystemExit('Ghidra startup evidence missing')
start,end=0x100008,0x1001c4
path=root/'private/boot-prefix.csv'
path.write_text(f'Name,Start,End,Size\nmp2_boot_prefix,{start:#x},{end:#x},{end-start}\n',encoding='ascii')
print('Original startup prefix',hex(start),hex(end),end-start,'bytes; before syscall')
print('GP',hex(struct.unpack_from('<I',d,4984280+20)[0]))
