#!/usr/bin/env python3
"""Genera las llamadas pila x86 -> COM nativo de Direct3D 8 desde d3d8.h.

  generate_d3d8_bridge.py <d3d8.h> <salida.inc>

Mismo método que generate_device_bridge.py de NFSU2: la ABI fijada en la cabecera
decide cada conversión, y lo que necesita lógica propia (bloqueos, creación de
recursos, Reset, Present) va a mano en d3d8_bridge.cpp.
"""
import pathlib, re, sys

KINDS = {"IDirect3D8": "D3D8", "IDirect3DDevice8": "Device", "IDirect3DTexture8": "Texture",
         "IDirect3DCubeTexture8": "Cube", "IDirect3DVolumeTexture8": "VolumeTexture",
         "IDirect3DSurface8": "Surface", "IDirect3DVolume8": "Volume", "IDirect3DVertexBuffer8": "VertexBuffer",
         "IDirect3DIndexBuffer8": "IndexBuffer", "IDirect3DSwapChain8": "SwapChain"}
# Interfaz: (nombre, número esperado de métodos, ranuras escritas a mano).
INTERFACES = [
    ("IDirect3DDevice8", 97, {0, 1, 2, 3, 14, 15, 13, 20, 21, 22, 23, 24, 25, 26, 27}),
    ("IDirect3DTexture8", 19, {0, 1, 2, 16, 17}),
    ("IDirect3DCubeTexture8", 19, {0, 1, 2, 16, 17}),
    ("IDirect3DVolumeTexture8", 19, {0, 1, 2, 16, 17}),
    ("IDirect3DSurface8", 11, {0, 1, 2, 7, 9, 10}),
    ("IDirect3DVolume8", 11, {0, 1, 2, 7, 9, 10}),
    ("IDirect3DVertexBuffer8", 14, {0, 1, 2, 11, 12}),
    ("IDirect3DIndexBuffer8", 14, {0, 1, 2, 11, 12}),
]


def methods(header, interface):
    start = header.index(f"DECLARE_INTERFACE_({interface},")
    body = header[start:header.index("};", start)]
    found = re.findall(r"STDMETHOD(?:_\(([^,]+),\s*(\w+)\)|\((\w+)\))\s*\((.*?)\)\s*PURE", body, re.S)
    return [((result or "HRESULT").strip(), name or plain, re.sub(r"\s+", " ", params).strip()) for result, name, plain, params in found]


def emit(result, name, params, receiver):
    params = params.removeprefix("THIS_").strip() if params != "THIS" else ""
    params = [p.strip() for p in params.split(",")] if params else []
    call, setup, complete = [], [], []
    for index, parameter in enumerate(params, 1):
        kind = re.sub(r"\s+\[.*$", "", parameter).replace("struct ", "")
        kind = re.sub(r"(\*|\s)([A-Za-z_]\w*)$", r"\1", kind).strip()
        kind = re.sub(r"\s*\*\s*", "*", kind)
        argument = f"args[{index}]"
        if kind == "void**" or kind == "BYTE**":
            raise SystemExit(f"{name}: {kind} necesita implementación propia")
        if kind.endswith("**") and kind[:-2] in KINDS:
            base = kind[:-2]
            setup.append(f"{base}* object{index}=nullptr;")
            call.append(f"{argument}?&object{index}:nullptr")
            wrapped = f"wrapTexture(object{index})" if base == "IDirect3DBaseTexture8" else f"wrap(object{index},Kind::{KINDS[base]})"
            complete.append(f"if({argument})write({argument},SUCCEEDED(status)?{wrapped}:0);")
        elif kind == "IDirect3DBaseTexture8**":
            setup.append(f"IDirect3DBaseTexture8* object{index}=nullptr;")
            call.append(f"{argument}?&object{index}:nullptr")
            complete.append(f"if({argument})write({argument},SUCCEEDED(status)?wrapTexture(object{index}):0);")
        elif kind.startswith("IDirect3D") and kind.endswith("*"):
            call.append(f"nativeObject<{kind[:-1]}>({argument})")
        elif kind == "REFGUID":
            call.append(f"*static_cast<const GUID*>(pointer({argument}))")
        elif kind == "HWND":
            call.append(f"{argument}?hostWindow.load():nullptr")
        elif "*" in kind:
            call.append(f"static_cast<{kind}>(pointer({argument}))")
        elif kind == "float":
            call.append(f"floatValue({argument})")
        else:
            call.append(f"static_cast<{kind}>({argument})")
    line = f"*argumentCount={len(params) + 1};" + "".join(setup)
    invocation = f"{receiver}->{name}({','.join(call)})"
    if complete:
        return line + f"HRESULT status={invocation};" + "".join(complete) + "return status;"
    if result == "void":
        return line + invocation + ";return 0;"
    if result == "float":
        return line + f"return floatBits({invocation});"
    return line + f"return {invocation};"


def main(header_path, out):
    header = pathlib.Path(header_path).read_text(encoding="latin1")
    lines = ["// Generado por scripts/pc_recomp/generate_d3d8_bridge.py desde d3d8.h."]
    for interface, expected, manual in INTERFACES:
        found = methods(header, interface)
        if len(found) != expected:
            raise SystemExit(f"{interface}: {len(found)} métodos, se esperaban {expected}")
        lines.append(f"if(kind==uint32_t(Kind::{KINDS[interface]})){{auto resource=static_cast<{interface}*>(object);switch(method){{")
        for slot, (result, name, params) in enumerate(found):
            if slot not in manual:
                lines.append(f"case {slot}:{{/*{name}*/{emit(result, name, params, 'resource')}}}")
        lines.append("}}")
    pathlib.Path(out).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{sum(e for _, e, _ in INTERFACES)} métodos revisados -> {out}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
