# Iteración 6 — InitSys completo y entrada a main(), 2026-10-27

La APK v0.5 ejecuta InitSys original completo y el crt0 llega a `main()` del juego
(0x0042F7E0) con `argc=1`, `argv[0]="cdrom0:\SLES_522.56;1"` y EIE habilitado.
El nuevo límite es la primera instrucción de `main`, aún no seleccionada.
**Todavía no hay menú, nivel, renderer, audio ni gameplay.**

## Configuración de sistema (OSD)

`src/runtime/ee_osd_config.h` atiende SetOsdConfigParam 0x4A, GetOsdConfigParam
0x4B y las variantes extendidas 0x6E/0x6F. El valor es una preferencia de sesión
de MP2R (`raw=0xE2032014`), no una lectura de NVRAM/BIOS comercial. Se validan
alineación y rango del puntero invitado. La rutina original lo lee y reescribe
dos veces; la comprobación de parches de compatibilidad no se omite.

## Servicios TLB copiados por el juego

El juego copia 0x330 bytes de 0x0054C868 a 0x80075000: cinco servicios del EE
(Put/Set/Get/ProbeTLBEntry y ExpandScratchPad), una función de búsqueda y su
tabla. `scripts/prepare_alarm_kernel.py --kind tlb` crea el ELF privado de
análisis, `MP2ExportTlbKernel.java` recupera las funciones y `build_initialization.ps1`
las recompila. Seis funciones, cero instrucciones no manejadas/warnings/stubs.
El ejecutor compara byte a byte la copia en RAM con su origen antes de ejecutar
ese rango y al final del ensayo, y comprueba los handlers 0x55–0x59 y 0x5B.

`src/runtime/ee_tlb.h` modela la TLB conjunta del R5900: 48 pares par/impar con
PageMask, EntryHi (VPN2+ASID), EntryLo0/1 y G como AND de G0/G1. TLBP compara VPN2
bajo la máscara de página y luego G o ASID; si falla, activa Index.P (bit31).
Las entradas nunca escritas no coinciden con ningún probe. TLBWI/TLBWR/TLBR con
índice ≥48 detienen el ensayo. Random avanza una vez por TLBWR (el hardware lo
decrementa cada ciclo) para que las trazas sean deterministas; el código PAL
localiza el slot con TLBP. Los registros TLB de COP0 son estado de la CPU y se
preservan en los cambios de hilo. No se reproducen mapeos que una BIOS dejaría.
Referencia: *EE Core User's Manual*, capítulo MMU.

### Corrección de PS2Recomp

El traductor fijado (e563698, también HEAD upstream) enmascaraba MTC0 EntryHi con
0xC00000FF, lo que borraba VPN2; `ExpandScratchPad` habría escrito EntryHi 0 en
lugar de 0x70004000. EntryLo0 perdía el bit S. El parche
`patches/ps2recomp-r5900-tlb-cop0-masks.patch` usa 0xFFFFE0FF, 0x83FFFFFF y
0x03FFFFFF. `bootstrap_dependencies.py --restore` lo aplica; la verificación
acepta el árbol solo limpio o con exactamente esos parches reversibles.

## Argumentos de arranque

SetupThread recibe `_args=0x5C1300` (dentro de la BSS que crt0 ya limpió). Como
el loader del kernel, MP2R escribe `{argc, argv[16], payload[256]}` con `argc=1`
y `argv[0]` igual al valor BOOT2 de `SYSTEM.CNF`. Java (`GameStorage.bootPath`) y el
test de host (`tests/verify_initialization.py`) lo leen del disco; no está fijado
en C++. Formato de la estructura: [PS2SDK startup.h](https://github.com/ps2dev/ps2sdk/blob/master/ee/startup/include/startup.h).

## Evidencia de ejecución original

1. GetOsdConfigParam/SetOsdConfigParam ×2 y regreso a InitSys.
2. 0x00115EC8 instala el copiador 0x5A, copia el bloque TLB, FlushCache ×2 e
   instala 0x5B, 0x54 y 0x55–0x59.
3. InitSys retorna a crt0; FlushCache(0); `ei`; `main(argc, argv)` en 0x00100208.
4. Se verifican argc, argv, la cadena BOOT2 en RAM invitada y Status.EIE.

Selección: 63 funciones originales (47 principales, 10 de alarmas y 6 TLB) y
201 PCs de entrada/reanudación. Ninguna función TLB se ejecuta todavía.

## Verificación

- Nueve CTest pasan (cinco de inicialización), incluido el nuevo `ee_tlb` (escrituras
  indexadas/aleatorias, lectura, máscaras, ASID/G/PageMask, rechazo de índice).
- Trazas x64 y ARM64 idénticas: 106 líneas, 250 pasos, 75 branches, 3 semáforos,
  2 hilos, 2 cambios de contexto, 1 espera, ambas bases de tabla de kernel.
- APK v0.5 instalada en el teléfono del proyecto; instrumentación JNI, BOOT2,
  ELF truncado y controles pasan. `verify_apk.py`: segmentos alineados a16KB;
  sigue sin ejecutarse en un dispositivo de páginas16KB.

## Siguiente trabajo

`main` mide 2596 bytes y llama a la mayor parte del motor. Las interrupciones ya
están habilitadas, así que la entrega de IRQ (INTC/timers/VBlank) será necesaria
pronto. Después: SIF/IOP RPC (CD, memoria, pad, audio) y GS.
