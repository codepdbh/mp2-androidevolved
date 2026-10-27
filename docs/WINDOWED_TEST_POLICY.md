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
