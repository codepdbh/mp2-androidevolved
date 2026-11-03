# Recompilación de PC — iteración 1, 2026-11-03

Herramienta: PCRecomp `nfsu2-fixes` (`b278e8a`) en `external/pcrecomp`, más `patches/pcrecomp/0001-seh-frames-and-or-xor-conditions.patch`.

## Reproducir

```powershell
.\.venv\Scripts\python scripts\pc_analysis\pe_inventory.py "Max Payne 2 The Fall of Max Payne" work\pc\pe_inventory.json
.\.venv\Scripts\python scripts\pc_recomp\layout_modules.py work\pc\pe_inventory.json config\pc\modules.json
.\.venv\Scripts\python scripts\pc_recomp\recompile_modules.py --jobs 2
.\.venv\Scripts\python scripts\pc_recomp\merge_dispatch.py work\pc\gen\merged
powershell -File scripts\pc_recomp\boot_host.ps1 -Log boot
powershell -File scripts\pc_recomp\run_difftests.ps1
.\.venv\Scripts\python scripts\pc_recomp\summarize.py
```

Todo lo generado queda en `work/pc/`, ignorado por Git.

## Resultado por módulo

﻿| Módulo | Funciones | Líneas C | Fallos de lift | Instr. sin implementar | Excluidas (datos) |
|---|---:|---:|---:|---:|---:|
| `MaxPayne2.exe` | 12267 | 1068251 | 0 | 1 | 0 |
| `msvcr71.dll` | 3172 | 1096823 | 0 | 34 | 2 |
| `msvcp71.dll` | 3460 | 213952 | 0 | 0 | 0 |
| `mfc71.dll` | 16342 | 1347735 | 0 | 4066 | 1419 |
| `rlmfc.dll` | 1554 | 154353 | 0 | 1 | 1 |
| `grphmfc.dll` | 651 | 70671 | 0 | 1 | 1 |
| `e2mfc.dll` | 2911 | 248969 | 0 | 1 | 1 |
| `T_GraphMFC.dll` | 188 | 10457 | 0 | 0 | 0 |
| `KF2MFC.dll` | 2857 | 240244 | 0 | 1 | 1 |
| `PSysLibMFC.dll` | 748 | 72223 | 0 | 0 | 0 |
| `sndmfc.dll` | 722 | 47635 | 0 | 0 | 0 |
| `X_HelpersMFC.dll` | 335 | 16515 | 0 | 0 | 0 |
| `X_SharedDBMFC.dll` | 3177 | 221197 | 0 | 0 | 0 |
| `X_MessageMFC.dll` | 974 | 81240 | 0 | 0 | 0 |
| `X_Inputmfc.dll` | 538 | 27345 | 0 | 1 | 1 |
| `X_PhysicalSimulationMFC.dll` | 4659 | 1855604 | 0 | 0 | 0 |
| `X_LevelMFC.dll` | 846 | 70242 | 0 | 1 | 1 |
| `X_LevelRuntimeMFC.dll` | 872 | 55562 | 0 | 0 | 0 |
| `X_AudioMFC.dll` | 1559 | 100101 | 0 | 0 | 0 |
| `X_ModesMFC.dll` | 1392 | 87345 | 0 | 0 | 0 |
| `X_BasicModesMFC.dll` | 2315 | 180593 | 0 | 0 | 0 |
| `X_GameObjectsMFC.dll` | 14333 | 1092267 | 0 | 1 | 1 |
| `e2driver/binkw32.dll` | 1114 | 457752 | 0 | 17 | 1 |
| `e2driver/e2_d3d8_driver_mfc.dll` | 2907 | 620178 | 0 | 2879 | 5 |
| **Total** | 79893 | 9437254 | 0 | 7004 | 1434 |

Las cifras incluyen funciones falsas que aún no se han filtrado: el número no es un recuento exacto de funciones reales.

- **`mfc71.dll`.** Guarda sus datos dentro de `.text`, así que casi todas sus instrucciones sin implementar son datos decodificados.
- **`e2_d3d8_driver_mfc.dll`.** Casi todas son 3DNow! de D3DX; el host quita 3DNow! de CPUID.
- **Fallo ruidoso.** Cualquiera de ellas que llegue a ejecutarse detiene el proceso con su dirección: no se oculta.

## Correcciones al flujo heredado de Carbon

