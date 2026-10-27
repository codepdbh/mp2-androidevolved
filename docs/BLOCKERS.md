# Bloqueos y siguientes comprobaciones

## Estado actual: iteración 5, 2026-10-27

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
