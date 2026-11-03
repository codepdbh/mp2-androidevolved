#!/usr/bin/env python3
"""Lista los controles de un diálogo de MaxPayne2.exe.

  dump_dialog.py <exe> <id de recurso>
"""
import struct, sys
import pefile


def sz(b, o):
    w = struct.unpack_from("<H", b, o)[0]
    if w == 0xFFFF:
        return struct.unpack_from("<H", b, o + 2)[0], o + 4
    e = o
    while struct.unpack_from("<H", b, e)[0]:
        e += 2
    return b[o:e].decode("utf-16le"), e + 2


def main(path, rid):
    pe = pefile.PE(path)
    img = bytes(pe.get_memory_mapped_image())
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if t.id != 5:
            continue
        for e in t.directory.entries:
            if e.id != rid:
                continue
            d = e.directory.entries[0].data.struct
            b = img[d.OffsetToData:d.OffsetToData + d.Size]
            ex = struct.unpack_from("<HH", b)[1] == 0xFFFF
            print("extendida" if ex else "clásica", "estilo", hex(struct.unpack_from("<I", b, 12 if ex else 0)[0]))
            n = struct.unpack_from("<H", b, 16 if ex else 8)[0]
            o = 26 if ex else 18
            menu, o = sz(b, o)
            cls, o = sz(b, o)
            title, o = sz(b, o)
            style = struct.unpack_from("<I", b, 12 if ex else 0)[0]
            if style & 0x40:
                o += 6 if ex else 2
                face, o = sz(b, o)
            print("título", title, "controles", n)
            for _ in range(n):
                o = (o + 3) & ~3
                if ex:
                    _, _, cstyle, x, y, w, h, cid = struct.unpack_from("<IIIhhhhI", b, o)
                    o += 24
                else:
                    cstyle, _, x, y, w, h, cid = struct.unpack_from("<IIhhhhH", b, o)
                    o += 18
                c, o = sz(b, o)
                text, o = sz(b, o)
                extra = struct.unpack_from("<H", b, o)[0]
                o += 2 + extra
                print(f"  id={cid} clase={c} estilo={cstyle:08X} {w}x{h} {text!r}")


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]))
