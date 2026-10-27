# Estado actual — iteración 5, 2026-10-27

La APK v0.4 crea y ejecuta el primer hilo auxiliar original, lo bloquea en su
semáforo y restaura el hilo principal. Dos cambios de contexto verificados en x64
y ARM64. 51 funciones/182 PCs registrados; ocho CTest y dos unittest pasan,
además de la instrumentación Android. Nuevo límite: GetOsdConfigParam0x4B,
PC0x0010D698. **Todavía no es jugable.**

Detalle: [ITERATION_05.md](ITERATION_05.md). Siguiente trabajo: configuración
de sistema y comprobación original de parches; después continuar InitSys y los
servicios IOP. IRQ, multitarea completa, renderer y audio siguen pendientes.

# Historial — iteración 4, 2026-10-27

43 funciones originales y 159 PCs registrados. La instalación original del
bloque de alarmas termina, las copias se verifican y el timer2 se inicializa.
Reloj nativo de bus y estado INTC implementados; entrega de IRQ pendiente.
Parada actual: CreateThread, syscall0x20, PC0x0010D3E8, tres semáforos.
Siete CTest pasan; APK v0.3 e instrumentación pasan en el teléfono del proyecto.
Trazas x64/ARM64 idénticas. **Todavía no es jugable.**

Detalle y límites: [ITERATION_04.md](ITERATION_04.md).
Siguiente trabajo: planificador EE, esperas/despertares y entrega de interrupciones;
después continuar la inicialización y servicios IOP, gráficos y audio.

# Historial — iteración 3, 2026-10-27

- Dispatcher cooperativo con 49 entradas/reanudaciones exactas para 10 funciones
  originales seleccionadas. Los cuerpos C++ generados no se parchean.
- Carga del ELF real en RAM invitada, creación de dos semáforos EE y comprobación
  de los identificadores guardados por el propio código original.
- Recuperados dos callbacks address-taken que faltaban como funciones en Ghidra;
  SetSyscall instala esos callbacks y la rutina original descubre la tabla virtual.
- La búsqueda funciona con dos ubicaciones distintas de la tabla proporcionada por
  MP2R. No se usa una BIOS ni se atribuye esa tabla a un firmware original.
- Windows x64 y el teléfono ARM64 producen trazas idénticas. Seis CTest y dos
  unittest pasan. La instrumentación de la APK verifica la misma inicialización.
- APK v0.2 instalada en el dispositivo de trabajo, con iconos/imagen del usuario.
- Nuevo límite: lectura de RCNT3_MODE 0x10001810 desde PC0x00116DC4. No se inventa
  respuesta del temporizador. No hay menú, nivel, render de juego ni gameplay.

Evidencia y reproducción: [ITERATION_03.md](ITERATION_03.md).
Siguiente objetivo: contrato nativo de temporización/alarmas y la ruta que instala
sus manejadores, a partir de la rutina original 0x00116DA8.

# Historial — iteración 2, 2026-10-26

- Ghidra 12.1.4 + EE v2.1.38 instalados localmente y análisis completado.
- 16165 cuerpos contiguos recompilados: 0 instrucciones no manejadas, 6022 warnings.
  Quedan fuera 980 cuerpos fragmentados; no equivale a traducción completa validada.
- Arranque original ejecutado en host y ARM64 hasta 0x001168C8, con BSS y servicios
  iniciales de hilo/heap. El runtime se detiene explícitamente en la primera llamada pendiente.
- librw NULL cargó/decodificó 285 TXD del primer nivel; no hay renderer del juego.
- APK de diagnóstico instalada, JNI, editor de posiciones y multitouch probados.
- 504 archivos copiados a `/storage/emulated/0/mp2androidevolved` y SHA-256 verificados.
- Diez pictogramas PNG propios generados con imagegen e integrados en los botones.
- Sin gameplay, menú, audio ni APK jugable. Ninguna prueba de escritorio abrió ventana.

Detalle verificable, archivos y comandos en [ITERATION_02.md](ITERATION_02.md).
Siguiente objetivo: enlazar el dispatcher y la inicialización original desde 0x001168C8.

# Historial: iteración 1 — 2026-10-26

## 1. Objetivo realizado

Identificar la ISO y ELF; construir PS2Recomp; generar C++ experimental y ejecutar
dos funciones originales aisladas en Windows y Android ARM64. Crear núcleo input
propio y análisis inicial de las referencias. No se completó el port.

## 2. Archivos modificados/creados

- Raíz: `.gitignore`, `CMakeLists.txt`, `README.md`, `requirements-tools.txt`.
- `config/development.toml`, `config/mp2-pal-experimental.toml`, `config/dependencies.lock.json`.
- `src/input/touch_engine.h`, `src/input/touch_engine.cpp`.
- `tests/test_analysis.py`, `tests/touch_engine_test.cpp`, `tests/recomp_leaf_probe.cpp`.
- Scripts: `analyze_iso.py`, `inspect_elf.py`, `inspect_rw.py`, `write_iso_report.py`,
  `development.py`, `lock_dependencies.py`, `bootstrap_dependencies.py`,
  `build_tools.ps1`, `verify_probe_bytes.py`, `ghidra_export.ps1`.
