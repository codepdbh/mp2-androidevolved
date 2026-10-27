"""Inspect the native ABI and PT_LOAD alignment inside the actual diagnostic APK."""
from pathlib import Path
import struct
import zipfile
from development import ROOT

apk=ROOT/'android/app/build/outputs/apk/debug/app-debug.apk'
with zipfile.ZipFile(apk) as archive:
    libs=[name for name in archive.namelist() if name.endswith('.so')]
    if libs!=['lib/arm64-v8a/libmp2r.so']: raise SystemExit(f'Unexpected native libraries: {libs}')
    data=archive.read(libs[0])
    if data[:6]!=b'\x7fELF\x02\x01': raise SystemExit('Expected little-endian ELF64')
    if struct.unpack_from('<H',data,18)[0]!=183: raise SystemExit('Expected AArch64')
    phoff=struct.unpack_from('<Q',data,32)[0]
    entsize,count=struct.unpack_from('<HH',data,54)
    loads=0
    for i in range(count):
        kind,flags,offset,va,pa,filesz,memsz,align=struct.unpack_from('<IIQQQQQQ',data,phoff+i*entsize)
        if kind==1:
            if align<16384 or offset%16384!=va%16384: raise SystemExit('PT_LOAD not 16KB aligned')
            loads+=1
    print(f'PASS: {libs[0]}, ELF64/AArch64, {loads} PT_LOAD segments aligned to 16KB')
    print(f'APK bytes={apk.stat().st_size}; runtime on a 16KB device remains untested')
