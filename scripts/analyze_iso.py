"""Read-only ISO9660 extraction, per-file verification and ELF32 inspection."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import pycdlib

def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def elf_info(path):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected little-endian ELF32')
    h = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    segments = []
    for i in range(h[9]):
        p = struct.unpack_from('<IIIIIIII', data, h[4] + i*h[8])
        if p[1]+p[4] > len(data) or p[4] > p[5]:
            raise ValueError('Invalid ELF segment')
        segments.append(dict(zip(('type','offset','vaddr','paddr','filesz','memsz','flags','align'),p)))
    return dict(machine=h[1], entry=hex(h[3]), flags=hex(h[6]), segments=segments,
                sha256=digest(path), size=len(data))

def analyze(image, output):
    output.mkdir(parents=True, exist_ok=True)
    iso = pycdlib.PyCdlib()
    iso.open(str(image))
    records = []
    try:
        cnf = io.BytesIO()
        iso.get_file_from_iso_fp(cnf, iso_path='/SYSTEM.CNF;1')
        config = cnf.getvalue().decode('ascii')
        boot = re.search(r'BOOT2\s*=\s*cdrom0:\\([^;\r\n]+)',config,re.I)
        if not boot:
            raise ValueError('Missing BOOT2')
        executable = boot[1].replace('\\','/')
        for directory, dirs, files in iso.walk(iso_path='/'):
            for name in files:
                source = directory.rstrip('/')+'/'+name
                relative = source.lstrip('/').split(';')[0]
                target = output/'extracted'/relative
                if not target.resolve().is_relative_to((output/'extracted').resolve()):
                    raise ValueError('Unsafe ISO path')
                target.parent.mkdir(parents=True, exist_ok=True)
                expected = iso.get_record(iso_path=source).data_length
                if target.exists():
                    # Verify existing files against a fresh streamed read, never overwrite.
                    class HashSink:
                        def __init__(self): self.hash = hashlib.sha256()
                        def write(self, b): self.hash.update(b); return len(b)
                    sink = HashSink()
                    iso.get_file_from_iso_fp(sink, iso_path=source)
                    if digest(target) != sink.hash.hexdigest():
                        raise ValueError(f'Existing extraction mismatch: {target}')
                else:
                    with target.open('xb') as f:
                        iso.get_file_from_iso_fp(f, iso_path=source)
                if target.stat().st_size != expected:
                    raise ValueError(f'Size mismatch: {target}')
                records.append(dict(path=relative,iso_path=source,size=expected,sha256=digest(target)))
        result = dict(image=image.name,size=image.stat().st_size,sha256=digest(image),
                      system_cnf=config,boot=executable,files=records,
                      elf=elf_info(output/'extracted'/executable))
        (output/'iso_manifest.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(json.dumps({k:v for k,v in result.items() if k!='files'},indent=2))
        print(f'Extracted and hashed {len(records)} files')
    finally:
        iso.close()

if __name__ == '__main__':
    p=argparse.ArgumentParser()
    p.add_argument('image',type=Path)
    p.add_argument('--output',type=Path,default=Path('private/disc'))
    a=p.parse_args()
    analyze(a.image,a.output)
