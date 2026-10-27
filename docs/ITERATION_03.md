# Iteración 3 — 2026-10-27

## 1. Objetivo realizado

Continuar la ejecución original después de0x001168C8. Se implementó despacho
cooperativo, semáforos EE, tabla virtual de syscalls y retorno desde callbacks.
Se recuperaron funciones address-taken, se cargó el ELF real y se reprodujo el hito
en Windows, ejecutable Android ARM64 y APK/JNI. No hay gameplay todavía.

## 2. Archivos creados/modificados

- Runtime: `src/runtime/ee_semaphores.h`, `kernel_syscall_table.h`,
  `initialization_probe.h`, `initialization_probe.cpp`.
- Selección: `config/initialization-functions.toml`, `config/mp2-initialization.toml`.
- Ghidra: `scripts/ghidra/MP2InspectCallbacks.java`, `MP2RecoverCallbacks.java`,
  extensión de `scripts/run_ghidra.py` para ejecutar una evidencia concreta.
- Build: `scripts/prepare_initialization_probe.py`, `build_initialization.ps1`,
  `build_android.py`, `test_android_initialization.py`, raíz `CMakeLists.txt`.
- Pruebas: `tests/initialization_probe_main.cpp`, `ee_semaphores_test.cpp`,
  `verify_initialization.py`.
- Android: `app/CMakeLists.txt`, `app/build.gradle`, `native_bridge.cpp`,
  `NativeBridge.java`, `GameStorage.java`, `DiagnosticActivity.java`,
  `TouchInstrumentation.java`.
- Informes: README, PROGRESS, BLOCKERS, RECOMPILATION_REPORT, ANDROID_STORAGE,
  este informe y `KERNEL_CONTRACTS.md`. Los PNG del HUD e icono del usuario se conservan.

## 3. Herramientas/versiones/comandos

Se reutilizan PS2Recomp e5636981…, Ghidra12.1.4/EEv2.1.38, MSVC19.51,
NDK28.2.13676358, CMake, Gradle8.13 y AGP8.10.1 fijados previamente.
Builds limitadas a dos workers; ejecutor supervisado a2048MB/300s. Ninguna ventana
gráfica de escritorio; interfaz probada solo en el teléfono de trabajo especificado.

```powershell
.\.venv\Scripts\python.exe scripts/run_ghidra.py --process --script MP2RecoverCallbacks.java
.\scripts\build_initialization.ps1
.\.venv\Scripts\python.exe scripts/build_android.py
.\.venv\Scripts\python.exe scripts/verify_apk.py
.\.venv\Scripts\python.exe scripts/test_android_initialization.py --serial SERIAL
```

Para CLI ARM64: configurar la raíz con el toolchain NDK y
`-DMP2R_INITIALIZATION_PROBE=ON`, compilar `initialization_probe`; pasarle la ruta
del ELF PAL ya verificado. En el teléfono se usó
`/sdcard/mp2androidevolved/SLES_522.56`. No se transfirieron nuevamente los4GB.
Había dos dispositivos conectados: solo se utilizó el serial del teléfono de trabajo
establecido; no se instalaron apps ni cambiaron permisos en el segundo.

## 4. Resultados

| Componente | Estado real | Evidencia |
|---|---|---|
| Dos callbacks recuperados | ANALIZADA | Tabla PAL contiene sus direcciones; Ghidra define cuerpos contiguos |
| Subconjunto de10 funciones | COMPILA | Traducción sin errores/warnings y build x64/ARM64 |
| Dispatcher plano | EJECUTA | Llamadas anidadas, retornos y49 entradas/resumes registrados |
| CreateSema EE | VALIDADA en ensayo | Dos objetos distintos; stores originales de IDs comprobados |
| SetSyscall/callbacks | EJECUTA | Instalación y retorno preservando RA/PC |
| Búsqueda de tabla original | VALIDADA en ensayo | Descubre bases distintas0x80060000/0x80050000 |
| Presupuestos de ejecución | VALIDADA | Presupuesto32 detiene el bucle BSS antes de continuar |
| CLI x64/ARM64 | EJECUTA | Trazas de ejecución idénticas |
| APK v0.2 | EJECUTA | ELF real de la raíz, JNI y prueba instrumental PASS |
| Temporizadores/alarmas | BLOQUEADA su ejecución | Lectura RCNT3_MODE sin servicio implementado |
| Gameplay | NO ANALIZADA su ejecución | Sin menú, escena, armas, sonido ni físicas ejecutadas |