- **Falsos arranques.** Se decide sobre el bloque de entrada, y un export en código no se excluye. El filtro original condenaba `R_Script::parse` y `R_System::determineProcessorCaps`.
- **Semillas de datos.** Una semilla relocalizada se descarta si su flujo lineal contiene instrucciones que MSVC no emite. Tapaba un inicializador real de `mfc71` en `0x6EA8DC`.
- **`pushaw`/`popaw`.** Añadidas en `mw_lifter.py`: `determineProcessorCaps` las ejecuta en el arranque.
- **Etiquetas sin definir.** Las funciones cuyo C no compilaría se excluyen tras el lift (2 en `mfc71`).
- **Compilaciones incrementales.** El lift no reescribe archivos sin cambios.
- **Arnés de difftest.** La base de la imagen ya no es fija en `0x400000`.

## Corrección del lifter: condiciones tras `or` y `xor`

`or ecx, eax; jge` generaba `CMP_GE(r, r)`, que siempre es cierto. Por eso `CString::GetBuffer` de `mfc71` nunca reservaba memoria y el arranque moría con `COleException` (`E_INVALIDARG`). El propio lift32 lo marcaba como aproximación pendiente. Ahora `or` y `xor` reducen las condiciones de orden igual que `test`. **El mismo bug está en tus ports de NFS**, que usan este lifter: conviene aplicar el parche y comprobarlos.

`tools/lift/difftest.py` tiene 16 casos nuevos. Con el lifter anterior fallan 8; con el corregido, el total es 262 de 262.

## Filtro por relocalizaciones

En una DLL, cada relocalización marca un puntero de 4 bytes. Si una decodificación la pisa sin que sean exactamente un `disp32` o `imm32`, son datos. Es la mejor señal de la iteración: sacó del catálogo los falsos arranques de `mfc71`, que tapaban el gancho CBT de MFC. En módulos de código puro marca 32 de 510 007 instrucciones, todas tablas de salto decodificadas.

Además, las semillas relocalizadas que quedan tapadas por otra función se añaden igualmente: dos funciones solapadas no se estorban.

## Pruebas diferenciales

Funciones hoja reales: bytes originales en Unicorn contra C recompilado compilado para x64.

| Módulo | Funciones | Casos | Coinciden | Fallan | Omitidos | Clases de fallo |
|---|---:|---:|---:|---:|---:|---|
| `e2_d3d8_driver_mfc` | 250 | 286 | 283 | 3 | 464 | fault: unicorn=True lifted=False ×3 |
| `e2mfc` | 250 | 328 | 326 | 2 | 422 | fault: unicorn=True lifted=False ×2 |
| `exe` | 250 | 437 | 434 | 3 | 313 | fault: unicorn=True lifted=False ×3 |
| `msvcr71` | 250 | 475 | 441 | 34 | 275 | fault: unicorn=True lifted=False ×20; st0: cpu nan lifted 0.0 ×9; st0: cpu inf lifted 1.0 ×2; eax: cpu 00000000 lifted 00004000 ×3; ecx: cpu 10080000 lifted 10084000 ×1; ecx: cpu 00000000 lifted 00004000 ×2 |
| `rlmfc` | 212 | 389 | 386 | 3 | 247 | fault: unicorn=True lifted=False ×3 |
| `x_gameobjectsmfc` | 250 | 436 | 435 | 1 | 314 | fault: unicorn=True lifted=False ×1 |
| `x_physicalsimulationmfc` | 250 | 338 | 330 | 8 | 412 | fault: unicorn=True lifted=False ×8 |
| **Total** | 1712 | 2689 | 2635 | 54 | 2447 | |

Solo cubre funciones hoja: nada de llamadas, saltos indirectos ni la lógica del juego en conjunto. Las divergencias reales de `msvcr71` están en [BLOCKERS.md](BLOCKERS.md).

# Historial PS2 — revisión del avance del usuario, 2026-10-28

Actualización posterior: se corrigieron 39 instrucciones VSUB a VF0 en 20 funciones mediante un posprocesador verificable e idempotente. El ELF original conserva su hash. Pasan 25 CTest; esto no valida gameplay ni resuelve los menús invertidos.

El manifiesto de programa completo registra20876 funciones y467266 PCs, sin
guards pendientes. Esta revisión corrigió runtime y pruebas; conserva los
cuerpos recompilados y el SHA-256 del ELF.21 CTest pasan y los objetos del runtime
compilan para ARM64. Registro completo no significa ejecución correcta de todo
el juego. Persisten defectos gráficos; sin gameplay validado.
[Detalle de la revisión](REVIEW_2026-10-28.md).

# Historial — iteración 5

51 cuerpos originales seleccionados:41 del ELF PAL y10 del bloque de alarmas;
182 PCs registrados, sin stubs/skips ni instrucciones parcheadas. Ambas
recompilaciones tienen cero warnings/errores e instrucciones no manejadas.
Recuperada raíz original de hilo0x00100220 a partir del argumento de SetupThread
y sus dos instrucciones verificadas. No se modifica el ELF original.

