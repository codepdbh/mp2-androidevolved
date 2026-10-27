# Iteración 2 — 2026-10-26

## 1. Objetivo realizado

Recuperar funciones mediante Ghidra R5900, repetir la traducción sin datos falsos,
ejecutar el arranque original con servicios mínimos de plataforma, cargar texturas
auténticas y crear/probar una APK diagnóstica con JNI y controles de figuras propios.
Se aplicó la instrucción posterior de usar la raíz `mp2androidevolved` en el teléfono.

## 2. Archivos

Nuevos scripts: `setup_ghidra.py`, `run_ghidra.py`, `prepare_boot_probe.py`,
`build_android.py`, `deploy_game_data.py`, `verify_apk.py`; scripts Java de Ghidra
en `scripts/ghidra/`. Configuraciones: `mp2-pal-ghidra.toml`, `mp2-boot-prefix.toml`,
`ghidra-packages.lock.json`, `hud-imagegen-prompts.json`. Runtime propio inicial:
`src/runtime/bootstrap_runtime.h`. Pruebas: `boot_prefix_probe.cpp`,
`bootstrap_entry_probe.cpp`, `rw_texture_probe.cpp`. Se extendieron CMake,
inspect_elf.py, requisitos y exclusiones Git.

Android: `android/settings.gradle`, `build.gradle`, `gradle.properties`,
`app/build.gradle`, `app/CMakeLists.txt`, `native_bridge.cpp`, manifiesto,
`DiagnosticActivity.java`, `GameStorage.java`, `NativeBridge.java`,
`TouchDiagnosticView.java`, `TouchInstrumentation.java` y diez PNG en
`app/src/main/res/drawable-nodpi/`. Informes obligatorios actualizados y añadidos
`ANDROID_STORAGE.md`, `HUD_ASSETS.md`, este informe. Datos y binarios privados
no se añadieron a Git.

## 3. Herramientas y reproducción

Ghidra12.1.4, EE v2.1.38 (commit fcc5deac…), Java25.0.2 para Ghidra;
Gradle8.13, AGP8.10.1, JBR21.0.10, compile/target SDK35, minSDK26,
NDK28.2.13676358 y CMake3.22.1 para APK. Todas las builds limitadas a dos workers.
image_gen integrado para iconos; Pillow12.2.0 para inspección de alpha y lectura
del TGA de diagnóstico, sin sustituir la generación de imágenes por dibujos de código.
Los hashes de los paquetes Ghidra se verifican contra el lock antes de extraer.

```powershell
.\.venv\Scripts\python.exe scripts/setup_ghidra.py
# Primera importación; para proyecto existente usar --process.
.\.venv\Scripts\python.exe scripts/run_ghidra.py
.\.venv\Scripts\python.exe scripts/development.py --log private/logs/recompile-ghidra.log -- build/ps2recomp/ps2xRecomp/Release/ps2_recomp.exe config/mp2-pal-ghidra.toml
.\.venv\Scripts\python.exe scripts/prepare_boot_probe.py
.\.venv\Scripts\python.exe scripts/development.py --log private/logs/recompile-boot-prefix.log -- build/ps2recomp/ps2xRecomp/Release/ps2_recomp.exe config/mp2-boot-prefix.toml
.\.venv\Scripts\python.exe scripts/development.py --log private/logs/configure-iteration2.log -- .venv/Scripts/cmake.exe -S . -B build/mp2r -DMP2R_RECOMP_PROBE=ON -DMP2R_BOOT_PROBE=ON -DMP2R_BOOTSTRAP_ENTRY_PROBE=ON
.\.venv\Scripts\python.exe scripts/development.py --log private/logs/build-iteration2.log -- .venv/Scripts/cmake.exe --build build/mp2r --config Release --parallel 2
.\.venv\Scripts\ctest.exe --test-dir build/mp2r -C Release --output-on-failure
.\.venv\Scripts\python.exe scripts/build_android.py
.\.venv\Scripts\python.exe scripts/verify_apk.py
```

La APK se encuentra en `android/app/build/outputs/apk/debug/app-debug.apk`.
Es un artefacto privado de diagnóstico que incluye código derivado de la copia
aportada; no es una distribución pública ni contiene los archivos comerciales del disco.
El script Android usa la distribución Gradle8.13 instalada localmente y falla de forma
explícita si falta. Ghidra y JDK/SDK son rutas locales documentadas, no instaladores globales.

## 4. Resultados reales

