# Análisis ISO — 2026-10-26

Estado: VALIDADA la identidad y extracción local; no validado el contenido lingüístico completo.

- Imagen: `Max Payne 2.iso`, 3992453120 bytes.
- SHA-256: `0e0ee6836ff012f1879f9131c6c8f626ff5bc1ad2856aa936fa2307d9c5cc969`.
- Serial BOOT2: SLES-52256; modo PAL según SYSTEM.CNF.
- Ejecutable exacto: `SLES_522.56`, 4984624 bytes.
- SHA-256 ELF: `9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba`.
- ELF32 little endian, e_machine=8 (MIPS), entrada `0x100008`.
- .symtab/.strtab vacías. El segmento cargable principal mezcla código y datos:
  offset 0x80, VA 0x00100000, filesz 4984064, memsz 6179840, flags RWX.
  No confundir todo el segmento ejecutable con funciones.

```ini
BOOT2 = cdrom0:\SLES_522.56;1 
VER = 1.00 
VMODE = PAL 
HDDUNITPOWER = NICHDD
```

Se extrajeron 504 archivos en `private/disc/extracted/`.
El manifiesto privado conserva rutas ISO con versión, tamaños y SHA-256.
La extracción elimina `;1` de los nombres de host y conserva las rutas y bytes;
la ruta original queda en el manifiesto. No reproduce metadatos de fechas/permisos.
Una segunda lectura comprobó SHA-256 de cada archivo contra la ISO, sin sobrescribir.
La imagen se abrió solo para lectura y no se modificó.

| Extensión | Archivos |
|---|---:|
| `.56` | 1 |
| `.CAP` | 2 |
| `.CNF` | 1 |
| `.CP2` | 70 |
| `.DAT` | 2 |
| `.DIR` | 2 |
| `.IMG` | 71 |
| `.IRX` | 11 |
| `.MEC` | 70 |
| `.PSS` | 5 |
| `.RAS` | 63 |
| `.RWS` | 200 |
| `.SAL` | 2 |
| `.SPL` | 2 |
| `.TXT` | 2 |

Raíces: ENG y OTH, DATA.RAS, módulos IOP y vídeos PSS. RAS comienza con `RAS\0`;
su envoltorio no se debe pasar directamente a librw como un clump.
Las carpetas de nivel contienen LEVEL.RAS, LEVEL.IMG, STREAMS.RWS, MUSIC.RWS,
CIN.RWS y capturas MEC/CP2. Las extensiones no prueban por sí solas el formato interno.
RWA.IRX y las cadenas RpPDS/World/Skin/HAnim/LtMap/PTank/ADC del ELF son indicios
de middleware RenderWare; todavía no se identificaron versiones ni contratos binarios.
Una inspección posterior del primer TXD en OTH/ASSETS/LV/01/LEVEL.IMG identificó
library ID 0x1803ffff (versión 0x36003), device ID6 y una textura nativa;
esto no identifica la versión de todos los plugins ni prueba que librw la decodifique.
Hay cadenas de Havok: conservar física requiere investigar sus funciones originales.
La presencia de ENG/OTH no basta para afirmar voces y textos en español: pendiente
identificar tablas de idioma y validarlas al ejecutar contenido real.

Reproducción: `.venv/Scripts/python.exe scripts/analyze_iso.py "Max Payne 2.iso"`.
El manifiesto y todo material del juego están excluidos de Git.
