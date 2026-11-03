# Informe de binarios de PC de Max Payne 2

Generado por `scripts/pc_analysis/write_reports.py` desde la copia local; no contiene código del juego.

## Identidad de la versión

- `MaxPayne2.exe`: SHA-256 `1e25cafc37174a607b58a38dfac796056f0f38f35d253b0b2d7677f9bd7bd010`, 1486848 bytes.
- Recurso de versión: FileVersion `1, 1, 102, 0`, ProductVersion `1, 0, 0, 0`, compañía `Remedy Entertainment`.
- Marca de tiempo PE `0x3f8ff27f` (17 de octubre de 2003). Cadena de compilación embebida: `Fri Oct 17 16:45:00 2003`.
- Las DLL de Remedy tienen marcas `0x3f8fdc8e`–`0x3f8fde13` (el mismo día): no hay mezcla de versiones.
- **v1.01:** el título de la ventana que crea el propio juego es `Max Payne 2 v1.01`.

## Archivos de terceros en la instalación

- `d3d9.dll` (cargador ASI) y `scripts/MaxPayne2.WidescreenFix.asi` son de terceros, de 2016: no se recompilan.

## Módulos

| Archivo | Tamaño | Base preferida | SizeOfImage | Entrada | Relocs | Exports | TLS | Enlazador |
|---|---:|---|---:|---|---:|---:|---|---|
| `BugReport.exe` | 49152 | `0x00400000` | `0xc000` | `0x39bd` (.text) | sin (.reloc eliminado) | 0 | no | 7.10 |
| `d3d9.dll` | 1188864 | `0x10000000` | `0x128000` | `0x1902c` (.text) | 6799 | 302 | sí | 14.0 |
| `DirectX/dxwebsetup.exe` | 292184 | `0x01000000` | `0x49000` | `0x5a5e` (.text) | sin (.reloc eliminado) | 0 | no | 7.0 |
| `e2driver/binkw32.dll` | 375808 | `0x30000000` | `0x72000` | `0x6ab1` (.text) | 9803 | 85 | no | 7.0 |
| `e2driver/e2_d3d8_driver_mfc.dll` | 495616 | `0x10000000` | `0x87000` | `0x559b2` (.text) | 8787 | 278 | no | 7.10 |
| `e2mfc.dll` | 421888 | `0x10000000` | `0x6a000` | `0x3e65b` (.text) | 10257 | 1264 | no | 7.10 |
| `eax.dll` | 135168 | `0x10000000` | `0x21000` | `0xe5fb` (.text) | 1745 | 7 | no | 6.0 |
| `grphmfc.dll` | 122880 | `0x10000000` | `0x1e000` | `0x13d1b` (.text) | 1617 | 116 | no | 7.10 |
| `KF2MFC.dll` | 368640 | `0x10000000` | `0x5b000` | `0x35972` (.text) | 7414 | 585 | no | 7.10 |
| `MaxPayne2.exe` | 1486848 | `0x00400000` | `0x16da50` | `0xd34f0` (.text) | sin (.reloc eliminado) | 0 | no | 7.10 |
| `mfc71.dll` | 1060864 | `0x7c140000` | `0x103000` | `0xf41a` (.text) | 31094 | 6443 | no | 7.10 |
| `msvcp71.dll` | 499712 | `0x7c3a0000` | `0x7b000` | `0x2db0` (.text) | 5971 | 2903 | no | 7.10 |
| `msvcr71.dll` | 348160 | `0x7c340000` | `0x56000` | `0x229f` (.text) | 5243 | 837 | no | 7.10 |
| `oleacc.dll` | 176128 | `0x7acc0000` | `0x2b000` | `0x11730` (.text) | 3383 | 16 | no | 5.0 |
| `PSysLibMFC.dll` | 106496 | `0x10000000` | `0x1a000` | `0xf876` (.text) | 1972 | 51 | no | 7.10 |
| `rlmfc.dll` | 225280 | `0x10000000` | `0x61000` | `0x1ba90` (.text) | 6264 | 503 | no | 7.10 |
| `scripts/MaxPayne2.WidescreenFix.asi` | 222720 | `0x10000000` | `0x3c000` | `0x943e` (.text) | 4348 | 0 | sí | 14.0 |
| `sndmfc.dll` | 106496 | `0x10000000` | `0x1a000` | `0xebca` (.text) | 2235 | 220 | no | 7.10 |
| `SysInfo.dll` | 49152 | `0x10000000` | `0xe000` | `0x5ea8` (.text) | 1019 | 69 | no | 7.10 |
| `T_GraphMFC.dll` | 36864 | `0x10000000` | `0x9000` | `0x3bcb` (.text) | 551 | 24 | no | 7.10 |
| `unins000.exe` | 1565755 | `0x00400000` | `0x184000` | `0xff004` (.itext) | sin (.reloc eliminado) | 0 | sí | 2.25 |
| `X_AudioMFC.dll` | 221184 | `0x10000000` | `0x37000` | `0x1cb7e` (.text) | 6227 | 303 | no | 7.10 |
| `X_BasicModesMFC.dll` | 315392 | `0x10000000` | `0x50000` | `0x2b183` (.text) | 9523 | 94 | no | 7.10 |
| `X_GameObjectsMFC.dll` | 1912832 | `0x10000000` | `0x1d6000` | `0x105348` (.text) | 60581 | 1154 | no | 7.10 |
| `X_HelpersMFC.dll` | 53248 | `0x10000000` | `0xd000` | `0x505a` (.text) | 933 | 93 | no | 7.10 |
| `X_Inputmfc.dll` | 77824 | `0x10000000` | `0x14000` | `0x91eb` (.text) | 2100 | 167 | no | 7.10 |
| `X_LevelMFC.dll` | 126976 | `0x10000000` | `0x1f000` | `0xf441` (.text) | 2256 | 310 | no | 7.10 |
| `X_LevelRuntimeMFC.dll` | 118784 | `0x10000000` | `0x1d000` | `0xe26a` (.text) | 1593 | 255 | no | 7.10 |
| `X_MessageMFC.dll` | 135168 | `0x10000000` | `0x21000` | `0x11b3b` (.text) | 3106 | 240 | no | 7.10 |
| `X_ModesMFC.dll` | 204800 | `0x10000000` | `0x32000` | `0x1827e` (.text) | 4835 | 538 | no | 7.10 |
| `X_PhysicalSimulationMFC.dll` | 860160 | `0x10000000` | `0xd4000` | `0x9c132` (.text) | 17826 | 604 | no | 7.10 |
| `X_SharedDBMFC.dll` | 434176 | `0x10000000` | `0x6e000` | `0x3314b` (.text) | 11929 | 946 | no | 7.10 |

