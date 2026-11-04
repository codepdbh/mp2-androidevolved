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

void mp2_measure_write_patches(void);
void mp2_measure_write_return_targets(void);

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
    mp2_measure_write_patches();
    mp2_measure_write_return_targets();
}

/* ---- X_InputControlButton::set(tipo, valor) ---- */
static uint32_t g_control_set_va;
static recomp_func_t g_control_set_fn;

static void log_control_set(void) {
    const uint32_t* args = (const uint32_t*)(uintptr_t)(g_esp + 4);
    fprintf(stderr, "[MP2:CTRL] botón %08X tipo=%u valor=%u (0x%X) desde sub_%08X\n", g_ecx, args[0], args[1], args[1], g_cur_func);
    g_control_set_fn();
}

/* ---- Parches de código: qué bytes ejecutables cambia un plugin en ejecución ----
 * El código recompilado no ve esas escrituras, así que hay que aplicarlas antes del
 * lift. La captura se hace después de enlazar, porque mfc71 tiene su IAT en .text. */
typedef struct { uint32_t va, size; uint8_t* copy; const char* name; } code_section_t;
static code_section_t g_sections[128];
static int g_nsections;

static void snapshot_code(void) {
    for (int m = 0; m < g_mp2_module_count; m++) {
        const mp2_module_info_t* info = mp2_module_at(m);
        uint8_t* b = (uint8_t*)(uintptr_t)info->base;
        IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
        IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections && g_nsections < 128; i++, s++) {
            if (!(s->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            code_section_t* c = &g_sections[g_nsections++];
            c->va = info->base + s->VirtualAddress;
            c->size = s->Misc.VirtualSize;
            c->name = info->name;
            c->copy = (uint8_t*)malloc(c->size);
            memcpy(c->copy, (void*)(uintptr_t)c->va, c->size);
        }
    }
}

void mp2_measure_write_patches(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\code_patches.txt", g_mp2_sandbox);
    FILE* f = fopen(path, "w");
    if (!f) return;
    int total = 0;
    for (int i = 0; i < g_nsections; i++) {
        const code_section_t* c = &g_sections[i];
        const uint8_t* now = (const uint8_t*)(uintptr_t)c->va;
        for (uint32_t o = 0; o < c->size; o++) {
            if (now[o] == c->copy[o]) continue;
            uint32_t e = o;
            /* Bytes iguales de por medio, hasta 3, siguen siendo el mismo parche. */
            for (uint32_t k = o; k < c->size && k - e <= 3; k++)
                if (now[k] != c->copy[k]) e = k;
            fprintf(f, "%08X %u %s ", c->va + o, e - o + 1, c->name);
            for (uint32_t k = o; k <= e; k++) fprintf(f, "%02X", c->copy[k]);
            fputc(' ', f);
            for (uint32_t k = o; k <= e; k++) fprintf(f, "%02X", now[k]);
            fputc('\n', f);
            total++;
            o = e;
        }
    }
    fclose(f);
    fprintf(stderr, "[MP2:WSF] %d parches de código\n", total);
}

/* Los ganchos del plugin vuelven a direcciones que calcula en ejecución y guarda en sus
 * datos. Cualquiera sin función recompilada debe ser una entrada forzada del catálogo. */
void mp2_measure_write_return_targets(void) {
    const mp2_module_info_t* plugin = NULL;
    for (int m = 0; m < g_mp2_module_count; m++)
        if (strstr(mp2_module_at(m)->name, ".asi")) plugin = mp2_module_at(m);
    if (!plugin) return;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\plugin_targets.txt", g_mp2_sandbox);
    FILE* f = fopen(path, "w");
    if (!f) return;
    uint8_t* b = (uint8_t*)(uintptr_t)plugin->base;
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    int found = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        if ((s->Characteristics & IMAGE_SCN_MEM_EXECUTE) || !(s->Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
        const uint32_t* words = (const uint32_t*)(b + s->VirtualAddress);
        for (uint32_t w = 0; w < s->Misc.VirtualSize / 4; w++) {
            uint32_t value = words[w];
            for (int c = 0; c < g_nsections; c++) {
                const code_section_t* code = &g_sections[c];
                if (value >= code->va && value - code->va < code->size && !recomp_lookup(value)) {
                    fprintf(f, "%08X %s\n", value, code->name);
                    found++;
                }
            }
        }
    }
    fclose(f);
    fprintf(stderr, "[MP2:WSF] %d destinos del plugin sin función\n", found);
}

/* Aislar un gancho: MP2_WSF_SKIP=va,va deshace esos parches en cuanto el plugin los escribe.
 * Las variantes del lift ven el byte original y siguen por el código del juego. */
static uint32_t g_skip_sites[32];
static int g_nskip;

void mp2_measure_restore_skipped(void) {
    for (int s = 0; s < g_nskip; s++)
        for (int i = 0; i < g_nsections; i++) {
            const code_section_t* c = &g_sections[i];
            if (g_skip_sites[s] >= c->va && g_skip_sites[s] - c->va + 8 <= c->size) {
                DWORD old;
                VirtualProtect((void*)(uintptr_t)g_skip_sites[s], 8, PAGE_READWRITE, &old);
                memcpy((void*)(uintptr_t)g_skip_sites[s], c->copy + (g_skip_sites[s] - c->va), 8);
                VirtualProtect((void*)(uintptr_t)g_skip_sites[s], 8, old, &old);
            }
        }
}

void mp2_measure_watch_init(void);

void mp2_measure_init(void) {
    mp2_measure_watch_init();
    const char* skip = getenv("MP2_WSF_SKIP");
    while (skip && *skip && g_nskip < 32) {
        g_skip_sites[g_nskip++] = strtoul(skip, (char**)&skip, 16);
        if (*skip == ',') skip++;
    }
    snapshot_code();
    uint32_t input = native32_module("X_Inputmfc.dll");
    g_control_set_va = native32_export(input, "?set@X_InputControlButton@@QAEXW4ButtonType@1@H@Z");
    g_control_set_fn = recomp_lookup(g_control_set_va);
}

/* MP2_WATCH_ENTRY=va,desplazamiento: vigila un dato de la pila cada vez que se entra en la
 * función, y avisa si recibe la parte alta 0x80000000. */
static uint32_t g_watch_entry, g_watch_offset;
static recomp_func_t g_watch_fn;

static void watch_entry(void) {
    mp2_watch_write(g_esp - g_watch_offset, 0xFFFF0000u, 0x80000000u);
    g_watch_fn();
}

void mp2_measure_watch_init(void) {
    const char* spec = getenv("MP2_WATCH_ENTRY");
    if (!spec) return;
    char* rest = NULL;
    g_watch_entry = strtoul(spec, &rest, 16);
    g_watch_offset = strtoul(rest + 1, NULL, 16);
    g_watch_fn = recomp_lookup(g_watch_entry);
}

#include "../../src/runtime/pc/mp2_math_overrides.h"

recomp_func_t recomp_lookup_manual(uint32_t va) {
    recomp_func_t math = mp2_math_lookup(va);
    if (math) return math;
    if (va == g_control_set_va) return log_control_set;
    return va == g_watch_entry && va ? watch_entry : NULL;
}
