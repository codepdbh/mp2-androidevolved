# Auditoría inicial de reVC Android Evolved Enhanced Mod

Fuente: https://github.com/codepdbh/reVC-android-evolved-ENHANCED-MOD
Commit: `f5676c0adeb5fdb28e26ae1af3e0bda759679690`.
Comparación librw: https://github.com/aap/librw,
commit `18532c2e13efbc1aa43f0b00b4459831f9414de0`.
Estado: ANALIZADA arquitectura inicial, auditoría completa y pruebas GPU pendientes.
No se copió código reVC en MP2R.

Actualización de decisión Android: por instrucción explícita posterior del usuario,
MP2R usa la raíz compartida `/storage/emulated/0/mp2androidevolved`, con su propio
flujo de acceso a todos los archivos en Android11+ y comprobaciones Java/JNI.
Esto sustituye la propuesta inicial SAF/app storage de la matriz para datos del juego.
Configuración del HUD y logs de la app permanecen en almacenamiento propio de MP2R.

| Componente | Ubicación | Utilidad MP2R | Dependencias | Riesgos | Reutilización recomendada |
|---|---|---|---|---|---|
| Gradle/NDK | android/launcher/app/build.gradle | Configuración ABI/JNI | SDK35, NDK27.2.12479018, CMake3.22.1 | ARMv7 innecesario; prebuilts antiguos | Patrón; MP2R inicial solo ARM64 |
| Activity | GameActivity.java | Arranque SDL y directorio independiente | SDLActivity, AndroidX | MANAGE_EXTERNAL_STORAGE y raíz compartida | Activity propia com.mp2r.android, importar con SAF/app storage |
| CMake | app/CMakeLists.txt, cmake/android/AndroidConfig.cmake | Integración nativa | SDL2, OpenAL, mpg123 | Binarios prebuilts y alineación | Recompilar dependencias necesarias con NDK moderno |
| JNI | src/skel/android/AndroidMain.cpp, JavaWrapper.cpp | Puente de plataforma | JavaVM/JNIEnv | Adjuntar hilos y ciclo de vida | Revisar contratos, implementación propia |
| Multitouch/editor | TouchControlsView.java | IDs por dedo, insets, SharedPreferences | Canvas y JNI | ACTION_CANCEL llama handleUp del índice; revisar liberación global | Patrones; editor propio por acciones |
| Entrada nativa | TouchControls.cpp/.h | Snapshot por frame | CPad, Frontend, PlayerPed | Estado global UI/juego sin mutex; volatile no sincroniza | Snapshot sincronizado propio |
| Dispositivos | src/skel/sdl2/sdl2.cpp | Touch/pad/teclado/ratón | SDL controller DB | Mapeos específicos de GTA | Separar detección de mapeo PAL MP2 |
| Escalado | vendor/librw/src/gl/gl3device.cpp, rwengine.h | Render target menor | FBO y backend GL | Estado GL/context loss/VRAM | Evaluar tras primer recurso auténtico |
| DXT | vendor/librw/src/gl/gl3raster.cpp | Tamaño por bloques y fallback RGBA | Extensión S3TC o CPU | Mayor VRAM; MP2 PS2 puede usar otro formato | Portar solo si se demuestra necesidad |
| Memoria raster | gl3raster.cpp getLevelSize | Redondeo 4×4 evita buffer insuficiente | Mipmaps/compresión | No demuestra formato de nuestras texturas | Pruebas específicas antes de integrar |
| PS2 pipelines | vendor/librw/src/ps2 | Referencia limitada | PDS/VU y plugins | Difiere mucho del upstream actual | No reemplazo binario de RenderWare |

El diff directo de src registra 44 archivos diferentes, 527 inserciones y 4383
eliminaciones. Compara dos snapshots, no demuestra autoría ni que cada diferencia sea
un parche Android: upstream tiene evolución posterior. No adoptar el árbol completo.
Existen rutas GLES y comprobación S3TC; no hay prueba aquí sobre Adreno o Mali.
Los ajustes legacy packaging del proyecto no prueban compatibilidad con páginas 16KB.
En MP2R deberán verificarse alineación ELF y empaquetado de todas las .so.

El estado TouchPad reproduce botones GTA y llama directamente a saltar cinemáticas;
incluye contextos vehículo/radio y accesos de CPlayerPed. No es un contrato MP2.
MP2 necesita Bullet Time/Shootdodge separados, analgésicos, recarga y armas. Sus
bindings PS2 y estados reales todavía no se conocen. No reutilizar direcciones ni
acciones GTA. Menús táctiles requerirán localizar coordenadas y callbacks originales.

Licencias observadas: librw MIT; PS2Recomp GPLv3; la raíz reVC declara MIT-PoU con
condiciones adicionales de registro/atribución. No se trata como MIT estándar.
El archivo externo pide una contribución pública; no se realizó, porque el usuario
no autorizó publicar en repositorios externos. Registrar revisión local y mantener
pendiente aclaración de condiciones antes de copiar/distribuir código de ese árbol.
No asumir que estas licencias liberan datos comerciales o código derivado del ELF.
