# Bloqueos y siguientes comprobaciones

## Ruta de PC (2026-11-03, iteración 2)

### Resuelto en esta iteración

- **Excepciones C++.** `desktop/pc_host/mp2_seh.c` y `RECOMP_SEH_FRAMES` funcionan con una excepción real: captura, dos relanzados y unwind. Solo esa ruta está probada: faltan destructores en cadena y `__try` del juego.
- **Lifter.** Condiciones tras `or`/`xor`, `pushaw`/`popaw`.
- **Primera ventana, Direct3D y entrada.** Ya funcionan en Windows.

### Incidente: escritura en el registro real

Del 2026-11-03 12:13 a las 12:47, las ejecuciones del host crearon `HKCU\Software\Remedy Entertainment\Max Payne 2`. MFC obtiene `RegCreateKeyExA` y compañía por `GetProcAddress`, y eso esquivaba el registro virtual.

- **Corregido:** `GetProcAddress` devuelve ahora los shims (`native32_shim_va`). En `boot_27` no hay ninguna llamada nativa de registro y las fechas de la clave no cambian.
- **Pendiente del usuario:** la clave sigue ahí, con los ajustes del juego. El borrado desde la sesión fue denegado por los permisos. Para quitarla: `reg delete "HKCU\Software\Remedy Entertainment" /f`. Hay copia en `private/registry-backup/`.
- **No se tocó:** `HKLM`, que es del instalador; la asociación `.mp2s`; Mis documentos.

### Pendientes, por orden de llegada

1. **Android es lo grande.** Todas las llamadas nativas (236 funciones de KERNEL32, 209 de USER32, 115 de GDI32) necesitan implementación propia, partiendo de `guest_*.inc` de Carbon. Gran parte de USER32 y GDI32 solo la pide `mfc71.dll`: hay que medir cuáles se usan. El registro virtual y la política del host son C específico de Windows y hay que llevarlos al runtime de Android.
2. **Direct3D 8 → Vulkan.** El DXVK del árbol de Android (`1.9.2b`) no tiene D3D8.
3. **Sonido sin verificar.** `DSOUND` y `eax.dll` nativos (x86) en Windows; el audio está silenciado por `waveOutSetVolume`. No se ha oído nada. En Android no hay implementación.
4. **Excepciones de hardware.** Un acceso inválido dentro de un `__try` del juego acaba en informe de crash: el despacho solo cubre `RaiseException`.
5. **Guardado.** Toda escritura va a `work/pc/sandbox/` con el nombre base, sin subcarpetas ni relectura. No se ha probado guardar: fallará o se comportará mal.
6. **Bullet Time, Shootdodge y disparo sin confirmar.** Bullet Time y Shootdodge reaccionan, pero falta verlos fotograma a fotograma; para disparar hace falta un tramo con arma.
7. **`AIM` no existe en PC** como control del jugador. Decisión de diseño pendiente: hoy va a `SNIPERZOOM`.
8. **Comparar con el original.** Nada de esto se ha contrastado con el EXE original en la misma máquina. Tus ports tienen un modo oráculo (`NFSU2_NATIVE=1`) que sirve de modelo.

### Divergencias de valores en el difftest

12 casos en 4 funciones de `msvcr71`. Sin resolver; no se ha observado que afecten a lo ejecutado.

| Función | Qué es | Divergencia |
|---|---|---|
| `0x00604CD8`, `0x00604F90` | Parte de `_adj_fdiv*`, `_adj_fprem` (solución al fallo FDIV del Pentium) | `st0` NaN en CPU, 0,0 recompilado |
| `0x00606039` | Fragmento matemático x87 cerca de `modf` | `st0` inf en CPU, 1,0 recompilado |
| `0x00635C6B` | Lee y limpia el estado x87 (`fnstsw`, `fnclex`) | Bit C3 (`0x4000`) activado en recompilado |

Las otras 42 son `unicorn=True lifted=False`: parece un efecto del arnés, como documentó NFSU2, sin demostrarlo caso por caso.

