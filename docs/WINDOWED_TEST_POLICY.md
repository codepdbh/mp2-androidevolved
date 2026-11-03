# Política de escritorio

Toda prueba gráfica debe abrir una ventana normal, redimensionable, de 960×540;
sin maximizar, fullscreen, topmost, captura inicial del ratón ni cambios de monitor.
Escape y pérdida de foco deberán liberar cualquier captura activada explícitamente.
No ejecutar herramientas externas cuya configuración de ventana no esté comprobada.

Esta iteración no abre interfaces gráficas. `scripts/development.py` lee
`config/development.toml` y rechaza solicitudes prohibidas antes de lanzar procesos.
El mismo ejecutor limita tiempo y memoria RSS del árbol de procesos creado y guarda
salida en archivos. Son límites supervisados cada 200 ms, no una reserva de memoria
del sistema. CMake se invoca con dos trabajos; no hay renderer de escritorio todavía.
Todo futuro launcher gráfico debe consumir esta política antes de crear ventanas.

## Host de PC

`desktop/pc_host/` aplica la política al juego recompilado. Sin `--allow-window` no se abre nada: la primera ventana o Direct3D detiene el proceso. Con `--allow-window`:

- **Ventana.** 960×540, normal y redimensionable: se quitan topmost, maximizar y popup de la ventana principal. Modo ventana forzado en `CreateDevice` y `Reset`.
- **Foco.** La ventana aparece sin activarse y se ignoran `SetForegroundWindow` y la activación en `SetWindowPos`. Al juego se le envía `WM_ACTIVATEAPP` para que no se pause.
- **Ratón y teclado.** DirectInput nunca es exclusivo. `ClipCursor` y `SetCursorPos` se ignoran. Con guion, la entrada real no llega al juego.
- **Sistema.** No se instala el gancho global de teclado del EXE. No se cambia la resolución. Se ignoran prioridades y `SystemParametersInfoA` de escritura: el juego pide `0x61`, que desactiva Ctrl+Alt+Supr.
- **Registro.** Virtual: en memoria y en `work/pc/sandbox/registry.txt`, también para lo que se obtiene con `GetProcAddress` (incidente en `docs/BLOCKERS.md`). El EXE registra la asociación `.mp2s`, que no llega al sistema.
- **Archivos.** Toda creación o escritura va a `work/pc/sandbox/`.
- **Recursos.** Sin sonido salvo `--audio`. Máximo 45 fps, como fija `development.toml`. Los diálogos no se muestran: el de inicio pulsa `Play` solo y cualquier otro detiene el proceso.

Sí se abre una ventana visible mientras dura la prueba, siempre con límite de tiempo o de frames.
