/*
 * mp2_registry.c - registro virtual del juego.
 *
 * MFC guarda los ajustes en HKCU y el EXE registra asociaciones de archivo en
 * HKCR. Ninguna llamada llega al registro real: las claves viven en memoria y
 * se guardan en <sandbox>/registry.txt. Los valores iniciales salen de
 * config/pc/default_registry.txt. El formato es una línea por valor:
 *   clave|nombre|tipo|bytes en hexadecimal
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

#define ARG(i) (((uint32_t*)(uintptr_t)(g_esp + 4))[i])
#define RET(value, nargs) do { g_eax = (uint32_t)(value); g_esp += 4 + 4 * (nargs); } while (0)
#define MAX_KEYS 512
#define MAX_VALUES 2048
#define MAX_HANDLES 256
#define HANDLE_BASE 0x4D520000u

typedef struct { char key[260]; char name[128]; uint32_t type, size; uint8_t* data; } reg_value_t;

static char g_keys[MAX_KEYS][260];
static int g_nkeys;
static reg_value_t g_values[MAX_VALUES];
static int g_nvalues;
static char g_handles[MAX_HANDLES][260];

static int key_exists(const char* path) {
    for (int i = 0; i < g_nkeys; i++)
        if (!_stricmp(g_keys[i], path)) return 1;
    return 0;
}

static void add_key(const char* path) {
    /* Con todos sus padres, como crea las claves el registro real. */
    char part[260];
    strncpy(part, path, sizeof part - 1);
    part[sizeof part - 1] = 0;
    for (char* p = part; ; p++) {
        if (*p == '\\' || !*p) {
            char saved = *p;
            *p = 0;
            if (!key_exists(part) && g_nkeys < MAX_KEYS) strcpy(g_keys[g_nkeys++], part);
            *p = saved;
        }
        if (!*p) break;
    }
}

static reg_value_t* find_value(const char* key, const char* name) {
    for (int i = 0; i < g_nvalues; i++)
        if (!_stricmp(g_values[i].key, key) && !_stricmp(g_values[i].name, name)) return &g_values[i];
    return NULL;
}

static void store_value(const char* key, const char* name, uint32_t type, const void* data, uint32_t size) {
    reg_value_t* v = find_value(key, name);
    if (!v) {
        if (g_nvalues == MAX_VALUES) { fprintf(stderr, "[MP2:REG] tabla de valores llena\n"); return; }
        v = &g_values[g_nvalues++];
        strncpy(v->key, key, sizeof v->key - 1);
        strncpy(v->name, name, sizeof v->name - 1);
    }
    free(v->data);
    v->type = type;
    v->size = size;
    v->data = (uint8_t*)malloc(size ? size : 1);
    if (size) memcpy(v->data, data, size);
}

static void load_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[4096];
    int count = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* key = strtok(line, "|");
        char* name = strtok(NULL, "|");
        char* type = strtok(NULL, "|");
        char* hex = strtok(NULL, "|\r\n");
        if (!key || !name || !type) continue;
        static uint8_t data[2048];
        uint32_t size = 0;
        for (const char* h = hex; h && h[0] && h[1] && size < sizeof data; h += 2) {
            unsigned b;
            if (sscanf(h, "%2x", &b) != 1) break;
            data[size++] = (uint8_t)b;
        }
        add_key(key);
        store_value(key, strcmp(name, "@") ? name : "", strtoul(type, NULL, 10), data, size);
        count++;
    }
    fclose(f);
    fprintf(stderr, "[MP2:REG] %d valores de %s\n", count, path);
}

static void save_all(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\registry.txt", g_mp2_sandbox);
    FILE* f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < g_nvalues; i++) {
        reg_value_t* v = &g_values[i];
        fprintf(f, "%s|%s|%u|", v->key, v->name[0] ? v->name : "@", v->type);
        for (uint32_t b = 0; b < v->size; b++) fprintf(f, "%02x", v->data[b]);
        fputc('\n', f);
    }
    fclose(f);
}

