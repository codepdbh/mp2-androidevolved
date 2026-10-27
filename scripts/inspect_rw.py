"""Validate outer RW chunks in an authentic level texture dictionary."""
import json
from pathlib import Path
import struct

def chunks(data,start,end):
    result=[]
    offset=start
    while offset<end:
        if not any(data[offset:end]): break  # disc-sector zero padding
        if offset+12>end: raise ValueError('Truncated chunk header')
        kind,size,library=struct.unpack_from('<III',data,offset)
        stop=offset+12+size
        if stop>end: raise ValueError('Chunk exceeds containing stream')
        version=((((library>>14)&0x3ff00)+0x30000)|((library>>16)&0x3f)) if library&0xffff0000 else library<<8
        result.append(dict(offset=offset,type=hex(kind),size=size,library=hex(library),version=hex(version)))
        offset=stop
    return result

path=Path('private/disc/extracted/OTH/ASSETS/LV/01/LEVEL.IMG')
data=path.read_bytes()
kind,size,library=struct.unpack_from('<III',data,0)
# LEVEL.IMG is a container; only the first bounded chunk is interpreted here.
# Do not claim the entire IMG is one continuous RW stream.
outer=chunks(data,0,12+size)
if not outer or outer[0]['type']!='0x16': raise ValueError('Expected texture dictionary')
first=outer[0]
children=chunks(data,12,12+first['size'])
if children[0]['type']!='0x1' or children[0]['size']!=4: raise ValueError('Unexpected dictionary struct')
count,device=struct.unpack_from('<HH',data,children[0]['offset']+12)
natives=[c for c in children if c['type']=='0x15']
if len(natives)!=count: raise ValueError('Dictionary texture count mismatch')
result=dict(path=str(path),size=len(data),remaining_container_bytes=len(data)-12-size,outer=outer,first_dictionary=dict(
    texture_count=count,device_id=device,children=children))
Path('private/rw_analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(dict(first_chunk=first,texture_count=count,device_id=device,
                      parsed_dictionary_bytes=12+size,native_texture_count=len(natives)),indent=2))