El x87 usa `double` de 64 bits en lugar de los 80 bits reales. Puede afectar a la física en casos límite.

### Otros

- `scripts/pc_recomp/coverage_gaps.py` copiado, sin ejecutar: la cobertura de `.text` no está medida.
- Upstream de PCRecomp lleva 164 commits sin evaluar.
- `MaxPayne2.WidescreenFix.asi` sin evaluar: hoy se juega a 640×480, escalado a la ventana.
- Un fallo en un módulo sin la función catalogada es ruidoso, pero solo aparece al ejecutarla: los niveles siguientes pueden revelar más.

## Ruta de PS2 (archivada)

Actualización S25: programa completo instalado; se resolvieron búsqueda de
módulos LOADFILE y drenaje cuadrático del vídeo. Persisten menús invertidos,
HUD contextual y falta renderer GPU. AAudio de streaming y blend de campos
están en validación; SPU2 completo sigue pendiente.
[Sesión y evidencia](SESSION_S25_2026-10-28.md).

## Estado actual: revisión de 2026-10-28

Se corrigió la saturación magenta del vídeo (sesgo extra tras IDCT intra).
Captura real «PRESENTS» verificada; aún presenta campos duplicados.
GIF reinicia Q por tag y respeta PRE solo en PACKED no vacío.

El avance del usuario ya ejecuta programa completo, IOP, vídeo y rasterización
GS. Se resolvió el callback de sceSifSetDmaIntr que bloqueaba DS2U; el mando
vuelve a actualizarse.21 tests pasan. Persisten imágenes negras/invertidas y
falta integración de presentación/entrada/audio en el frontend Android del
programa completo. Sin gameplay validado.
[Revisión y evidencia](REVIEW_2026-10-28.md).

## Estado actual: iteración 6, 2026-10-27

InitSys completo. La ejecución se detiene en main() 0x0042F7E0, llamada desde
crt0 0x00100208, porque esa función aún no está seleccionada ni revisada. Las
interrupciones están habilitadas (EIE): cualquier IRQ pendiente detiene el ensayo
porque la entrega sigue sin implementar. Pendientes: IRQ, Sleep/Wakeup, SIF/IOP,
GS/VU, audio. Parche local de PS2Recomp pendiente de proponer upstream.
[Detalle de iteración6](ITERATION_06.md).

## Historial: iteración 5, 2026-10-27

CreateThread e inicio/espera del auxiliar superados. El principal reanuda su PC
original tras dos cambios de contexto. Falta GetOsdConfigParam0x4B, PC0x0010D698,
para que continúe la comprobación original de compatibilidad de configuración.
Planificador cooperativo parcial; entrega de IRQ, Sleep/Wakeup/suspensión,
servicios IOP y subsistemas de juego pendientes. Ocho CTest y pruebas de APK v0.4
pasan. No hay gameplay. [Detalle de iteración5](ITERATION_05.md).

## Historial: iteración 4, 2026-10-27

Instalación original de alarmas y configuración del timer2 superadas. El nuevo
límite es CreateThread (0x20), PC0x0010D3E8: falta planificador EE con contextos,
prioridades y espera/despertar. Los contadores tienen reloj de bus nativo, pero
la entrega de IRQ y las fuentes de vídeo siguen pendientes y paran explícitamente.
No hay gameplay, escena, renderer ni audio. Siete CTest e instrumentación Android
pasan. Véase [ITERATION_04.md](ITERATION_04.md).

## Historial: iteración 3, 2026-10-27

El dispatcher y la primera parte de InitSys ya ejecutan: semáforos, instalación de
callbacks y búsqueda original de la tabla de syscalls. La ejecución se detiene en
PC0x00116DC4 al leer RCNT3_MODE (0x10001810). Falta implementar el contrato de
temporización/alarmas, no devolver cero ni marcar un temporizador activo para omitir
su inicialización. El resto de InitSys y sus servicios siguen pendientes.

