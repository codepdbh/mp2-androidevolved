/* Sustituciones de funciones recompiladas, por dirección invitada. Solo actúan en las
 * llamadas indirectas: las de otros módulos pasan por la IAT y las virtuales por su
 * vtable. Las direcciones salen de los exports (config/pc/modules.json fija las bases). */
#include "recomp_types.h"
#include "../../../../../src/runtime/pc/mp2_math_overrides.h"
#include <math.h>
#include <string.h>
#include <time.h>

typedef void (*mp2_override_fn)(void);

/* Las versiones nativas de memcpy, strcmp, tolower y compañía se probaron y se retiraron:
 * con ellas la partida nueva se quedaba en negro tras la novela gráfica, y solo adelantaban
 * unos 4 s la llegada al menú. La CRT sigue siendo la recompilada. */

/* ---- Menús: X_MenuModeBase::update(float) lleva el cursor en [objeto+0xBD]+0x1A0/+0x1A4,
 * en unidades 640x480 ya limitadas. Un toque lo coloca exacto, sin pasar por el ratón
 * relativo, cuya escala depende del tiempo de frame. Además, que esta función corra es
 * la señal fiable de "hay un menú en pantalla" para el HUD. ---- */
void sub_00C15F00(void);
volatile int mp2_menu_target_pending;
volatile float mp2_menu_target_x, mp2_menu_target_y;
volatile long long mp2_menu_last_ms;
static void menu_update(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    mp2_menu_last_ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    if (mp2_menu_target_pending) {
        mp2_menu_target_pending = 0;
        uint32_t state = *(uint32_t*)ADDR(g_ecx + 0xBD);
        float x = mp2_menu_target_x, y = mp2_menu_target_y;
        memcpy((void*)ADDR(state + 0x1A0), &x, 4);
        memcpy((void*)ADDR(state + 0x1A4), &y, 4);
    }
    sub_00C15F00();
}

/* ---- Novela gráfica: MP_GraphicNovelMode::update(float) lleva su propio cursor en
 * [objeto+0x107]+0x1A0/+0x1A4 (640x480, igual que los menús) y solo lo mueve con el ratón
 * relativo; luego mira isPressed(0) sobre esa posición para los botones de abajo. El toque
 * lo coloca en el dedo antes de que el juego lo lea. ---- */
void sub_00484AE0(void);
static void novel_update(void) {
    if (mp2_menu_target_pending) {
        mp2_menu_target_pending = 0;
        uint32_t state = *(uint32_t*)ADDR(g_ecx + 0x107);
        float x = mp2_menu_target_x, y = mp2_menu_target_y;
        memcpy((void*)ADDR(state + 0x1A0), &x, 4);
        memcpy((void*)ADDR(state + 0x1A4), &y, 4);
    }
    sub_00484AE0();
}

/* X_InputDeviceMouse::getPositionRelative: solo la pide quien mueve la cámara del jugador;
 * los menús usan la variante NoInvert. Es la señal de "se está jugando". */
void sub_00A56B50(void);
volatile long long mp2_look_last_ms;
static void mouse_relative(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    mp2_look_last_ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    sub_00A56B50();
}

mp2_override_fn mp2_override_lookup(uint32_t va) {
    recomp_func_t math = mp2_math_lookup(va);
    if (math) return math;
    switch (va) {
    case 0x00A56B50u: return mouse_relative;
    case 0x00C15F00u: return menu_update;
    case 0x00484AE0u: return novel_update;
    default: return 0;
    }
}