void mp2_registry_init(const char* defaults) {
    /* Existen en cualquier Windows: MFC abre HKCU\software antes de crear sus claves. */
    add_key("HKCU\\Software");
    add_key("HKLM\\SOFTWARE");
    load_file(defaults);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\registry.txt", g_mp2_sandbox);
    load_file(path);
}

static const char* handle_path(uint32_t h) {
    switch (h) {
    case 0x80000000u: return "HKCR";
    case 0x80000001u: return "HKCU";
    case 0x80000002u: return "HKLM";
    case 0x80000003u: return "HKU";
    case 0x80000005u: return "HKCC";
    }
    if (h >= HANDLE_BASE && h < HANDLE_BASE + MAX_HANDLES && g_handles[h - HANDLE_BASE][0]) return g_handles[h - HANDLE_BASE];
    return NULL;
}

static uint32_t new_handle(const char* path) {
    for (int i = 0; i < MAX_HANDLES; i++)
        if (!g_handles[i][0]) {
            strncpy(g_handles[i], path, 259);
            return HANDLE_BASE + i;
        }
    fprintf(stderr, "[MP2:REG] sin identificadores libres\n");
    return 0;
}

static int join(uint32_t parent, const char* sub, char* out, size_t size) {
    const char* base = handle_path(parent);
    if (!base) return 0;
    if (sub && *sub) snprintf(out, size, "%s\\%s", base, sub);
    else snprintf(out, size, "%s", base);
    return 1;
}

static uint32_t open_key(uint32_t parent, const char* sub, uint32_t* out, int create, const char* api) {
    char path[260];
    if (!join(parent, sub, path, sizeof path)) return ERROR_INVALID_HANDLE;
    int exists = key_exists(path);
    if (create && !exists) { add_key(path); exists = 1; }
    fprintf(stderr, "[MP2:REG] %s %s -> %s\n", api, path, exists ? "ok" : "no existe");
    if (!exists) return ERROR_FILE_NOT_FOUND;
    uint32_t h = new_handle(path);
    if (!h) return ERROR_TOO_MANY_OPEN_FILES;
    *out = h;
    return ERROR_SUCCESS;
}

void mp2_RegOpenKeyA(void) { uint32_t r = open_key(ARG(0), (const char*)(uintptr_t)ARG(1), (uint32_t*)(uintptr_t)ARG(2), 0, "RegOpenKeyA"); RET(r, 3); }
void mp2_RegOpenKeyExA(void) { uint32_t r = open_key(ARG(0), (const char*)(uintptr_t)ARG(1), (uint32_t*)(uintptr_t)ARG(4), 0, "RegOpenKeyExA"); RET(r, 5); }
void mp2_RegCreateKeyA(void) { uint32_t r = open_key(ARG(0), (const char*)(uintptr_t)ARG(1), (uint32_t*)(uintptr_t)ARG(2), 1, "RegCreateKeyA"); RET(r, 3); }
void mp2_RegCreateKeyExA(void) {
    uint32_t r = open_key(ARG(0), (const char*)(uintptr_t)ARG(1), (uint32_t*)(uintptr_t)ARG(7), 1, "RegCreateKeyExA");
    if (r == ERROR_SUCCESS && ARG(8)) *(uint32_t*)(uintptr_t)ARG(8) = REG_OPENED_EXISTING_KEY;
    RET(r, 9);
}

void mp2_RegCloseKey(void) {
    uint32_t h = ARG(0);
    if (h >= HANDLE_BASE && h < HANDLE_BASE + MAX_HANDLES) g_handles[h - HANDLE_BASE][0] = 0;
    RET(ERROR_SUCCESS, 1);
}

static uint32_t query(const char* key, const char* name, uint32_t* type, uint8_t* data, uint32_t* size) {
    reg_value_t* v = find_value(key, name);
    fprintf(stderr, "[MP2:REG] consulta %s [%s] -> %s\n", key, name, v ? "ok" : "sin valor");
    if (!v) return ERROR_FILE_NOT_FOUND;
    if (type) *type = v->type;
    if (data && size && *size < v->size) { *size = v->size; return ERROR_MORE_DATA; }
    if (data) memcpy(data, v->data, v->size);
    if (size) *size = v->size;
    return ERROR_SUCCESS;
}