## SHA-256

| Archivo | SHA-256 |
|---|---|
| `BugReport.exe` | `362c9c66d26d573215e3c1f0cc897aca061ae6f6cc371d46ea268581e8173747` |
| `d3d9.dll` | `f8dbac94d2e8e39503544fc25a3e1568ba48399e3c215f24a175f18d4471870a` |
| `DirectX/dxwebsetup.exe` | `34f64699d4830145cae69bd40115b1f326e70fc6a98456cb3df996d947dddca4` |
| `e2driver/binkw32.dll` | `2d0ae23a6175dc7b635c402a5e7e9542e923c0d1c376a8c5ef876ca0d5959d23` |
| `e2driver/e2_d3d8_driver_mfc.dll` | `9d72637c51f7a17dd9d95b079e5508ecbe8f1799c34395e0876945d741ff11ae` |
| `e2mfc.dll` | `9107267fd91d101782f00689431d2faaf7ec8e1984b1a37a189f9c26c09d4d72` |
| `eax.dll` | `e56ee2d67bfeebbbcaa154b61e0ef248b0a25d6a1e65450f0ed86231d1768e1c` |
| `grphmfc.dll` | `1a7c6974f66cd7633bcaa7ae799936b301acedb64549e8a0ab734d1e218e8248` |
| `KF2MFC.dll` | `ae16ab1360d20c1478334e9d55b989b782baf6db8a3f5a67d95537f882b469ee` |
| `MaxPayne2.exe` | `1e25cafc37174a607b58a38dfac796056f0f38f35d253b0b2d7677f9bd7bd010` |
| `mfc71.dll` | `4da5efdc46d126b45daeee8bc69c0ba2aa243589046b7dfd12a7e21b9bee6a32` |
| `msvcp71.dll` | `df96156f6a548fd6fe5672918de5ae4509d3c810a57bffd2a91de45a3ed5b23b` |
| `msvcr71.dll` | `8094af5ee310714caebccaeee7769ffb08048503ba478b879edfef5f1a24fefe` |
| `oleacc.dll` | `195c173a2009ca957b93fad00f2bff24597cdba022258dddd0014b4069979ff7` |
| `PSysLibMFC.dll` | `94be26fe728fb5e3cda23398245846555f20774093346e39f1ba78ca48b5f3e5` |
| `rlmfc.dll` | `68e4d7d99d8db08bd05d414fa224eae921a72760ed8b93e377f8c51597ea2e61` |
| `scripts/MaxPayne2.WidescreenFix.asi` | `3ba1f25bd374f104aa4833b84823113a111456e8b4d49969b300202cee6af111` |
| `sndmfc.dll` | `82940ecb52cdf7ccbf9115f88582ae718f6da9ed8d2a0864e6078a067b41782c` |
| `SysInfo.dll` | `f82d757ccafbf914b07f41bb367a5067986a7b68169bed654ae1e66e5fda66a4` |
| `T_GraphMFC.dll` | `0d1fc2c9dae1af032b19e29eb8083a73fee95ffed32d97bbfe863f6aa91c8419` |
| `unins000.exe` | `122cf5229aa47727ca1711a2fdd7774f9fed33ea920fa258abd77e3602fa3788` |
| `X_AudioMFC.dll` | `975efe0460c7417ed15778796c4362f7b24a908800bd53577c0c2a1a4ecebb27` |
| `X_BasicModesMFC.dll` | `7812180685d6cc1821b94c71739226ecd461052e2c204e46b824b486bdaf6895` |
| `X_GameObjectsMFC.dll` | `98823044e9ba0e9dfd61c0008c1831a2372cfccd7a287314cc93e50b30ae2628` |
| `X_HelpersMFC.dll` | `770410fe92095c1e0f768f4e10168c8fc551bc0cf6aec03f83c21b12c426689e` |
| `X_Inputmfc.dll` | `196dd65447d6614c7ede82fb6f4bf902712df1dbfe15df2589d99247dd69c27c` |
| `X_LevelMFC.dll` | `0c37c97e81d93619db6f0a4b03947c4a05a4a45aef50b5df970ee4a58463ccd7` |
| `X_LevelRuntimeMFC.dll` | `42565f3579eaf1f2d592cd4b60db9257d1bb1cc5d9f6de17d4a57545c14252b3` |
| `X_MessageMFC.dll` | `0cb2b59eec07c6fec01e5390f2a06977c8bfd733005eca87218477baec2649de` |
| `X_ModesMFC.dll` | `5491f25aa2d6986511c3af32754108dde2a0220c0aeb69c8db16f0f541c26556` |
| `X_PhysicalSimulationMFC.dll` | `cd97cb986712f7f540e7af83353f4980b6c7affc997bd657946b78ec3f77c99f` |
| `X_SharedDBMFC.dll` | `02684e6465ea6097a50d01a48d785d98b3197df1966f4ccb1d18e3b576375c54` |