El auxiliar original inicia, espera su semáforo y devuelve CPU al principal;
la ejecución llega a GetOsdConfigParam0x4B, PC0x0010D698. Ocho CTest, dos unittest
y pruebas de APK v0.4 pasan; trazas x64/ARM64 coinciden. Sin gameplay ni validación
del resto del juego. [ITERATION_05.md](ITERATION_05.md).

# Historial — iteración 4

33 funciones del ELF PAL más 10 funciones del bloque original de alarmas,
recompilado en la dirección a la que el juego lo copia. 159 PCs registrados,
cero stubs/skips, cero instrucciones no manejadas y cero warnings/errores en
ambos subconjuntos. Cuerpos generados sin parches. El callback de copia y el
trampolín de retorno se recuperaron mediante evidencia de las tablas originales.

La ejecución verifica las copias, instala los servicios originales y supera
la configuración de timer2. Se detiene en CreateThread, PC0x0010D3E8.
Trazas x64/ARM64 coinciden; siete CTest y pruebas de APK v0.3 pasan.
Esto sigue siendo inicialización parcial, sin gameplay.
Véase [ITERATION_04.md](ITERATION_04.md).

# Historial — iteración 3

El ELF PAL conserva su SHA-256 original. Se seleccionaron10 funciones con límites
de Ghidra y se tradujeron por separado: 0 stubs/skips, 0 instrucciones no manejadas,
0 warnings/errores, 39 etiquetas internas y49 PCs registrados contando entradas.
Dos callbacks de la propia tabla en0x0054CEC0 fueron recuperados como funciones:
0x001166F8 (copia de palabras) y0x00116730 (búsqueda de palabras). Antes estaban
desensamblados, pero no definidos como funciones; no se alteraron bytes del ELF.

Un executor plano reanuda los puntos generados tras llamadas/retornos y checkpoints.
Las llamadas a callbacks de syscall conservan explícitamente RA y el PC de retorno.
El código original crea dos semáforos y guarda sus IDs en0x0054CED0/0x0054CED4;
instala dos callbacks y descubre la tabla de servicios creada por MP2R. Se verificó
con dos bases distintas de la tabla y trazas idénticas en x64 y ARM64.

La ejecución ahora alcanza PC0x00116DC4 y se detiene ante la lectura MMIO de
RCNT3_MODE0x10001810. Esto cubre inicialización de plataforma parcial, no el juego.
La APK carga el ELF verificado desde `mp2androidevolved`, copia sus segmentos en RAM
y ejecuta las mismas traducciones. Véase [ITERATION_03.md](ITERATION_03.md).

# Historial — iteración 2

Ghidra 12.1.4 con Emotion Engine v2.1.38 analizó el ELF en 103s de análisis.
Exportador upstream sin modificar, por argumentos headless: 17145 funciones y
69239 etiquetas. La exportación propia conserva rangos de cuerpo: 16165 contiguos
y 980 fragmentados. Solo los contiguos se tradujeron en esta prueba.
`config/mp2-pal-ghidra.toml`: 0 stubs, 0 skips, 0 instrucciones no manejadas,
6022 warnings y 332653 entradas adicionales. Estas métricas no prueban ejecución
completa ni cubren los 980 cuerpos excluidos.

GP confirmado en `.reginfo` y en el propio arranque: 0x005C8870.
El código original borra BSS `[0x005C0D00,0x006E4C00)`, 1195776 bytes.
La prueba de 444 bytes ejecuta instrucciones sin parches y valida RAM, registros
y reanudación con intervalos de checkpoint 1 y 4096, en x64 y Android ARM64.

La entrada completa de Ghidra se compiló con `BootstrapRuntime`, adaptador nativo
limitado que registra/configura los parámetros de SetupThread/SetupHeap. Se verifican
SP=0x02000000 y heap_base=0x006E4C00, y se detiene en el JAL 0x001001E8 hacia
0x001168C8. No implementa scheduler general, allocator, IOP o GPU. Los servicios
tienen pruebas locales contra el contrato observado; falta comparación diferencial PS2.
No se devuelve éxito para funciones ausentes.

La misma prueba ejecuta dentro de la .so de la APK de diagnóstico vía JNI.
El código generado conserva los cuerpos originales; los puntos de plataforma se
vinculan explícitamente al adaptador. No es el runtime upstream completo.
Detalles y logs en [ITERATION_02.md](ITERATION_02.md).

# Historial: iteración 1, 2026-10-26

