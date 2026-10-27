# Módulos IOP del disco y estrategia MP2R

El EE ejecuta sin cambios las librerías SIF del juego (sifcmd/sifrpc de Sony).
MP2R responde como un IOP arrancado: `src/runtime/iop_sif.h` implementa el extremo
IOP del protocolo (registros, SifSetDma, comandos de sistema, RPC y cola SIF0 por
el canal DMAC 5). Los módulos del disco se ejecutan **originales** en el intérprete
R3000A (`iop_cpu.h`) sobre un kernel IOP nativo (`iop_kernel.h`). Ninguna BIOS.

| Módulo | Función | Comunicación con el EE | Estado MP2R |
|---|---|---|---|
| IOPRP280.IMG | Reemplazos del kernel IOP (reboot `rom0:UDNL`) | — | El reinicio se emula; sus servicios estándar se proveen nativamente |
| RTFSDVD.IRX | Sistema de archivos RenderWare sobre DVD | RPC `0x00475453` (fno 1,3,4,5,6) | Carga y llega a su bucle RPC; lee ISO9660 en crudo vía `sceCdRead` |
| RWA.IRX | RenderWare Audio (SPU2 vía libsd) | Comandos SIF de usuario: EE→IOP cid 0, IOP→EE cid 1 | Pendiente de ejecución; usa `libsd` |
| SDRDRV.IRX | Servidor libsdr | RPC `0x80000701` | Llega a su bucle RPC |
| DBCMAN.IRX | Gestor de mandos (libdbc) | RPC `0x80001300`, `0x8000131B`, `0x8000131C` | Llega a su bucle RPC |
| DS2U.IRX, SIO2D.IRX | Driver DualShock 2 sobre SIO2 | vía dbcman | Pendiente: requiere SIO2 |
| MCMAN/MCSERV.IRX | Tarjeta de memoria | RPC estándar de libmc | Pendiente |
| CDVDSTM.IRX | Streaming de CD/DVD | cdvdman | Pendiente |
| LIBSD.IRX, SIO2MAN.IRX | Drivers de hardware | — | Librerías nativas (`iop_libsd.h`; SIO2 pendiente) |

Las rutas que el juego usa: `cdrom0:\IOPRP280.IMG;1`, `cdrom0:\`, `cdrom0:\MODULES1\`,
`cdrom0:\MODULES2\`, `cdrom0:\PAD2\`. Un módulo inexistente devuelve `KE_NOFILE`, como
en hardware, y el juego decide.

## CD desde los archivos extraídos

`scripts/build_disc_map.py` guarda en `MP2RDISC.BIN` el LBA de cada archivo y los
1010 sectores que no pertenecen a ninguno (descriptores, directorios, tablas de rutas).
`disc_image.h` sirve cualquier sector; `disc_image_probe` comprobó 6108 sectores
idénticos a la ISO. El teléfono necesita los 504 archivos más `MP2RDISC.BIN`.

## Herramientas

`scripts/irx_disasm.py MODULE.IRX <exports> --rpc` desensambla un IRX con las
importaciones nombradas según las tablas `exports.tab` del PS2SDK y lista las
llamadas a `sceSifRegisterRpc`.