La función de copia de palabras fue traducida y registrada como callback, pero el
camino actual no la invoca; no se presenta como copia ejecutada. Sí ejecuta el
callback de búsqueda dos veces por escenario. La tabla virtual no contiene firmware.
No se afirma equivalencia de kernel/hardware PS2 completo ni validación diferencial.

## 5. Pruebas

- CTest6/6 y unittest2/2 pasan. Contratos de semáforos: argumentos inválidos,
  IDs distintos, estado inicial/metadatos y agotamiento de256 slots. Tabla virtual:
  instalación, retirada, reflejo en RAM y rechazo de índices/rangos inválidos.
- Ensayo original con dos bases de tabla. Se verifican stores de semáforos y el
  valor descubierto que el propio código escribe en0x0054CEB8.
- Guardas de ejecución: presupuesto de checkpoints32, truncamiento ELF rechazado.
- La traza x64 es idéntica a la del CLI ARM64: comparadas línea por línea, incluyendo
  ambos escenarios y el ensayo de presupuesto.
- APK: instrumentación `passed=true`, lee/hash-verifica ELF de BOOT2, ejecuta InitSys
  hasta el límite esperado y mantiene las pruebas de multitouch/editor/aspecto.
- La .so conserva alineación16KB y zipalign pasa. Dispositivo real con páginas4KB;
  no se probó ejecución en dispositivo16KB.

Logs privados: `recompile-init-selection.log`, `run-initialization-host.log`,
`run-initialization-arm64.log`, `android-instrumentation.log`,
`android-native-initialization.log`, `build-init.log`, `build-init-arm64.log` y
`build-android-apk.log`. Metadatos/hash de fuentes generadas:
`private/generated/initialization/selection.json`. No se publicaron fuentes comerciales.

## 6. Fallos y correcciones

El mapa anterior no incluía como funciones los dos callbacks aunque ya tenían
instrucciones desensambladas: se recuperaron a partir de la tabla de punteros real.
No se intentó resolverlo inventando límites ni copiando offsets de otra edición.
Se compila un CSV seleccionado para evitar depender de cuerpos ajenos al ensayo.
La dirección de tabla es un recurso del ABI nativo, no una ubicación de BIOS supuesta.

Se encontró una dependencia real de hardware: PC0x00116DC4 hace LW de0x10001810.
La ejecución se detiene antes de consumir un valor. No se estableció artificialmente
el bit de contador activo para saltar la inicialización original.

## 7. Estado del juego

InitSys original ejecuta más allá del punto anterior, hasta inicialización de alarmas.
No se ha producido un frame del juego ni se ha llegado al menú o a un nivel.

## 8. Estado Android

APK privada `0.2-initialization-diagnostic`, versionCode2, instalada en el teléfono.
LEE los datos del usuario de la raíz `mp2androidevolved`; solo el código nativo
derivado y recursos propios de interfaz están dentro del APK. El icono aportado
por el usuario se conserva y no se rediseñó. La app guarda la traza en su diagnostic.log.

## 9. Estado HUD

Sin cambios funcionales de mecánicas: figuras/entrada semántica/editor siguen
operativos y pasan regresión. Ningún botón está aún conectado a bindings PAL de juego.

## 10. Próximo objetivo

Implementar un contrato nativo comprobable para temporización/alarmas desde
0x00116DA8. Resolver los callbacks y la ruta de servicios que realmente utiliza,
sin forzar condiciones para omitirla. El siguiente acceso conocido es
RCNT3_MODE0x10001810; inspeccionar escrituras, IRQ y tiempos antes de sustituirlo.
