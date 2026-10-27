# Pictogramas propios del HUD

Se generaron diez imágenes originales mediante la herramienta integrada
`image_gen.imagegen`, una llamada por pictograma y fondo transparente. La primera
pistola se utilizó como referencia de estilo para las demás. No se extrajeron gráficos
del APK de Max Payne Mobile. Referencia conceptual: siluetas legibles y controles
monocromos de acción, adaptados a las acciones específicas de MP2.

Archivos definitivos: `android/app/src/main/res/drawable-nodpi/hud_*.png`.
1254×1254 RGBA; se verificó alpha mínimo0/máximo255 en todos. Android carga una
versión reducida en memoria y encuadra los límites opacos al dibujar; se preservan
los PNG originales con su alpha, sin borrar ni alterar el archivo generado de origen.

| Acción | Archivo |
|---|---|
| Disparo | hud_fire.png |
| Apuntar | hud_aim.png |
| Recargar | hud_reload.png |
| Saltar | hud_jump.png |
| Interactuar | hud_interact.png |
| Bullet Time | hud_bullet_time.png |
| Shootdodge | hud_shootdodge.png |
| Cambiar armas | hud_weapons.png |
| Analgésicos | hud_painkiller.png |
| Pausa | hud_pause.png |

Prompts completos, herramienta y procedencia local:
[`config/hud-imagegen-prompts.json`](../config/hud-imagegen-prompts.json).
La solicitud base fue un pictograma HUD noir original, blanco/gris con contorno
oscuro, reconocible a36px, sin palabras, círculos ni fondos, con alpha real. Cada
prompt define su figura; Shootdodge usa un personaje lanzándose con pistola,
Bullet Time un reloj de arena y una bala. Los botones se muestran sin texto durante
la prueba normal; el editor conserva nombres para identificar cada acción.

No hay aún estado de disponibilidad de estas mecánicas derivado del juego. El
color de pulsación representa entrada táctil, no Bullet Time activo en gameplay.
