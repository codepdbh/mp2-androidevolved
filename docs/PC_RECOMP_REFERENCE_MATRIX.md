# Matriz de referencia de PCRecomp

Copias locales usadas: `C:\Users\Sistemas\Documents\nfsu2-androidevolved\`. Solo se ha leído de ellas; lo reutilizado se copió a este repositorio.

Estados: **INTEGRADO** (se usa y se ejecutó aquí), **PENDIENTE** (identificado, sin integrar), **SOLO REFERENCIA**.

## PCRecomp

Se usa el fork local `nfsu2-fixes`, commit `b278e8a`, clonado en `external/pcrecomp` (ignorado por Git). Tiene 17 commits propios sobre upstream y va 164 por detrás de `sp00nznet/pcrecomp`. Upstream no se ha integrado: esos 164 commits están sin evaluar.

| Componente | Archivo real | Utilidad | Adaptación | Estado |
|---|---|---|---|---|
| Análisis PE | `tools/pe/pe_analyze.py` | Secciones, IAT | Ninguna | INTEGRADO |
| Descubrimiento de funciones | `tools/disasm/disasm32.py` | Catálogo | Ninguna | INTEGRADO |
| Lifter x86 | `tools/lift/lift32.py`, `simd32.py` | x87, MMX, SSE, SSE2 | Ninguna | INTEGRADO |
| Tipos y macros | `runtime/recomp32/recomp_types.h` | Modelo de registros y memoria | Ninguna | INTEGRADO |
| Host de 32 bits | `runtime/native32/native32.c` | Puente guest↔Windows, multimódulo | Sin aplicar relocs: se mapean copias ya relocalizadas | INTEGRADO |
| Difftest por instrucción | `tools/lift/difftest.py` | lift32 contra Unicorn | 16 casos nuevos | INTEGRADO |
| Win32 HLE | `runtime/win32hle/` | Win32 en el host | Sin SEH (`RaiseException` es fatal) | SOLO REFERENCIA |
| CPU interpretada | `tools/lift/lift32_cpu.py`, `runtime/recomp32_cpu/` | Alternativa a `lift32` | Tus ports usan `lift32` | SOLO REFERENCIA |

## Tus ports

| Proyecto | Componente | Archivo real | Utilidad | Adaptación | Estado |
|---|---|---|---|---|---|
| nfsu2-recompiled | Parches de PCRecomp | `patches/pcrecomp/0001…0014` | Ya presentes en `nfsu2-fixes` | Ninguna | INTEGRADO |
| nfsu2-recompiled | Difftest de funciones reales | `scripts/difftest_real.py` | Unicorn contra C recompilado | La base de la imagen ya no es fija | INTEGRADO |
| nfsu2-recompiled | Huecos de cobertura | `scripts/coverage_gaps.py` | Bytes de `.text` sin catalogar | Copiado, sin ejecutar | PENDIENTE |
| nfsu2-recompiled | Puerto de DXVK | `ports/dxvk_android/` | D3D9→Vulkan en Android | **Sin frontend D3D8** | PENDIENTE |
| nfsu2-recompiled | Trazas de D3D9 | `src/runtime/d3d9_trace.c` | Medir uso real de la API | Adaptar a D3D8 | PENDIENTE |
| nfsu2-recompiled | Reparto de fuentes para Android | `scripts/split_android_sources.py` | Compilar `.c` enormes | Ninguna prevista | PENDIENTE |
| nfscarbonpc | Driver de lift | `scripts/run_lift.py`, `mw_lifter.py` | Catálogo y lift por módulo | Rutas de config | INTEGRADO |
| nfscarbonpc | Base fija por módulo | `scripts/prepare_module.py`, `prepare_modules.py` | Rebase de DLL | Filtro de falsos arranques corregido (abajo) | INTEGRADO |
| nfscarbonpc | Host de Windows | `src/host/main.c`, `src/runtime/nfs_runtime.c` | Lanzador y reserva de rango | Reescrito para 24 módulos | INTEGRADO |
| nfscarbonpc | Runtime de Android | `android/app/src/main/cpp/guest_*.inc`, `guest_runtime.cpp` | Win32 sobre ARM64, heaps, hilos, archivos | Faltan GDI y MFC | PENDIENTE |
| nfscarbonpc | Cargador de módulos | `android/.../guest_modules.inc` | DLL recompiladas en Android | Pasar a 24 módulos; llevar `modules.json` | PENDIENTE |
| nfscarbonpc | Puente D3D9 | `android/.../d3d9_bridge.cpp`, `d3d9_device_methods.inc` | COM 32 bits → DXVK | Añadir la capa D3D8 | PENDIENTE |
| nfsmwpc | Launcher | `android/.../LauncherActivity.java`, `GameOptions.java`, `Lang.java`, `GpuDrivers.java` | Launcher probado, drivers | Quitar lo de conducción | PENDIENTE |
| nfsmwpc | Mando | `android/.../gamepad_input.h` | Mandos en Android | Mapeo propio de MP2 | PENDIENTE |
| nfsu1-recompiled | Semillas sin relocs | `scripts/data_seeds.py`, `code_seeds.py` | Catálogo de `MaxPayne2.exe` | Rutas | INTEGRADO |
| army-men-rts | DirectInput | `android/.../guest_dinput.inc` | `X_Inputmfc` usa `DirectInputCreateA` | Comparar con D3D8 | PENDIENTE |
| army-men-rts | Registro | `android/.../guest_registry.inc` | Registro virtual | MP2 guarda ajustes de entrada y vídeo ahí | PENDIENTE |
| army-men-rts | DirectDraw | `android/.../guest_ddraw.inc` | Modelo de objetos COM del invitado | D3D7 no es D3D8: solo infraestructura | SOLO REFERENCIA |
| reVC ENHANCED | Táctil | `android/launcher/.../TouchControlsView.java`, `src/skel/android/TouchControls.cpp` | Controles, gamepad | Sin librw | SOLO REFERENCIA |

## Corrección que conviene llevar a Carbon

`prepare_modules.py` marca una función como datos si cualquier bloque contiene una instrucción sospechosa. En MP2 eso condenaba a `R_Script::parse` (rlmfc), que importan cinco módulos, y a `R_System::determineProcessorCaps`, ambas código real. Aquí solo se examina el bloque de entrada y un export en código nunca se excluye (`scripts/pc_recomp/recompile_modules.py`). Los ports de NFS no se han modificado: comprueba si tienen el mismo falso positivo en sus DLL.

## Para llevar a tus ports de NFS

Tres cosas de esta rama pueden servirles, sin haberse probado allí:

- **Condiciones tras `or`/`xor`** (`patches/pcrecomp/`): un bug real del lifter compartido.
- **Filtro por relocalizaciones** en `scripts/pc_recomp/recompile_modules.py`.
- **Despacho SEH** (`desktop/pc_host/mp2_seh.c`, `RECOMP_SEH_FRAMES`): `guest_kernel.inc` de Carbon termina con `SEH dispatch is not implemented`. Es para el host x86; falta en Android también aquí.

## Licencias

- PCRecomp: MIT (`external/pcrecomp/LICENSE`, `NOTICE`).
- Los repositorios `nfsu2-recompiled`, `nfsmwpc-recompiled`, `nfsu1-recompiled`, `nfscarbonpc-androidevolved` y `army-men-rts-android-evolved` no declaran licencia en GitHub. Son tuyos, pero conviene declarar una antes de publicar esta rama con código copiado.
- reVC declara una licencia no estándar (`NOASSERTION`): solo se ha consultado.
- DXVK: zlib, con cabeceras derivadas de Wine (LGPL 2.1+), según `ports/dxvk_android/README.md`.