void mp2_RegQueryValueExA(void) {
    const char* key = handle_path(ARG(0));
    const char* name = ARG(1) ? (const char*)(uintptr_t)ARG(1) : "";
    uint32_t r = key ? query(key, name, (uint32_t*)(uintptr_t)ARG(3), (uint8_t*)(uintptr_t)ARG(4), (uint32_t*)(uintptr_t)ARG(5)) : ERROR_INVALID_HANDLE;
    RET(r, 6);
}

void mp2_RegQueryValueA(void) {
    char path[260];
    uint32_t r = join(ARG(0), (const char*)(uintptr_t)ARG(1), path, sizeof path)
        ? query(path, "", NULL, (uint8_t*)(uintptr_t)ARG(2), (uint32_t*)(uintptr_t)ARG(3)) : ERROR_INVALID_HANDLE;
    RET(r, 4);
}

void mp2_RegSetValueExA(void) {
    const char* key = handle_path(ARG(0));
    const char* name = ARG(1) ? (const char*)(uintptr_t)ARG(1) : "";
    if (!key) { RET(ERROR_INVALID_HANDLE, 6); return; }
    fprintf(stderr, "[MP2:REG] escritura %s [%s]\n", key, name);
    store_value(key, name, ARG(3), (const void*)(uintptr_t)ARG(4), ARG(5));
    save_all();
    RET(ERROR_SUCCESS, 6);
}

void mp2_RegSetValueA(void) {
    char path[260];
    if (!join(ARG(0), (const char*)(uintptr_t)ARG(1), path, sizeof path)) { RET(ERROR_INVALID_HANDLE, 5); return; }
    add_key(path);
    fprintf(stderr, "[MP2:REG] escritura %s [@] (virtual, no llega al sistema)\n", path);
    store_value(path, "", ARG(2), (const void*)(uintptr_t)ARG(3), ARG(4));
    save_all();
    RET(ERROR_SUCCESS, 5);
}

void mp2_RegDeleteValueA(void) {
    const char* key = handle_path(ARG(0));
    const char* name = ARG(1) ? (const char*)(uintptr_t)ARG(1) : "";
    reg_value_t* v = key ? find_value(key, name) : NULL;
    if (!v) { RET(ERROR_FILE_NOT_FOUND, 2); return; }
    free(v->data);
    *v = g_values[--g_nvalues];
    save_all();
    RET(ERROR_SUCCESS, 2);
}

void mp2_RegDeleteKeyA(void) {
    char path[260];
    if (!join(ARG(0), (const char*)(uintptr_t)ARG(1), path, sizeof path)) { RET(ERROR_INVALID_HANDLE, 2); return; }
    for (int i = 0; i < g_nkeys; i++)
        if (!_stricmp(g_keys[i], path)) {
            strcpy(g_keys[i], g_keys[--g_nkeys]);
            RET(ERROR_SUCCESS, 2);
            return;
        }
    RET(ERROR_FILE_NOT_FOUND, 2);
}

void mp2_RegEnumKeyA(void) {
    const char* base = handle_path(ARG(0));
    uint32_t index = ARG(1), seen = 0;
    char* out = (char*)(uintptr_t)ARG(2);
    uint32_t size = ARG(3);
    size_t n = base ? strlen(base) : 0;
    for (int i = 0; base && i < g_nkeys; i++)
        if (!_strnicmp(g_keys[i], base, n) && g_keys[i][n] == '\\' && !strchr(g_keys[i] + n + 1, '\\') && seen++ == index) {
            if (strlen(g_keys[i] + n + 1) >= size) { RET(ERROR_MORE_DATA, 4); return; }
            strcpy(out, g_keys[i] + n + 1);
            RET(ERROR_SUCCESS, 4);
            return;
        }
    RET(ERROR_NO_MORE_ITEMS, 4);
}
