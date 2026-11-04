/*
 * Matemáticas de msvcr71 en nativo, compartidas por el host de Windows y el de Android.
 *
 * Estas funciones entran por un despachador x87 escrito a mano que clasifica sus
 * argumentos antes de operar. El lifter no reproduce esa clasificación: fmod(0, 40)
 * devolvía 40, así que al reiniciarse el ciclo de la animación de correr el motor tomaba
 * el último fotograma y restaba al personaje toda la distancia recorrida.
 *
 * Solo actúan en las llamadas que pasan por la IAT, que son las de los demás módulos.
 * Las versiones cdecl reciben dobles en la pila; las _CI, en la pila x87 con el primer
 * operando debajo. Todas devuelven en ST(0). Se incluye desde un único archivo por host.
 */
#ifndef MP2_MATH_OVERRIDES_H
#define MP2_MATH_OVERRIDES_H
#include "recomp_types.h"
#include <math.h>

#define MP2_MATH_ARG(i) (*(double*)ADDR(g_esp + 4 + 8 * (i)))
#define MP2_MATH_RESULT(value) do { double _r = (value); fp_push_impl(g_st, &g_fp_top, _r); g_esp += 4; } while (0)
#define MP2_MATH_CDECL1(name, fn) static void name(void) { MP2_MATH_RESULT(fn(MP2_MATH_ARG(0))); }
#define MP2_MATH_CDECL2(name, fn) static void name(void) { MP2_MATH_RESULT(fn(MP2_MATH_ARG(0), MP2_MATH_ARG(1))); }
#define MP2_MATH_CI1(name, fn) static void name(void) { double x = fp_pop_impl(g_st, &g_fp_top); MP2_MATH_RESULT(fn(x)); }
#define MP2_MATH_CI2(name, fn) static void name(void) { double y = fp_pop_impl(g_st, &g_fp_top); double x = fp_pop_impl(g_st, &g_fp_top); MP2_MATH_RESULT(fn(x, y)); }

MP2_MATH_CDECL2(mp2_fmod, fmod)   MP2_MATH_CI2(mp2_ci_fmod, fmod)
MP2_MATH_CDECL2(mp2_pow, pow)     MP2_MATH_CI2(mp2_ci_pow, pow)
MP2_MATH_CDECL2(mp2_atan2, atan2) MP2_MATH_CI2(mp2_ci_atan2, atan2)
MP2_MATH_CDECL1(mp2_sin, sin)     MP2_MATH_CI1(mp2_ci_sin, sin)
MP2_MATH_CDECL1(mp2_cos, cos)     MP2_MATH_CI1(mp2_ci_cos, cos)
MP2_MATH_CDECL1(mp2_tan, tan)     MP2_MATH_CI1(mp2_ci_tan, tan)
MP2_MATH_CDECL1(mp2_atan, atan)   MP2_MATH_CI1(mp2_ci_atan, atan)
MP2_MATH_CDECL1(mp2_asin, asin)   MP2_MATH_CI1(mp2_ci_asin, asin)
MP2_MATH_CDECL1(mp2_acos, acos)   MP2_MATH_CI1(mp2_ci_acos, acos)
MP2_MATH_CDECL1(mp2_exp, exp)     MP2_MATH_CI1(mp2_ci_exp, exp)
MP2_MATH_CDECL1(mp2_log, log)     MP2_MATH_CI1(mp2_ci_log, log)
MP2_MATH_CDECL1(mp2_log10, log10) MP2_MATH_CI1(mp2_ci_log10, log10)
MP2_MATH_CDECL1(mp2_sqrt, sqrt)   MP2_MATH_CI1(mp2_ci_sqrt, sqrt)

/* Direcciones de los exports de msvcr71 con la base de config/pc/modules.json. */
static recomp_func_t mp2_math_lookup(uint32_t va) {
    switch (va) {
    case 0x00603D62u: return mp2_fmod;  case 0x00603D6Cu: return mp2_ci_fmod;
    case 0x00604030u: return mp2_pow;   case 0x0060406Fu: return mp2_ci_pow;
    case 0x00603BDAu: return mp2_atan2; case 0x00603BE4u: return mp2_ci_atan2;
    case 0x006042CCu: return mp2_sin;   case 0x006042B8u: return mp2_ci_sin;
    case 0x00603C02u: return mp2_cos;   case 0x00603BEEu: return mp2_ci_cos;
    case 0x00604434u: return mp2_tan;   case 0x00604420u: return mp2_ci_tan;
    case 0x00603AC7u: return mp2_atan;  case 0x00603B06u: return mp2_ci_atan;
    case 0x00603A10u: return mp2_asin;  case 0x006039FCu: return mp2_ci_asin;
    case 0x00603945u: return mp2_acos;  case 0x00603931u: return mp2_ci_acos;
    case 0x00603CD8u: return mp2_exp;   case 0x00603D13u: return mp2_ci_exp;
    case 0x00603D94u: return mp2_log;   case 0x00603DD3u: return mp2_ci_log;
    case 0x00603EE2u: return mp2_log10; case 0x00603F21u: return mp2_ci_log10;
    case 0x0060437Au: return mp2_sqrt;  case 0x00604366u: return mp2_ci_sqrt;
    default: return 0;
    }
}
#endif
