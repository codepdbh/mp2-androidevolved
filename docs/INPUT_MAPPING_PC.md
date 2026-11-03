# Mapeo de entrada de PC

Flujo: Android Touch → `TouchEngine` → `PcActionMapper` → capa de entrada → lógica recompilada.

## Lo que usa el juego

Comprobado en las tablas de imports y las cadenas de los binarios:

- `X_Inputmfc.dll` importa `DirectInputCreateA` para mouse y joystick. Para el teclado usa `SetWindowsHookExA`, `GetKeyboardState` y `ToAscii`.
- Clases exportadas: `X_Input` (`construct`, `update`, `reset`, `readInputScript`, `isKeyPressed`), `X_InputControlButton`, `X_InputDeviceKeyboard`, `X_InputDeviceMouse`, `X_InputDeviceJoystick`.
- `MaxPayne2.exe` guarda en `Input Settings` los valores `Button0 type`, `Button0 value`, `Button1 type`, `Button1 value`, `Invert Mouse`, `Invert Mouse Wheel`, `Mouse Sensitivity X` y `Y`, `Invert Joystick`, `Joystick Sensitivity` y `Double-Click Speed`.
- Los valores por defecto vienen de `defaultvalues.txt` (`PrimaryDefaultType/Value`, `SecondaryDefaultType/Value`). Se ven en ejecución, abajo, sin abrir los `.ras`.
- `X_Input::readInputScript` y la cadena `input.txt` apuntan a un archivo con los nombres de teclas (`KeyName`, `ScanCode`, `MouseButtonName`). Tampoco se ha leído aún.

## Tabla de controles del jugador

Cadenas de `MaxPayne2.exe` en la tabla de controles:

`RUN`, `WALKBACK`, `STRAFELEFT`, `STRAFERIGHT`, `SHOOT`, `ATTACK`, `RELOAD`, `JUMP`, `CROUCH`, `DODGELEFT`, `DODGERIGHT`, `DODGEFORWARD`, `DODGEBACKWARD`, `DODGEMODIFIER`, `USE`, `SLOT0`…`SLOT10`, `BESTWEAPON`, `NEXTWEAPON`, `PREVIOUSWEAPON`, `PAUSE`, `PAINKILLER`, `SNIPERZOOM`, `SLOWMOTION`, `BULLETTIME`.

`AIM`, `SHOOTDODGELEFT` y similares también aparecen en el EXE, pero pertenecen a los comandos de la IA (`AI_AddCommand`), no a los controles del jugador.

## Asignación

| Acción del HUD | Pictograma | Control del juego | Estado |
|---|---|---|---|
| `MOVE` | joystick | `RUN`, `WALKBACK`, `STRAFELEFT`, `STRAFERIGHT` | IDENTIFICADO; mapeador probado |
| `LOOK` | área libre | Ejes del mouse por `X_InputDeviceMouse` | IDENTIFICADO; delta probado |
| `FIRE` | `hud_fire.png` | `SHOOT` | IDENTIFICADO. `ATTACK` existe y falta saber en qué se diferencian |
| `AIM` | `hud_aim.png` | `SNIPERZOOM` | **Sin equivalente directo.** PC no tiene botón de apuntar: solo el zoom de francotirador. Decisión de diseño pendiente |
| `RELOAD` | `hud_reload.png` | `RELOAD` | IDENTIFICADO |
| `JUMP` | `hud_jump.png` | `JUMP` | IDENTIFICADO |
| `INTERACT` | `hud_interact.png` | `USE` | IDENTIFICADO |
| `BULLET_TIME` | `hud_bullet_time.png` | `SLOWMOTION` | Reacciona. `BULLETTIME` existe, pero no tiene tecla por defecto |
| `SHOOTDODGE` | `hud_shootdodge.png` | `DODGEMODIFIER` + dirección mantenida | Reacciona. `DODGE*` existe, sin tecla por defecto |
| `CHANGE_WEAPON` | `hud_weapons.png` | `NEXTWEAPON` (hoy) | IDENTIFICADO. El selector usará `SLOT0`…`SLOT10` |
| `USE_PAINKILLER` | `hud_painkiller.png` | `PAINKILLER` | IDENTIFICADO |
| `PAUSE` | `hud_pause.png` | `PAUSE` | IDENTIFICADO |

`PcActionMapper` produce el estado, pero aún no hay capa que lo entregue a `X_Input`.

## Lo comprobado en el juego

`desktop/pc_host/mp2_d3d8_trace.c` sustituye lo que devuelve DirectInput desde un guion (`config/pc/input_*.txt`). Es la costura que usará el mapeador en Android.

- `X_InputDeviceMouse::update` lee `GetDeviceState` (formato de 20 bytes) para los ejes y `GetDeviceData` para los flancos de botón.
- El ratón del menú avanza 1,25 píxeles por unidad.
- **Comprobado:** `W` (DIK 17) corre hacia delante, `A` (DIK 30) se desplaza a la izquierda, los ejes del ratón giran la cámara y el clic izquierdo activa el menú.
- **Sin confirmar:** clic izquierdo como disparo, porque Max no lleva arma en ese tramo.

## Controles por defecto

Medidos interceptando `X_InputControlButton::set` (`desktop/pc_host/mp2_measure.c`). Están en `pcDefaultBinding`.

| Control | Primario | Secundario |
|---|---|---|
| `RUN`, `WALKBACK`, `STRAFELEFT`, `STRAFERIGHT` | W, S, A, D | Flechas |
| `SHOOT` | Clic izquierdo | |
| `ATTACK` | F | Botón central |
| `RELOAD` | R | Insert |
| `JUMP` | Espacio | Mayús derecha |
| `CROUCH` | C | Alt derecho |
| `DODGELEFT`, `DODGERIGHT`, `DODGEFORWARD`, `DODGEBACKWARD` | Sin asignar | |
| `DODGEMODIFIER` | Mayús izquierda | Ctrl derecho |
| `USE` | E | Enter |
| `SLOT0`…`SLOT5` | 1…6 | |
| `BESTWEAPON` | G | |
| `NEXTWEAPON` / `PREVIOUSWEAPON` | `[` / `]` | Rueda |
| `PAUSE` | P | Pausa |
| `PAINKILLER` | Tab | Retroceso |
| `SNIPERZOOM` | Q | Num 1 |
| `SLOWMOTION` | Clic derecho | |
| `BULLETTIME` | Sin asignar | |

## Estado real para los indicadores del HUD

Los exports dan acceso por nombre, sin adivinar offsets. Sirven para que el pictograma refleje el estado real del juego:

- `X_Character::getPainkillerCount`
- `X_CharacterProperties::hasBulletTimeLeftToShootDodge`, `canShootDodgeWithCurrentWeapon`, `cantPerformShootDodge`
- `X_DifficultyLevelContainer::getBulletTimeGainedPerKillMultiplier`, `getHealthGainedFromPainkiller`

## Pruebas

`tests/pc_action_mapper_test.cpp` (compilada con MSVC x64, pasa): movimiento, cámara y disparo con dedos independientes; Shootdodge con el vector de movimiento; toques más cortos que un frame; zona muerta; cancelación global sin controles retenidos; sensibilidad e inversión de eje.
