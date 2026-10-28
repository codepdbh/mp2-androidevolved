# Android: programa completo y prueba en S25

Actualización de interfaz: ⋮ incluye «Invertir imagen verticalmente» como
alternativa manual temporal. Se aplica al copiar el framebuffer al Surface y
se desactiva al iniciar una sesión nueva. No repara el origen del fallo ni
recupera el fondo y el título que faltan en el menú. La APK compila y fue
instalada en el S25; falta comprobar visualmente esta última implementación.

Actualización 0.9 (S25, 2026-10-28): salida AAudio estéreo PCM16 a 48 kHz
para el flujo AutoDMA del core 0. No incluye todavía el mezclador completo
SPU2 de voces y efectos. La cola se regula con el reloj físico de audio.
En la prueba de 15 s hubo 235 presentaciones (15,67/s); pausa y reanudación
pasaron. AAudio consumió 895776 frames y recibió muestras no nulas.
Se descartaron 3072 frames frente a 159744 antes del ajuste de ritmo, en
sesiones de prueba distintas. No constituye una medición de gameplay.

La presentación mezcla campos entrelazados para reducir parpadeo; puede
producir estela. Falta confirmar visualmente la eliminación del temblor.
El dibujo sigue en CPU. Los controles están ocultos inicialmente y se activan
manualmente desde ⋮; tap confirma, swipe navega, dos dedos retroceden y
mantener pulsado abre pausa. La detección automática de gameplay está pendiente.
La descripción de la versión 0.6 que sigue se conserva como historial.

La versión 0.6 sustituye el arranque limitado a InitSys por el registro de las
20876 funciones originales. `GameActivity` presenta la salida del GS en un
SurfaceView y envía controles multitáctiles al DualShock 2 mediante el driver
IOP original. Conserva el icono y las figuritas existentes.

Datos: `/storage/emulated/0/mp2androidevolved`. El ELF se verifica con SHA-256
antes de ejecutar. No se incluye contenido del disco en la APK.

La biblioteca ARM64 se compila primero en `build/android-game`. La APK enlaza
ese archivo estático; no usa el conjunto reducido de inicialización. Ejecutar
`scripts/build_android.py` para reconstruir ambas partes, con límites del
perfil de compilación de `config/development.toml`.

La vista conserva relación 4:3, limita las presentaciones a 30/s de reloj real,
libera el Surface al destruirse y pausa el hilo nativo al pasar a segundo plano.
Salir solicita una parada cooperativa y guarda `files/game.log` en los datos
privados de la app. La prueba antigua de InitSys conserva un breakpoint real
antes de main, aunque el programa completo esté enlazado.

Controles: sticks izquierdo/derecho para mover/mirar; disparo R1; apuntado
rápido R3; recarga Triángulo; salto Cross; acción Square; Bullet Time L2;
Shootdodge L1; armas arriba; analgésico Circle; pausa Start. La barra superior
expone flechas, Cross, Circle y Start para navegar los menús originales.
Referencia: [manual original de PS2, controles predeterminados](https://www.videogamemanual.com/PS2/Max%20Payne%202-%20The%20Fall%20of%20Max%20Payne%20(USA).pdf).
La comprobación de acciones durante gameplay sigue pendiente.

Correcciones gráficas de esta continuación:

- UNPACK V2 duplica XY en ZW, con extensión de signo correspondiente.
- SMODE2 entrelazado con FFMD=FRAME lee media altura por campo, evitando leer
  el siguiente búfer y mostrar el vídeo duplicado.
- Entrada, presentación y parada se exponen como callbacks del runtime.
  La parada se comprueba también durante esperas del procesador principal.

Pruebas reproducibles en el teléfono autorizado:
`scripts/test_android_game.py --serial <serial S25> --output <carpeta privada>
--apk android/app/build/outputs/apk/debug/app-debug.apk`.
Instala sin borrar datos, abre el juego, captura durante 60 s y sale guardando
el log. Comprueba modelo y ELF antes de instalar. Una prueba de arranque no
demuestra que haya gameplay correcto.

Estado previo a validar el dispositivo: persiste texto invertido y pantalla
negra después del aviso de memory card en pruebas de escritorio. La mezcla
de audio SPU2 todavía no está implementada. No se declara una versión jugable.