PS2Recomp: `e5636981a1c9de368f6b71e819c3dc188dfedbf4`.
Referencias: https://github.com/ran-j/PS2Recomp y fuentes fijadas en
`config/dependencies.lock.json`. No se modificó el repositorio externo.

## Evidencias

- MSVC 19.51.36260.0, CMake 4.4.4: compilan ps2_recomp y ps2_analyzer.
- Analizador fallback: 14632 funciones heurísticas, 0 símbolos, 8 secciones,
  0 relocaciones; 678 coincidencias SDK (487 renombradas, 191 añadidas).
  Propone 211 stubs, detecta 428 funciones de biblioteca sin manejador,
  29 posibles parches y 7 tablas de saltos. No son contratos validados.
- `config/mp2-pal-experimental.toml`: sin stubs, sin patches, modo de memoria
  reducida, un escritor. El informe upstream dice 14632 funciones procesadas,
  638070 entradas adicionales, 4902 warnings y **16522 errores** de instrucciones.
  Exit code 0 y “completed successfully” no significan compilación funcional.
- La última función heurística llega a datos: raw `0x74736f68` corresponde a
  bytes de texto. Primero separar código/datos; no implementar opcodes falsos.
  GP=0 y “No BSS found” también requieren corrección mediante análisis del arranque.
- C++ privado: 14633 archivos .cpp, 381586092 bytes en esta generación.
  No se compiló ni ejecutó el conjunto completo.

## Dos funciones originales aisladas

Se compilaron **sin editar su C++ generado**, con headers/macros/contexto originales
de PS2Recomp y sin construir PS2Runtime:

| Entrada PAL | Bytes verificados | Contrato observado | Estado |
|---|---|---|---|
| 0x0010DCC8 | JR RA; ADDIU V0,ZERO,-1 | Retorna -1; delay slot ejecutado | EJECUTA y VALIDADA solo prueba aislada |
| 0x001B1C20 | LUI V0,0x1194; JR RA; NOP | Retorna 0x11940000 | EJECUTA y VALIDADA solo prueba aislada |

Se verificaron también JAL originales desde 0x00123A08 y 0x001AB680 respectivamente.
La finalidad en el juego de estas funciones no está identificada. El harness prueba
4 semillas del registro × 3 direcciones de retorno = 12 casos para cada función,
sign extension de -1, retorno PC y limpieza del estado delay slot. No valida MMI,
VU, memoria, syscalls, dispatcher, física ni boot. No se cargó BIOS.

Windows x64: `build/mp2r/Release/recomp_leaf_probe.exe` pasó.
Android: NDK 28.2.13676358, API26, arm64-v8a, Ninja 1.13.2,
sse2neon v1.9.1 fijado. `llvm-readelf -h` confirma ELF64 AArch64.
Se ejecutó vía ADB en el dispositivo conectado; ABI arm64-v8a y páginas 4096.
Resultado real: `PASS: two original PAL leaf translations, 12 register/return cases; ADDIU delay slot and LUI. No game boot.`
No se verificaron páginas 16KB ni otro teléfono. No hay APK.

## Arquitectura upstream revisada

`ps2xRuntime` mantiene RAM, R5900Context, dispatch y syscalls;
`ps2xIOP` ejecuta R3000A/IRX y servicios. Los Game Override Hooks deben enlazarse
por identidad exacta del ELF, nunca por serial solamente. MMI/VU0 tienen traductores,
pero su presencia no prueba equivalencia ni soporte completo del título.
Los headers usan SSE y una ruta USE_SSE2NEON. La prueba ARM64 ejercita solo las
operaciones usadas por las dos funciones. El CMake upstream puede habilitar crypto/CRC
por soporte del compilador; eso no demuestra disponibilidad en toda CPU Android.
El runtime completo usa raylib y componentes gráficos: no se ejecutó su runner.

Ghidra: extensión Emotion Engine descargada y fijada; lenguaje confirmado
`r5900:LE:32:default`. No se encontró instalación en las ubicaciones inspeccionadas.
`scripts/ghidra_export.ps1` prepara importación headless limitada a dos CPU/300s.
Exportador upstream: `external/PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java`.
Su selección de salida es interactiva; no se afirma exportación automatizada realizada.
Falta instalar Ghidra/extensión compatibles y generar un mapa fiable TOML/CSV.

Logs: `private/logs/analyzer-pal.log`, `recompile-pal.log`, `build-probe.log`,
`build-arm64-probe.log`; todos privados/excluidos. Próximo paso: recuperar límites
reales y comparar cobertura antes de intentar ejecutar el entry point.