## Secciones de los módulos del juego

- `e2driver/binkw32.dll`: .text (0xd4f0, H=6.684), BINK (0x12d9a, H=6.586), BINK16 (0x191d, H=6.105), BINK4444 (0x1ccf, H=6.113), BINK5551 (0x18e6, H=6.174), BINK32 (0x186a, H=6.035), BINK32A (0x1cc0, H=6.122), BINK16X2 (0xc2a, H=5.726), BINK32X2 (0xca6, H=5.949), BINK16MX (0x203, H=3.894), BINK32MX (0x2af, H=4.821), BINK16M (0x1f7, H=5.828), BINK32M (0x223, H=4.086), BINKYUY2 (0x169, H=4.782), BINKYUY2 (0xd7f, H=5.889), BINKYUY2 (0x1ec, H=5.727), BINKYUY2 (0x124, H=4.05), BINK32R (0x146a, H=6.069), BINK32RA (0x1640, H=6.106), BINK32RX (0xca6, H=5.947), BINK24 (0x1cb6, H=6.106), BINK24X2 (0x105e, H=6.013), BINK24M (0x2ca, H=5.044), BINK24R (0x1826, H=6.081), BINK24RX (0x105e, H=6.013), BINK24RM (0x324, H=5.471), BINKUYVY (0x1e8, H=5.759), BINKUYVY (0x171, H=4.92), BINKUYVY (0xe0f, H=5.602), BINKUYVY (0x147, H=4.216), BINKYV12 (0x2c0, H=4.666), .rdata (0xaf17, H=6.428), .data (0x3464, H=5.135), BINKDATA (0xfcb0, H=2.732), .rsrc (0x2130, H=4.715), .reloc (0x5364, H=6.671)
- `e2driver/e2_d3d8_driver_mfc.dll`: .text (0x56475, H=6.74), .rdata (0x14a18, H=5.65), .data (0x12164, H=4.503), .data1 (0x900, H=2.508), .reloc (0x5ff8, H=5.769)
- `e2mfc.dll`: .text (0x40c5b, H=6.465), .rdata (0x1c8c6, H=5.651), .data (0x3a50, H=4.946), .reloc (0x69fc, H=5.713)
- `grphmfc.dll`: .text (0x137e5, H=6.406), .rdata (0x56d8, H=5.215), .data (0x864, H=2.533), .reloc (0x185e, H=3.612)
- `KF2MFC.dll`: .text (0x380e4, H=6.446), .rdata (0x18ac3, H=5.37), .data (0x1c68, H=4.353), .reloc (0x5e8a, H=5.038)
- `MaxPayne2.exe`: .text (0xe3000, H=6.291), .rdata (0x59da6, H=4.918), .data (0x1bc68, H=4.822), .rsrc (0x13a50, H=3.08)
- `mfc71.dll`: .text (0xdb1d6, H=6.542), .data (0x996c, H=3.923), .rsrc (0xb478, H=3.422), .reloc (0xfa70, H=6.47)
- `msvcp71.dll`: .text (0x2ec98, H=6.633), .rdata (0x42a32, H=5.522), .data (0x2168, H=4.063), .rsrc (0x3c0, H=1.027), .reloc (0x30c4, H=5.713)
- `msvcr71.dll`: .text (0x38ba8, H=6.756), .rdata (0x10040, H=6.011), .data (0x6820, H=2.481), .rsrc (0x3b8, H=1.031), .reloc (0x2b64, H=6.423)
- `PSysLibMFC.dll`: .text (0xf67b, H=6.295), .rdata (0x5488, H=4.732), .data (0x7a0, H=2.397), .reloc (0x1df8, H=4.22)
- `rlmfc.dll`: .text (0x1ca44, H=6.429), .rdata (0x100c7, H=5.181), .data (0x2c198, H=4.871), .reloc (0x4732, H=5.03)
- `sndmfc.dll`: .text (0xe8b4, H=6.158), .rdata (0x6806, H=5.144), .data (0xa90, H=3.117), .reloc (0x1e28, H=4.632)
- `T_GraphMFC.dll`: .text (0x31ab, H=5.424), .rdata (0x18e8, H=4.104), .data (0x3fc, H=1.232), .reloc (0xd20, H=2.589)
- `X_AudioMFC.dll`: .text (0x1deb4, H=6.358), .rdata (0xf33a, H=5.239), .data (0x2a6c, H=4.719), .reloc (0x4948, H=5.055)
- `X_BasicModesMFC.dll`: .text (0x2d674, H=6.316), .rdata (0x1086c, H=4.856), .data (0x74cc, H=4.784), .reloc (0x7222, H=4.877)
- `X_GameObjectsMFC.dll`: .text (0x118934, H=6.406), .rdata (0x6ffe9, H=5.237), .data (0x1f3ec, H=4.99), .reloc (0x2b910, H=5.431)
- `X_HelpersMFC.dll`: .text (0x47c5, H=5.864), .rdata (0x3e0a, H=5.359), .data (0x520, H=1.318), .reloc (0x10dc, H=2.29)
- `X_Inputmfc.dll`: .text (0x8f75, H=5.88), .rdata (0x57d6, H=5.067), .data (0x1b14, H=3.51), .reloc (0x1c24, H=4.36)
- `X_LevelMFC.dll`: .text (0xf3b4, H=6.21), .rdata (0x95a8, H=5.227), .data (0x958, H=2.389), .reloc (0x206c, H=3.434)
- `X_LevelRuntimeMFC.dll`: .text (0xe164, H=6.174), .rdata (0x92b7, H=5.168), .data (0x638, H=1.783), .reloc (0x1bda, H=3.574)
- `X_MessageMFC.dll`: .text (0x12154, H=6.257), .rdata (0x87e1, H=5.083), .data (0xfd0, H=4.097), .reloc (0x2910, H=4.399)
- `X_ModesMFC.dll`: .text (0x18cf4, H=6.235), .rdata (0x11ee3, H=5.443), .data (0x1bd8, H=4.576), .reloc (0x3ce4, H=4.915)
- `X_PhysicalSimulationMFC.dll`: .text (0x9fd14, H=6.493), .rdata (0x2112e, H=5.599), .data (0x3468, H=4.015), .reloc (0xc108, H=5.5)
- `X_SharedDBMFC.dll`: .text (0x38044, H=6.378), .rdata (0x21ef4, H=5.432), .data (0x8528, H=4.354), .reloc (0x8162, H=5.282)
