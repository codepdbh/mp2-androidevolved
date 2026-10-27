"""Sector map of the verified disc for IOP CD emulation on extracted files.

Writes private/disc/disc_map.json (file extents: LBA, size, path) and
private/disc/extracted/MP2RDISC.BIN with every 2048-byte sector that belongs to no
file (system area, volume descriptors, path tables, directory records), so any LBA
can be served from the extracted files plus this small metadata blob.
"""
import json
from pathlib import Path
import struct
import sys
import pycdlib

root=Path(__file__).resolve().parents[1]
iso_path=Path(sys.argv[1]) if len(sys.argv)>1 else root/'Max Payne 2.iso'
manifest=json.loads((root/'private/disc/iso_manifest.json').read_text())
iso=pycdlib.PyCdlib(); iso.open(str(iso_path))
extents=[]
for row in manifest['files']:
    record=iso.get_record(iso_path=row['iso_path'])
    extents.append({'path':row['path'],'lba':record.extent_location(),'size':row['size']})
iso.close()
total=iso_path.stat().st_size//2048
covered=bytearray(total)
for e in extents:
    for s in range(e['lba'],e['lba']+(e['size']+2047)//2048): covered[s]=1
# Metadata sectors are the uncovered ones up to the last directory/file start.
meta=[s for s in range(min(e['lba'] for e in extents)) if not covered[s]]
with iso_path.open('rb') as f, (root/'private/disc/extracted/MP2RDISC.BIN').open('wb') as out:
    # Layout: magic, metadata count, total sectors, file count; metadata LBAs;
    # file table {lba, size, path length, path}; metadata sector bytes.
    out.write(b'MP2RDISC'+struct.pack('<III',len(meta),total,len(extents)))
    out.write(struct.pack(f'<{len(meta)}I',*meta))
    for e in sorted(extents,key=lambda e:e['lba']):
        path=e['path'].encode('ascii')
        out.write(struct.pack('<III',e['lba'],e['size'],len(path))+path)
    for s in meta: f.seek(s*2048); out.write(f.read(2048))
(root/'private/disc/disc_map.json').write_text(json.dumps({'sectors':total,'metadata_sectors':len(meta),'files':sorted(extents,key=lambda e:e['lba'])},indent=1))
print(f'{len(extents)} file extents, {len(meta)} metadata sectors, {total} total sectors')