- Documentos: `ISO_ANALYSIS.md`, `RECOMPILATION_REPORT.md`, `REVC_REFERENCE_ANALYSIS.md`,
  `TOUCH_HUD_DESIGN.md`, `WINDOWED_TEST_POLICY.md`, `BLOCKERS.md`, `PROGRESS.md`.
- Evidencia privada: manifiesto ISO, JSON ELF/RW, extracción, configuración propuesta
  por el analizador, C++ generado y logs en `private/`; binarios en `build/`.

## 3. Herramientas y comandos

Python3.14.2; pycdlib1.22.0; Git2.52.0.windows.1; CMake4.4.4; Ninja1.13.2;
psutil7.2.2; MSVC19.51.36260.0; Windows SDK10.0.26100.0; NDK28.2.13676358/API26.
Comandos reproducibles de escritorio en README; commits concretos en lock JSON.
Herramientas PS2Recomp configuradas sin runtime/test/studio; compilación `--parallel 2`.
Ejecutor limita procesos propios a 2048MB RSS agregado/300s. No altera prioridad global.

ARM64 se configuró con CMake/Ninja, `ANDROID_ABI=arm64-v8a`,
`ANDROID_PLATFORM=android-26`, el toolchain NDK y `MP2R_RECOMP_PROBE=ON`.
ADB: primero `devices`, luego `getprop ro.product.cpu.abilist` y `getconf PAGE_SIZE`.
Se copió únicamente el ejecutable del proyecto a `/data/local/tmp/mp2r-recomp-leaf-probe`,
se otorgó modo700 y se ejecutó. No se tocaron aplicaciones ajenas ni se abrió ventana.

## 4. Resultados reales

| Componente | Estado | Evidencia/límite |
|---|---|---|
| Identidad ISO y extracción | VALIDADA | SLES-52256, PAL; 504 archivos comparados por hash |
| ELF estructural | ANALIZADA | ELF32/MIPS, entry0x100008; stripped |
| Herramientas PS2Recomp | COMPILA | ps2_analyzer.exe y ps2_recomp.exe |
| Generación completa | IMPLEMENTACIÓN PARCIAL | 14632 cuerpos, 16522 errores; no compilación completa |
| Dos hojas recompiladas x64 | VALIDADA en alcance aislado | 12 casos por función, bytes y llamadas originales |
| Dos hojas recompiladas ARM64 | EJECUTA | mismos casos pasan en teléfono real, páginas4KB |
| Memoria/IOP/GS del juego | NO ANALIZADA en profundidad | no se ejecutó entry point |
| Primer TXD auténtico | ANALIZADA | LEVEL.IMG: RW0x36003, device6, una textura; no decodificada |
| librw/renderer MP2 | NO ANALIZADA su integración | revisión de fuentes; sin render de recursos |
| Núcleo MP2 Touch Engine | EJECUTA en host / COMPILA ARM64 | snapshot sincronizado y multitouch; sin UI/JNI |
| HUD, editor y gamepad | NO ANALIZADA su implementación | arquitectura documentada, sin vista táctil |
| Menú/gameplay/Bullet Time/Shootdodge | NO ANALIZADA su ejecución | ninguna escena ni mecánica ejecutada |

## 5. Pruebas

- Relectura ISO: SHA-256 de los 504 archivos comparado con extracción; hash ISO estable.
- Python unittest: 2/2 (identidad/segmento ELF y rechazo de fullscreen forzado).
- CTest: 2/2 (input y funciones recompiladas); ambos ejecutables muestran PASS.
- Input: move+look+fire+shootdodge simultáneos, repetición de DOWN, dos dedos en
  disparo, UP independiente, tap Bullet Time completo entre frames y cancelación.
  No se probaron vista Java, editor, cambios de aspecto ni reconexión de mando.
- Verificación bytes: dos hojas y llamadas JAL desde el ELF con hash exacto.
- ARM64: cabecera ELF64/AArch64 verificada y ejecución ADB con resultado PASS.
- Chunk TXD: límites y recuento de textura nativa comprobados en primer chunk.

## 6. Errores y correcciones

CMake no estaba en PATH: herramientas instaladas en venv local. Las descargas
FetchContent trajeron submódulos de documentación/pruebas innecesarios; se detuvieron
solo sus árboles propios y se sustituyeron por fuentes fijadas sin recursión.
Primera compilación del harness falló por macro sin paréntesis al pasar `&ctx` y
ruta Stubs/Unimplemented.h: se pasó un puntero nombrado y agregó include Kernel.
La compilación posterior y ejecución pasaron; no se alteró el código traducido.
El recorrido lineal del IMG falló por un chunk fuera de límites: se limita el análisis
al primer TXD y se conserva pendiente la estructura de contenedor restante.
No se ocultaron los errores de traducción ni se sustituyeron por stubs retornando éxito.

## 7–9. Estado del juego, Android y HUD

Juego real: solo dos funciones hoja sin interacción de hardware; no boot, primer
frame, menú, nivel, audio ni gameplay. Android: ejecución nativa de esas dos hojas,
sin APK ni launcher. HUD: núcleo semántico probado; ninguna acción validada contra
la jugabilidad. No hay FPS del juego que informar.

## 10. Próximo objetivo

Instalar Ghidra compatible con la extensión fijada, analizar la ruta desde entry,
exportar límites fiables TOML/CSV y regenerar un subconjunto de inicio. Corregir
GP/BSS y servicios requeridos por evidencia antes de llamar al entry point completo.
