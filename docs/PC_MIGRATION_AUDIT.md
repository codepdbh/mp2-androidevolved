# Auditoría de migración de PS2 a PC

Fecha: 2026-11-03. Rama: `migration/maxpayne2-pc-pcrecomp`, creada desde `main` en `61f6d01`.

## Preservación

- Nada del trabajo de PS2 se ha borrado ni movido. `main` queda en `61f6d01`.
- Los cinco archivos modificados sin confirmar y `patches/ps2recomp-quadword-alignment.patch` siguen en el árbol de trabajo, tal como estaban.
- Copia de esos cambios: `private/pre-pc-migration/` (ignorado por Git), con `wip-uncommitted.diff`, el parche y el commit base.
- `.gitignore` excluye ahora la instalación del juego, `work/`, `src/recomp/generated/`, `*.ras` y `*.bik`. Antes, la carpeta del juego aparecía como no rastreada y un `git add .` la habría incluido.

## Clasificación

| Componente | Ruta | Clase | Motivo |
|---|---|---|---|
| Pictogramas del HUD y arte del launcher | `android/app/src/main/res/drawable-nodpi/` | REUTILIZAR | Los once PNG están intactos (hashes abajo). |
| Icono de la app | `android/icono/`, `res/mipmap-*`, `res/drawable/mp2r_launcher_foreground.xml` | REUTILIZAR | Independiente de la plataforma del juego. |
| Motor táctil | `src/input/touch_engine.*`, `tests/touch_engine_test.cpp` | REUTILIZAR | Solo acciones semánticas; no sabe nada de PS2. Sus pruebas pasan. |
| Vista de controles y editor | `android/.../TouchDiagnosticView.java` | ADAPTAR | Multitouch y editor sirven. Se llama "diagnóstico" y falta conectarla al mapeador de PC. |
| Actividad de juego | `android/.../GameActivity.java` | ADAPTAR | El ciclo de vida sirve; el arranque es del ELF de PS2. |
| Almacenamiento | `android/.../GameStorage.java`, `docs/ANDROID_STORAGE.md` | ADAPTAR | Valida ISO/ELF; debe validar la instalación de PC. |
| Puente JNI | `android/app/native_bridge.cpp`, `NativeBridge.java` | ADAPTAR | `touchBatch` se conserva; `runGame` y `menuInput` son de PS2. |
| Salida de audio | `android/app/audio_sink.h` | PENDIENTE DE INVESTIGACIÓN | Comparar con `audio_mixer.h` de Carbon, ya probado en juego real. |
| Launcher completo | (no existe en MP2R) | REEMPLAZAR | Partir de `LauncherActivity.java` de los ports NFS. |
| Gradle y CMake de Android | `android/build.gradle`, `android/app/build.gradle`, `android/app/CMakeLists.txt` | ADAPTAR | arm64, NDK 28 y API 26 sirven; las fuentes son de PS2. |
| Política de escritorio | `scripts/development.py`, `config/development.toml`, `docs/WINDOWED_TEST_POLICY.md` | REUTILIZAR | Ya supervisa las compilaciones y el lift de esta iteración. |
| Pruebas de Android | `android/app/src/androidTest/.../TouchInstrumentation.java`, `scripts/verify_apk.py` | ADAPTAR | Comprueban táctil y APK, no el juego. |
| Runtime de PS2 | `src/runtime/` | ARCHIVAR | EE, IOP, GS, VU, SIF. No aplica al código x86. |
| Herramientas de PS2 | `scripts/` (ISO, ELF, IRX, Ghidra EE), `config/mp2-*.toml`, `patches/ps2recomp-*` | ARCHIVAR | Solo para la ruta R5900. |
| Pruebas de PS2 | `tests/ee_*`, `iop_*`, `gs_*`, `vu0_*`, etc. | ARCHIVAR | Validan contratos del kernel de PS2. |
| Código raíz | `CMakeLists.txt` | ARCHIVAR | Construye la ruta PS2; el host de PC tiene el suyo. |
| Dependencias externas de PS2 | `external/PS2Recomp`, `external/librw`, `external/ghidra-emotionengine-reloaded` | ARCHIVAR | Sin uso en PC. `librw` no se aplica a esta versión. |
| ISO PS2 | `Max Payne 2.iso` | ARCHIVAR | Se conserva; PCRecomp no la procesa. |

Archivar significa dejar de compilarlo, no moverlo: mover `src/runtime/` a `archive/ps2_reference/` queda pendiente hasta que el CMake de Android deje de depender de esa ruta. El historial de Git lo conserva en cualquier caso.

## Pictogramas: comprobación del 2026-11-03

Los once existen, miden 1254×1254 y son RGBA de 32 bits. Ninguno se ha modificado.

| Archivo | Bytes | SHA-256 |
|---|---:|---|
| `hud_aim.png` | 242572 | `5303E5B86B36A99912219ED9C58AFE74CAC6BB4BB29E1B4744C2FDDB602178EA` |
| `hud_bullet_time.png` | 549264 | `BDD845F3F1B67EAA89EF0A7F0A354B165F8A55467B78DE49BFA528910737DF82` |
| `hud_fire.png` | 491421 | `90DC9D7D2E4BBD54688AC21AD71DE364E981397C7AECC44002CC46403B8C0E7E` |
| `hud_interact.png` | 608505 | `5E3705C56DC83110C9D05F6979D6AE7273404AB6D404329702AF56083193E78E` |
| `hud_jump.png` | 470903 | `4F262E11A0EA1B7CE61F5DF1BCF05FC8D5482FFC509BD6BA72AE96BC7FD8AE41` |
| `hud_painkiller.png` | 499987 | `A5AA89BB8DD2AE0781AB4686DC02E4671F28C97F0D4EDBA59426F912DC34B08F` |
| `hud_pause.png` | 384637 | `488BDA100816AB8C85EB9236E3F1FF5B829B0A10788C151E757C54589311A162` |
| `hud_reload.png` | 522571 | `55C93444A4CD736531C9B6A677208270B398F2CA8553C653809BF0AB2E2ABB2A` |
| `hud_shootdodge.png` | 621191 | `97AAE1DD5B929D460B2378263B4BD4A2B5423189F8035576E6D541E62F55CDB4` |
| `hud_weapons.png` | 610782 | `62AF84648D962856A50DA994B93AA2E54A04A9CEB32DA70E7450D6ED94BE516B` |
| `mp2r_launcher_art.png` | 2026382 | `647EE046F765C3500AC7B84FEC9AE14A63B08A9BD1FDF7C8D57056E7535C3DD1` |

## Estado real del HUD

- Funciona hoy: motor multitouch con pruebas, vista Canvas con los diez pictogramas y el editor de `TouchDiagnosticView`.
- Faltan: perfiles, opacidad por botón, configuración para zurdos, sensibilidad e inversión en la interfaz, contextos de juego y selector de armas.
- Nuevo en esta iteración: `src/input/pc_action_mapper.*` traduce las acciones al juego de PC. Aún no está conectado a Android ni al `X_Input` recompilado.