Los dos callbacks recuperados aumentan el mapa a17147 funciones (16167 contiguas,
980 fragmentadas); no se retradujo ni validó todo el juego. Solo el subconjunto de
10 funciones se recompiló y ejecutó en este hito, sin warnings/errores de traducción.
La tabla de kernel virtual de MP2R no es una BIOS. Sus ubicaciones0x80060000 y
0x80050000 fueron elegidas para pruebas del ABI nativo, no deducidas del disco.

Nuevo cargador real Java/JNI del ELF PAL y APK v0.2 probados en el dispositivo de
trabajo. Sin renderer, audio, escena, bindings PAL ni gameplay; páginas16KB todavía
sin prueba de ejecución. Referencia detallada: [ITERATION_03.md](ITERATION_03.md).

## Actualización iteración 2

El bloqueo inicial de instalación/exportación Ghidra está resuelto. La traducción
de 16165 cuerpos contiguos reporta cero instrucciones no manejadas; quedan 980
cuerpos fragmentados y 6022 warnings de flujo que requieren tratamiento explícito.
Se ejecutó la entrada original con adaptación mínima de dos servicios de arranque,
hasta la llamada pendiente 0x001168C8. La próxima implementación es el dispatcher
y las funciones originales de inicialización; no saltarlas con retornos ficticios.

librw ya decodifica 285 TXD de LEVEL.IMG. No se ha interpretado todo el contenedor
ni implementado geometría/pipelines/renderer. La APK de diagnóstico existe y fue
probada en un teléfono de páginas4KB; ELF/ZIP se verificaron a16KB, pero falta
ejecución en dispositivo de16KB. Los controles visuales/JNI funcionan como entrada
semántica; siguen sin bindings PAL ni mecánicas del juego. Gamepads, sonido,
guardados y escenarios pendientes. Datos: raíz `mp2androidevolved`, 504 hashes OK.

## Historial de bloqueos de la primera iteración

1. **Mapa de funciones (IMPLEMENTACIÓN PARCIAL).** ELF stripped con código y datos
   en el mismo segmento; el fallback incluye texto como instrucciones. Evidencia:
   16522 errores y la cola de `private/logs/recompile-pal.log`. Solución mínima:
   Ghidra EE, referencias cruzadas desde entry/JAL, límites revisados y exclusión de
   datos. Regenerar todo el C++ después; no usarlo como mapa definitivo.
2. **Runtime de arranque (NO ANALIZADA en profundidad).** Faltan contratos de GP,
   BSS, kernel, IOP/RPC, VU/GS y dispatcher para este ELF. Las dos hojas probadas
   no ejercitan estos subsistemas. No sustituirlos por retornos ficticios.
3. **RenderWare (ANALIZADA parcialmente).** Primer chunk de LEVEL.IMG es TXD,
   versión 0x36003, device ID6, una textura nativa. El contenedor completo no es un
   stream lineal sin más: un intento de recorrerlo así excede límites de chunk.
   RAS usa envoltorio propio. Falta decodificar contenedores/plugins PS2 y comprobar
   pipelines PDS/ADC/Skin/HAnim/PTank/LtMap. librw no está integrada ni renderiza MP2.
4. **Android (IMPLEMENTACIÓN PARCIAL).** Biblioteca input y dos hojas recompiladas
   compilan ARM64; las hojas ejecutan en un dispositivo real. Faltan Activity/JNI,
   importación de datos, renderer/audio/lifecycle y APK. Páginas 16KB sin validar.
5. **Controles originales (NO ANALIZADA).** No se conocen bindings PAL ni estados
   gameplay. El núcleo semántico no permite afirmar Bullet Time/Shootdodge funcionales.
6. **Licencias (ANALIZADA inicialmente).** No copiar código reVC sin aclarar sus
   condiciones adicionales; no se publicó ninguna contribución externa. El acceso
   web a halpz/re3 devolvió HTTP451; no se intentó eludirlo. PCSX2 se consultó como
   referencia pública, no se ejecutó ni se incorporó. Revisión de dependencias
   transitivas y distribución de código derivado todavía pendiente.

El siguiente hito es un mapa verificable de la ruta de inicio y su recompilación
con menos falsos positivos; no es una APK decorativa.
