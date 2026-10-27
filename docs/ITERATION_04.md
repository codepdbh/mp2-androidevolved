# Iteración 4 — alarmas originales y reloj EE, 2026-10-27

La APK v0.3 ejecuta el bloque original de instalación de alarmas y la
inicialización del temporizador 2. Se detiene en el syscall CreateThread (0x20),
PC 0x0010D3E8. **Todavía no hay gameplay, menú, escena ni audio.**

## Código original recuperado

La rutina PAL 0x00116DA8 copia 1856 bytes de 0x0054D6A0 a 0x80076000 y
40 bytes de 0x0054DDE0 a 0x00082000. El callback original de copia 0x00116D60
se recuperó como función en Ghidra. Las copias las ejecuta el juego; el cargador
no precarga el destino. Se comparan ambos bloques completos en el primer
FlushCache. Las siguientes verificaciones comparan código/vector, permitiendo
que cambie el estado mutable situado al final del payload.

`prepare_alarm_kernel.py` crea un ELF privado de análisis con ese mismo payload,
ubicado en su dirección de ejecución. No modifica la ISO ni el ELF original.
SHA-256 del payload:
`91fea21c5fbac87ffaa363b2810520e72a916b9dcd79f35033036a548e19c914`.
El ELF derivado también se verifica antes de generar glue o ejecutar Ghidra.

Ghidra exporta diez funciones del bloque, incluida la continuación 0x800766C0
demostrada por la tabla original. El conjunto de inicialización contiene 43
funciones (33 del ELF principal y 10 del bloque), 159 PCs exactos de entrada o
reanudación y cero guards de función pendiente generados. Ambas recompilaciones
tienen cero stubs/skips, instrucciones no manejadas, warnings y errores.
Esto no valida las funciones del resto del juego.

La rutina original de búsqueda del bloque devuelve e instala:

| Entrada | Dirección original |
| --- | --- |
| 0xFC, 0xFE | 0x80076440 |
| 0xFD, 0xFF | 0x800762A0 |
| 0x12C, INTC12 | 0x80076488 |
| 0x08, continuación | 0x800766C0 |

Se mantienen los valores reales de esta edición; no se reemplazan por los de
otro SDK. 0x12C se conserva como registro separado de la interrupción de timer3;
no se escribe fuera de la tabla de 256 syscalls de MP2R.
La función del slot extendido está documentada también en
[PS2SDK srcfile.c](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/src/srcfile/src/srcfile.c).

## Servicios nativos

- Registros de timers 2/3: reset, count/target de 16 bits, MODE y flags W1C.
- Avance por reloj de bus de 147456000 Hz, con divisores 1/16/256, fase fraccional,
  target, overflow y puesta a cero por target. El ejecutor convierte tiempo de
  `steady_clock` en ciclos de bus; no es temporización exacta por instrucción EE.
- Registro/ordenación de manejadores INTC, máscara, status y acknowledge.
  AddIntcHandler y Enable/DisableIntc conservan estado y retornos de transición.
- ERET usa los campos COP0 generados y limpia LLbit. FlushCache admite los modos
  0/2 observados, comprueba las copias y aplica una barrera nativa de coherencia.

No se entrega todavía una interrupción al código invitado: al aparecer un bit
pendiente habilitado, el ejecutor se detiene explícitamente. Los relojes por
HBlank/gate también se detienen porque requieren el proveedor de vídeo. Tampoco
se ejecutaron callbacks de alarmas ni se validó su programación completa.

La inicialización original del timer2 registra causa INTC11, manejador 0x001174C0,
configura count/target/MODE y habilita la causa. A continuación crea un tercer
semáforo e intenta crear el primer hilo auxiliar. Falta implementar el planificador
EE y sus contratos de espera/despertar; no se devuelve éxito en CreateThread.

## Validación

- Siete CTest pasan, incluidos límites de semáforos, slot INTC12, divisores,
  fracción, W1C, target/overflow, zero-return, rechazo de reloj de vídeo y cola INTC.
- La ejecución original pasa con bases de tabla 0x80060000 y 0x80050000, además
  de la prueba de presupuesto que se detiene antes de inicializar semáforos.
- APK v0.3 compilada e instalada en el teléfono del proyecto. Su instrumentación
  verifica el ELF desde `/storage/emulated/0/mp2androidevolved`, el nuevo punto
  de parada, rechazo de ELF truncado y controles/editor multitáctiles.
- Traza x64 y traza JNI ARM64 idénticas: 174 pasos, 45 branches, 3 semáforos.
- ELF de la biblioteca AArch64: tres segmentos LOAD alineados a 16 KB.
  Sigue pendiente ejecutar en un dispositivo de páginas de 16 KB.

Reproducción: `scripts/build_initialization.ps1`, `scripts/build_android.py`,
`scripts/test_android_initialization.py --serial R5GL70TXWMP` y
`scripts/verify_apk.py`. Para regenerar el mapa del payload:
`scripts/run_ghidra.py --kernel --process --script MP2ExportAlarmKernel.java`.
Primer análisis: omitir `--process` después de `prepare_alarm_kernel.py`.

Evidencia privada: `private/logs/recompile-alarm.log`,
`recompile-init-selection.log`, `host-alarm-initialization.log`,
`android-native-initialization.log`, `android-instrumentation.log` y
`private/generated/initialization/selection.json`.

La carpeta raíz del teléfono, las diez figuras táctiles y el icono aportado por
el usuario se conservan. No hubo pruebas de escritorio con ventanas.
