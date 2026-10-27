"""Disassemble an IOP module (IRX) with import stubs named after PS2SDK export tables.

usage: irx_disasm.py MODULE.IRX EXPORTS_DIR [--rpc] [--range START END]
EXPORTS_DIR holds <library>.tab files (PS2SDK exports.tab). --rpc prints the
sceSifRegisterRpc call sites with the server ID and handler address.
"""
import argparse
import re
import struct
from pathlib import Path
import capstone

def load(path):
    data=Path(path).read_bytes()
    phoff,=struct.unpack_from('<I',data,28); phnum,=struct.unpack_from('<H',data,44)
    image=bytearray()
    for i in range(phnum):
        p_type,off,va,pa,fs,ms=struct.unpack_from('<6I',data,phoff+i*32)
        if p_type==1:
            image[va:va+ms]=bytes(max(0,va+ms-len(image)))[:0]
            if len(image)<va+ms: image.extend(bytes(va+ms-len(image)))
            image[va:va+fs]=data[off:off+fs]
    return bytes(image)

def export_names(directory):
    names={}
    for tab in Path(directory).glob('*.tab'):
        current=None; index=0
        for line in tab.read_text(errors='ignore').splitlines():
            m=re.match(r'DECLARE(?:_EXPORT_TABLE|_EXPORT_TABLE_\w*)\((\w+)',line)
            if m: current=m.group(1); index=0; continue
            m=re.match(r'\s*DECLARE_EXPORT\((\w+)\)',line)
            if m and current: names[(current,index)]=m.group(1); index+=1
    return names

def imports(image,names):
    stubs={}
    for i in range(0,len(image)-20,4):
        if struct.unpack_from('<I',image,i)[0]!=0x41e00000: continue
        lib=image[i+12:i+20].split(b'\0')[0].decode('ascii','replace'); j=i+20
        while j+8<=len(image):
            w1,w2=struct.unpack_from('<II',image,j)
            if w1!=0x03e00008 or (w2>>16)!=0x2400: break
            stubs[j]=f'{lib}::{names.get((lib,w2&0xffff),"#%d"%(w2&0xffff))}'; j+=8
    return stubs

def main():
    a=argparse.ArgumentParser(); a.add_argument('irx'); a.add_argument('exports')
    a.add_argument('--rpc',action='store_true'); a.add_argument('--range',nargs=2)
    o=a.parse_args()
    image=load(o.irx); stubs=imports(image,export_names(o.exports))
    md=capstone.Cs(capstone.CS_ARCH_MIPS,capstone.CS_MODE_MIPS32|capstone.CS_MODE_LITTLE_ENDIAN)
    md.skipdata=True
    insns=list(md.disasm(image,0))
    def text(ins):
        s=f'{ins.address:06x}: {ins.mnemonic} {ins.op_str}'
        m=re.match(r'0x([0-9a-f]+)',ins.op_str)
        if ins.mnemonic in ('jal','j') and m and int(m.group(1),16) in stubs: s+=f'   <{stubs[int(m.group(1),16)]}>'
        return s
    if o.rpc:
        for k,ins in enumerate(insns):
            m=re.match(r'0x([0-9a-f]+)',ins.op_str)
            if ins.mnemonic=='jal' and m and stubs.get(int(m.group(1),16),'').endswith('sceSifRegisterRpc'):
                print('\n'.join(text(x) for x in insns[max(0,k-12):k+2])); print('---')
    if o.range:
        lo,hi=int(o.range[0],16),int(o.range[1],16)
        for ins in insns:
            if lo<=ins.address<hi: print(text(ins))
main()
