# Iteración 5 — primer hilo auxiliar original, 2026-10-27

La APK v0.4 supera CreateThread. El primer hilo auxiliar del ELF PAL ejecuta su
función original, espera el semáforo que creó el juego y devuelve la ejecución al
hilo principal. El nuevo límite es GetOsdConfigParam (syscall0x4B), PC0x0010D698.
**Todavía no hay menú, nivel, renderer, audio ni gameplay.**

## Implementación

`src/runtime/ee_threads.h` conserva IDs, descriptores, estados Dormant/Ready/
Running/Waiting, prioridades y orden de cola. El hilo principal usa ID1; la
primera creación válida devuelve ID2. Se validan entradas alineadas, prioridades,
rango de stack y tamaño sin overflow. Las creaciones no ejecutan el hilo.

El adaptador implementa creación, inicio, borrado de hilos dormidos, ExitThread,
ChangeThreadPriority y GetThreadId. La selección es cooperativa en los servicios
que requieren replanificación: prioridades menores se ejecutan primero y los
empates conservan el orden de cola. No hay time slicing ni scheduler general.

Cada hilo conserva el contexto R5900 completo y su pila de retornos de callbacks
de syscall. Los cambios abandonan el marco C++ generado antes de entrar en el
nuevo hilo. Se limpian las reservas LL/SC al cambiar de contexto. El stack nativo
invitado queda alineado a16 bytes con16 bytes reservados para el área del llamador;
es una decisión del ABI de MP2R, no una réplica de los marcos privados de una BIOS.
GP, argumento inicial y raíz de retorno proceden del descriptor/SetupThread.

WaitSema consume un recurso disponible o bloquea el hilo. SignalSema entrega el
recurso a un hilo en espera o incrementa el contador hasta su máximo. Se modelan
colas FIFO y selección por prioridad cuando el atributo lo solicita. PollSema no
bloquea; las variantes de interrupción no fuerzan replanificación. Las operaciones
EE exitosas de semáforo conservan el retorno de ID del modelo contrastado en
[Play! PS2OS.cpp](https://github.com/jpd002/Play-/blob/master/Source/ee/PS2OS.cpp)
y el PS2Recomp fijado; no se ha medido este ABI en hardware PS2 propio. No se copió
código de esos proveedores.

Referencias de estados y servicios:
[investigación original ps2tek](https://psi-rockin.github.io/ps2tek/) y
[PS2SDK kernel.h](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/kernel/include/kernel.h).
El borrado de semáforos, suspensión, Sleep/Wakeup, rotación de colas y entrega de
IRQ siguen pendientes. Una cola sin hilos ejecutables detiene el ensayo de forma
explícita. Los tests de señales son de estado nativo; la ruta original ejercitada
todavía no despierta al auxiliar mediante SignalSema.

`MP2RecoverThreadRoot.java` recupera la raíz original0x00100220, pasada por el
arranque a SetupThread. Comprueba los dos words originales: carga del syscall0x23
y SYSCALL. El cuerpo de8 bytes termina en ese servicio que no retorna. No se
alteran bytes del ejecutable ni se sustituye el cuerpo por código inventado.

## Evidencia de ejecución original

1. CreateThread crea ID2 con entry0x0010E2E0, stack0x005C1C80, tamaño0x400 y prioridad0.
2. StartThread recibe argumento0x005C2088. El hilo principal conserva la CPU hasta
   que ChangeThreadPriority reduce su prioridad a1.
3. El auxiliar ejecuta el cuerpo original0x0010E2E0 y llama WaitSema(3).
4. El auxiliar queda Waiting; el principal recupera PC0x0010D478 y continúa InitSys.
5. Se verifican los IDs que el juego guarda en0x0054C798/0x005C2080, el GP/stack/PC
   del auxiliar guardado y la recuperación del hilo principal.
6. GetOsdConfigParam se detiene explícitamente. No se inventa configuración ni se
   omite la rutina original que comprueba si requiere parches de compatibilidad.

El conjunto actual tiene51 funciones originales (41 principales y10 del bloque
copiado de alarmas) y182 PCs registrados. Las dos recompilaciones reportan cero
instrucciones no manejadas, warnings/errores, stubs y skips. El mapa Ghidra del
ELF principal tiene17149 funciones; esto no significa que todo el juego esté
recompilado y validado.

## Verificación y reproducción

- Ocho CTest pasan. El nuevo test cubre descriptores inválidos, dormancia, inicio
  duplicado, prioridad, bloqueo, handoff, FIFO/prioridad de esperas, salida,
  reutilización y ausencia de hilos listos.
- El ensayo original verifica dos bases distintas de la tabla de kernel y el
  presupuesto de parada anticipada. Se mantiene la validación de alarmas anterior.
- APK v0.4 instalada en el teléfono del proyecto; instrumentación JNI y controles
  pasa, incluidos rechazo de ELF truncado y editor/aspecto/multitouch.
- Trazas x64 y ARM64 idénticas:188 pasos,52 branches,3 semáforos,2 hilos,
  2 cambios de contexto y1 espera bloqueante. Dos unittest de Python pasan.
- Biblioteca AArch64 y tres segmentos LOAD alineados a16KB verificados. Falta
  ejecutar en dispositivo de páginas16KB.

Comandos: `scripts/build_initialization.ps1`, `scripts/build_android.py`,
`scripts/test_android_initialization.py --serial R5GL70TXWMP`, `scripts/verify_apk.py`.
Para regenerar el mapa del root sobre el proyecto existente:
`scripts/run_ghidra.py --process --script MP2RecoverThreadRoot.java`.
Evidencia privada: `private/logs/host-thread-initialization.log`,
`android-native-initialization.log`, `android-instrumentation.log`,
`recompile-init-selection.log` y `private/generated/initialization/selection.json`.

Se conservan los datos en la raíz `/storage/emulated/0/mp2androidevolved`, las
figuras táctiles y el icono aportado por el usuario. Todas las pruebas de escritorio
se ejecutaron sin ventanas.