| Componente | Estado | Evidencia |
|---|---|---|
| Ghidra EE | EJECUTA | Análisis103s; importación/exportación headless guardada |
| Mapa de funciones | ANALIZADA | 17145 funciones: 16165 contiguas, 980 fragmentadas |
| C++ de cuerpos contiguos | IMPLEMENTACIÓN PARCIAL | 0 instrucciones no manejadas; 6022 warnings; no compilación de todos los cuerpos |
| Prefijo de444 bytes | VALIDADA en alcance aislado | BSS1195776 bytes, GP0x5c8870, checkpoints1/4096 en host/ARM64 |
| Entrada completa | EJECUTA parcialmente | Syscalls0x3c/0x3d y parada explícita en llamada0x1168c8 |
| Recursos reales | EJECUTA carga/decodificación | librw NULL decodificó285 TXD; primer recurso64×128,4bit |
| APK diagnóstica | EJECUTA | Instalada; prueba nativa original vía JNI; sin gameplay |
| Carpeta del teléfono | VALIDADA | 504 archivos,3990041090 bytes,504 SHA-256 OK |
| Vista táctil/JNI/editor | EJECUTA | Pruebas MotionEvent reales y persistencia pasan |
| Pictogramas | IMPLEMENTADOS | 10 PNG RGBA generados; alpha0–255, incluidos en APK |

La librería nativa del APK es ELF64/AArch64 y sus tres PT_LOAD tienen alineación
16KB. zipalign comprobó el empaquetado con `-c -P 16 4`. Solo se ejecutó en un
dispositivo de páginas4KB; no se afirma validación de runtime en páginas16KB.
Referencia de comprobación: [Android, tamaños de página](https://developer.android.com/guide/practices/page-sizes).

## 5. Pruebas y evidencia privada

- Ghidra: `private/ghidra/analysis.log`, mapas CSV/TOML, rangos de cuerpos y listing
  de arranque. Solo el CSV de cuerpos contiguos alimenta la segunda traducción.
- Recompilación: `private/logs/recompile-ghidra.log`, `recompile-boot-prefix.log`.
- CPU original: `run-boot-host.log`, `run-boot-arm64.log`, `run-bootstrap-entry.log`,
  `run-bootstrap-arm64.log`; se detiene la rama desconocida y se rechazan syscalls ajenas.
- Texturas: `run-rw-probe.log`, `rw-scan-level01.log`; primer TGA/PNG inspeccionado.
  El primer recurso muestra una puerta reconocible. Sin comparación píxel a píxel PS2.
- APK: `build-android-apk.log`, `android-instrumentation.log`. Instrumentación:
  `passed=true`; ruta raíz, hash PAL y lectura Java/JNI, dos dedos, MOVE+FIRE,
  UP independiente, CANCEL, cámara sin salto, editor persistente y otro aspecto.
- Archivos: `deploy-phone-data.log`, `verify-phone-data.log`,504 líneas OK.
- Capturas de la interfaz diagnóstica, privadas; no se presentaron como gameplay.

## 6. Fallos y correcciones

Descarga completa Ghidra detenida: se reemplazó por rangos de8MB con dos conexiones
y hash global verificado. Primer -scriptPath con separador produjo argumento inválido:
se copiaron los scripts sin cambios a un directorio privado único. El exportador
upstream acepta sus askFile mediante argumentos headless; no fue necesario modificarlo.

El TGA indexado4bit no fue leído por Pillow: se convirtió a32bit mediante librw antes
de exportar. El contenedor IMG comienza con285 TXD contiguos y después tiene datos
de otra estructura; se detiene el escaneo sin afirmar interpretación del resto.
La primera clase adaptadora por alias chocó con el forward declaration generado:
se utilizó una clase derivada explícita. La generación de iconos tardó varios minutos;
se conservó cada original y los prompts completos.

## 7. Estado real del juego

Ejecución de bootstrap original con adaptación mínima, hasta la primera función no
enlazada. No hay renderer del juego, primer frame, menú, nivel, disparos, física,
audio, Bullet Time ni Shootdodge funcionales. No se informan FPS de gameplay.

## 8. Estado Android

APK instalada `com.mp2r.android`, ARM64, vista de diagnóstico y prueba original JNI.
Lectura desde la raíz pedida funcionando y verificada en dispositivo real.
Los datos del disco están fuera del APK; logs/preferencias pertenecen solo a MP2R.

## 9. Estado HUD

Pictogramas originales y controles semánticos conectados por JNI. Editor mueve y
guarda posiciones. No hay aún bindings PS2 PAL, perfiles completos, ajuste de tamaño/
opacidad, selector radial funcional ni gamepad/reconexión. Ninguna figura demuestra
que su mecánica correspondiente funcione en el juego.

## 10. Próximo objetivo

Registrar y ejecutar la inicialización original0x001168C8 y sus llamadas comenzando
por0x00116690; implementar solo los contratos de kernel que se observen. Tratar los
cuerpos fragmentados y los saltos indirectos con evidencia, manteniendo parada explícita
para entradas ausentes. Integrar recursos/gráficos después de resolver el flujo requerido.
