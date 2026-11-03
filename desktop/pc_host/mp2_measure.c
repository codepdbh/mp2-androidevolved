/*
 * mp2_measure.c - mediciones para el port de Android.
 *   - Qué API nativa llama el juego de verdad: mfc71 importa cientos de funciones
 *     que nunca se usan, y lo que hay que implementar es solo lo que aparece aquí.
 *   - Qué controles asigna: X_InputControlButton::set recibe el tipo y el valor,
 *     así se conocen las teclas por defecto sin abrir los .ras.
 * El resultado se escribe en <sandbox>/api_usage.txt.
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "native32.h"
#include "mp2_host.h"

#define MAX_API 4096
typedef struct { uint32_t target; uint64_t calls; } api_row_t;
static api_row_t g_api[MAX_API];
static int g_napi;

void mp2_measure_call(uint32_t target) {
    uint32_t slot = (target * 2654435761u) >> 20;
    for (int probe = 0; probe < MAX_API; probe++) {
        api_row_t* row = &g_api[(slot + probe) & (MAX_API - 1)];
        if (row->target == target) { row->calls++; return; }
        if (!row->target) { row->target = target; row->calls = 1; g_napi++; return; }
    }
}

static int by_name(const void* a, const void* b) {
    return strcmp(((const char(*)[128])a)[0], ((const char(*)[128])b)[0]);
}

void mp2_measure_write(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\api_usage.txt", g_mp2_sandbox);
    FILE* f = fopen(path, "w");
    if (!f) return;
    static char lines[MAX_API][128];
    int n = 0;
    for (int i = 0; i < MAX_API; i++) {
        if (!g_api[i].target) continue;
        const char* name = native32_name(g_api[i].target);
        char fallback[96];
        if (!name) {
            /* COM sin nombrar o import por ordinal: módulo más desplazamiento. */
            HMODULE mod = NULL;
            char file[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (const char*)(uintptr_t)g_api[i].target, &mod))
                GetModuleFileNameA(mod, file, sizeof file);
            const char* base = strrchr(file, '\\');
            snprintf(fallback, sizeof fallback, "%s+0x%X", base ? base + 1 : file,
                     mod ? g_api[i].target - (uint32_t)(uintptr_t)mod : g_api[i].target);
            name = fallback;
        }
        snprintf(lines[n++], 128, "%-60s %llu", name, g_api[i].calls);
    }
    qsort(lines, n, sizeof lines[0], by_name);
    for (int i = 0; i < n; i++) fprintf(f, "%s\n", lines[i]);
    fclose(f);
}

/* ---- X_InputControlButton::set(tipo, valor) ---- */
static uint32_t g_control_set_va;
static recomp_func_t g_control_set_fn;

static void log_control_set(void) {
    const uint32_t* args = (const uint32_t*)(uintptr_t)(g_esp + 4);
    fprintf(stderr, "[MP2:CTRL] botón %08X tipo=%u valor=%u (0x%X) desde sub_%08X\n", g_ecx, args[0], args[1], args[1], g_cur_func);
    g_control_set_fn();
}

void mp2_measure_init(void) {
    uint32_t input = native32_module("X_Inputmfc.dll");
    g_control_set_va = native32_export(input, "?set@X_InputControlButton@@QAEXW4ButtonType@1@H@Z");
    g_control_set_fn = recomp_lookup(g_control_set_va);
}

recomp_func_t recomp_lookup_manual(uint32_t va) {
    return va == g_control_set_va ? log_control_set : NULL;
}
