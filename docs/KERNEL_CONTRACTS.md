# Contratos nativos observados — 2026-10-27

Este módulo es una adaptación limitada de plataforma para el ELF PAL fijado.
No implementa un emulador de PS2 ni un kernel general. Operaciones ausentes detienen
el ensayo. Los cuerpos recompilados se incluyen sin modificaciones de instrucciones.

## Memoria y dispatcher

El cargador copia los segmentos ELF originales a32MB de RAM invitada. El llamador
verifica SHA-256: Python en las pruebas host y Java antes de entregar la instantánea
al JNI. C++ comprueba estructura, rangos y el layout PAL. El ensayo envenena BSS
antes de ejecutar el borrado original. No usa una ROM/BIOS comercial.

Se registran182 PCs exactos de entrada/reanudación extraídos del C++ generado
para51 funciones revisadas. Cada llamada cede al executor plano; el siguiente paso
entra en el target. Los retornos se resuelven por el PC preparado por el código
original. Los callbacks de syscall tienen una pila de retorno de máximo8 niveles
que conserva RA y el PC posterior al syscall. Cada hilo guarda su contexto R5900
y su propia pila de callbacks. Existe planificación cooperativa parcial; faltan
operaciones de multitarea general y entrega de interrupciones.

## Semáforos EE, syscall0x40

El descriptor EE tiene campos de32bit con offsets: count0, max4, init8, waiters12,
atributos16 y option20. No se usa el orden de IOP. Se conserva estado y se asignan
IDs positivos distintos; max debe ser positivo e init estar entre0 y max. Hay256
slots y fallos explícitos de creación. Wait/Signal/Poll conservan contadores y
colas del planificador nativo. El borrado de semáforos sigue sin implementar.

Fuente ABI: [PS2SDK kernel.h, commit329e2bb4](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/kernel/include/kernel.h),
consultada como referencia, sin incorporar el SDK. Se contrastó también la
implementación de Sync.cpp/EeScheduler.cpp del PS2Recomp fijado.

En el ELF real:0x00116690 prepara los descriptores y llama al wrapper0x0010D5E0;
el código original guarda los dos IDs en0x0054CED0 y0x0054CED4.

## Instalación de syscalls y callbacks

La tabla de datos del ELF en0x0054CEC0 especifica dos parejas:
0x83→0x00116730 y0x5A→0x001166F8. Ghidra confirmó código de búsqueda y copia de
palabras respectivamente. La primera se ejecuta en este camino; la segunda queda
registrada pero no se invoca antes del límite actual.

SetSyscall0x74 registra el manejador y refleja su puntero en una tabla de RAM
reservada por **MP2R**. Por defecto está en físico0x00060000/alias0x80060000;
el ensayo alternativo usa0x00050000. Son decisiones del ABI nativo, no offsets
atribuidos al firmware original. El propio código PAL busca sus punteros, deduce
la base y la guarda en0x0054CEB8. Ambas ubicaciones producen el resultado esperado.

## Límite de hardware — historial de iteración 3

PC0x00116DC4, dentro de0x00116DA8, intenta leer0x10001810 y examinar el bit0x100.
La definición de [PCSX2 Hw.h, commitf0523f8a](https://github.com/PCSX2/pcsx2/blob/f0523f8acb1d3a765ee73b74cffec63093569199/pcsx2/Hw.h)
identifica esa dirección como RCNT3_MODE. El acceso sigue sin implementarse:
se conserva PC/dirección/motivo de parada y no se proporciona un valor inventado.
Para continuar deben definirse estado de contador, reloj y entrega de alarmas/IRQ
o un reemplazo nativo equivalente, con sus comprobaciones de comportamiento.

## Alarmas y temporizadores — iteración 4

La lectura de RCNT3_MODE usa ahora el estado real de reset del modelo de registro.
Se deja ejecutar la instalación original, con copia verificada del payload y
registro separado del slot extendido INTC12. Timer2/3 tienen reloj de bus nativo,
divisores, fracción, target/overflow y W1C; vídeo/gate y entrega de IRQ pendientes.
INTC implementa cola, máscara, status y acknowledge. Un bit pendiente habilitado
detiene el ejecutor de forma conservadora, incluso antes de habilitación global
COP0; no se afirma entrega de interrupciones.

Los contratos de ordenación de AddIntcHandler y retorno de Enable/DisableIntc
se contrastaron con la investigación original de
[ps2tek, EE syscalls](https://psi-rockin.github.io/ps2tek/#biossyscalls).
Registros/clock de referencia:
[PCSX2 Counters.cpp fijado](https://github.com/PCSX2/pcsx2/blob/f0523f8acb1d3a765ee73b74cffec63093569199/pcsx2/Counters.cpp).
No se incorporó código de esas fuentes.

ERET limpia LLbit después de restaurar PC/status mediante el código generado.
FlushCache0x64 admite modos0/2 y verifica copias antes de la barrera nativa.
La ejecución alcanza tres semáforos; CreateThread0x20 continúa sin implementar.
Evidencia, direcciones PAL y limitaciones: [ITERATION_04.md](ITERATION_04.md).

## Hilos y esperas — iteración 5

CreateThread20, StartThread22, ChangeThreadPriority29 y GetThreadId2F ejecutan la
ruta original del primer auxiliar. El principal comienza con prioridad0; cuando
el juego la cambia a1, el auxiliar de prioridad0 toma la CPU. Su WaitSema44 sobre
el semáforo3 lo bloquea y restaura el principal. Se comprobaron PC, GP, rango de
stack y stores de IDs originales. La raíz de retorno original0x00100220 está
recompilada y ExitThread23 cambia el estado a Dormant antes de replanificar.

Los estados nativos de salida/borrado/reinicio y señales se prueban aisladamente;
no todas esas rutas fueron ejercitadas por el ELF antes de la parada actual.
No hay time slicing, Sleep/Wakeup, suspensión ni borrado de semáforos.
La ausencia de un hilo listo se informa como `no_runnable_threads`.
GetOsdConfigParam4B permanece sin implementar. No se devuelve una configuración
inventada para saltar la comprobación original de parches. Más evidencia y
referencias ABI en [ITERATION_05.md](ITERATION_05.md).
