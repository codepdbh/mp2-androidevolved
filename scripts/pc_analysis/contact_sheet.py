#!/usr/bin/env python3
"""Hoja de contactos de las capturas del host, para revisar una ejecución de un vistazo.

  contact_sheet.py <sandbox> <salida.png> <frame> [frame ...]

Las capturas muestran contenido del juego: la salida va fuera del repositorio.
"""
import sys
from PIL import Image, ImageDraw


def main(sandbox, out, frames):
    columns = 3 if len(frames) > 4 else 2
    rows = (len(frames) + columns - 1) // columns
    width, height = 320, 240
    sheet = Image.new("RGB", (columns * width, rows * height))
    for i, frame in enumerate(frames):
        image = Image.open(f"{sandbox}/frame_{frame:05d}.bmp").convert("RGB").resize((width, height))
        ImageDraw.Draw(image).text((4, 4), str(frame), fill=(255, 255, 0))
        sheet.paste(image, ((i % columns) * width, (i // columns) * height))
    sheet.save(out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], [int(a) for a in sys.argv[3:]])
