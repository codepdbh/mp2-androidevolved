/* Sustituciones de funciones recompiladas, por dirección invitada. Solo actúan en las
 * llamadas indirectas: las de otros módulos pasan por la IAT y las virtuales por su
 * vtable. Las direcciones salen de los exports (config/pc/modules.json fija las bases). */
#include "recomp_types.h"
#include <math.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>

typedef void (*mp2_override_fn)(void);
#define GUEST_ARG(i) (*(uint32_t*)ADDR(g_esp + 4 + 4 * (i)))
#define GUEST_PTR(va) ((char*)ADDR(va))
/* cdecl: solo saca la dirección de retorno. */
#define RETURN_CDECL(value) do { g_eax = (uint32_t)(value); g_esp += 4; } while (0)

/* _CIpow de msvcr71: ST(1) es la base y ST(0) el exponente. Sus rutas x87 escritas a
 * mano de clasificación y error no se representan bien en la pila de dobles del
 * lifter, igual que en el port de Carbon (misma DLL, base distinta). */
static void ci_pow(void) {
    double exponent = fp_pop_impl(g_st, &g_fp_top);
    double base = fp_pop_impl(g_st, &g_fp_top);
    fp_push_impl(g_st, &g_fp_top, pow(base, exponent));
    g_esp += 4;
}

/* ---- CRT nativa: el motor analiza su base de datos de texto carácter a carácter y
 * cada tolower lifted pasa por _getptd (GetLastError + FlsGetValue + SetLastError).
 * El juego no llama a setlocale: el locale es el "C" y estas son equivalentes. ---- */
static int sign(int value) { return value < 0 ? -1 : value > 0 ? 1 : 0; }
static void crt_memcpy(void) { memmove(GUEST_PTR(GUEST_ARG(0)), GUEST_PTR(GUEST_ARG(1)), GUEST_ARG(2)); RETURN_CDECL(GUEST_ARG(0)); }
static void crt_memset(void) { memset(GUEST_PTR(GUEST_ARG(0)), (int)GUEST_ARG(1), GUEST_ARG(2)); RETURN_CDECL(GUEST_ARG(0)); }
static void crt_memcmp(void) { RETURN_CDECL(sign(memcmp(GUEST_PTR(GUEST_ARG(0)), GUEST_PTR(GUEST_ARG(1)), GUEST_ARG(2)))); }
static void crt_strlen(void) { RETURN_CDECL(strlen(GUEST_PTR(GUEST_ARG(0)))); }
static void crt_strcmp(void) { RETURN_CDECL(sign(strcmp(GUEST_PTR(GUEST_ARG(0)), GUEST_PTR(GUEST_ARG(1))))); }
static void crt_strncmp(void) { RETURN_CDECL(sign(strncmp(GUEST_PTR(GUEST_ARG(0)), GUEST_PTR(GUEST_ARG(1)), GUEST_ARG(2)))); }
static void crt_strcpy(void) { strcpy(GUEST_PTR(GUEST_ARG(0)), GUEST_PTR(GUEST_ARG(1))); RETURN_CDECL(GUEST_ARG(0)); }
static void crt_strncpy(void) { strncpy(GUEST_PTR(GUEST_ARG(0)), GUEST_PTR(GUEST_ARG(1)), GUEST_ARG(2)); RETURN_CDECL(GUEST_ARG(0)); }
static void crt_strcat(void) { strcat(GUEST_PTR(GUEST_ARG(0)), GUEST_PTR(GUEST_ARG(1))); RETURN_CDECL(GUEST_ARG(0)); }
static void crt_strchr(void) {
    uint32_t text = GUEST_ARG(0); const char* base = GUEST_PTR(text);
    const char* found = strchr(base, (int)(GUEST_ARG(1) & 0xff));
    RETURN_CDECL(found ? text + (uint32_t)(found - base) : 0);
}
static void crt_strrchr(void) {
    uint32_t text = GUEST_ARG(0); const char* base = GUEST_PTR(text);
    const char* found = strrchr(base, (int)(GUEST_ARG(1) & 0xff));
    RETURN_CDECL(found ? text + (uint32_t)(found - base) : 0);
}
static int ascii_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static void crt_stricmp(void) {
    const unsigned char* a = (const unsigned char*)GUEST_PTR(GUEST_ARG(0));
    const unsigned char* b = (const unsigned char*)GUEST_PTR(GUEST_ARG(1));
    int ca, cb;
    do { ca = ascii_lower(*a++); cb = ascii_lower(*b++); } while (ca && ca == cb);
    RETURN_CDECL(ca - cb);
}
static void crt_strnicmp(void) {
    const unsigned char* a = (const unsigned char*)GUEST_PTR(GUEST_ARG(0));
    const unsigned char* b = (const unsigned char*)GUEST_PTR(GUEST_ARG(1));
    uint32_t n = GUEST_ARG(2); int ca = 0, cb = 0;
    while (n--) { ca = ascii_lower(*a++); cb = ascii_lower(*b++); if (!ca || ca != cb) break; }
    RETURN_CDECL(ca - cb);
}
static void crt_tolower(void) { int c = (int)GUEST_ARG(0); RETURN_CDECL(ascii_lower(c)); }
static void crt_toupper(void) { int c = (int)GUEST_ARG(0); RETURN_CDECL(c >= 'a' && c <= 'z' ? c - 32 : c); }

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

mp2_override_fn mp2_override_lookup(uint32_t va) {
    switch (va) {
    case 0x0060406Fu: return ci_pow;
    case 0x00602EBBu: case 0x0060298Eu: return crt_memcpy;  /* memcpy, memmove */
    case 0x00601222u: return crt_memset;
    case 0x00603234u: return crt_memcmp;
    case 0x006017D5u: return crt_strlen;
    case 0x00602CC0u: return crt_strcmp;
    case 0x00602955u: return crt_strncmp;
    case 0x0060174Au: return crt_strcpy;
    case 0x006026C3u: return crt_strncpy;
    case 0x00602652u: return crt_strcat;
    case 0x00602811u: return crt_strchr;
    case 0x00603868u: return crt_strrchr;
    case 0x006105DCu: return crt_stricmp;
    case 0x00610724u: return crt_strnicmp;
    case 0x00612396u: return crt_tolower;
    case 0x006149D4u: return crt_toupper;
    case 0x00C15F00u: return menu_update;
    default: return 0;
    }
}
