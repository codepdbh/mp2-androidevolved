/*
 * mp2_host.c - host de validación en Windows para Max Payne 2 de PC recompilado.
 *
 *   mp2_pc_host.exe --game-root DIR --guest-dir DIR --sandbox DIR [--registry ARCHIVO]
 *                   [--allow-window] [--game-args "-nodialog -window"] [--stop-after-frames N]
 *                   [--audio] [--trace-native]
 *
 * Mismo flujo que los ports de NFS (pcrecomp native32), con varios módulos:
 *   1. el lanzador reserva el rango invitado en un hijo suspendido, antes de que
 *      el cargador de Windows lo ocupe;
 *   2. mapea cada módulo en su base fija desde la copia ya relocalizada;
 *   3. enlaza imports: entre módulos a código recompilado, el resto a Windows;
 *   4. ejecuta los DllMain recompilados en orden de dependencias;
 *   5. ejecuta el entry point original de MaxPayne2.exe.
 *
 * Sin --allow-window no se abre ninguna ventana: al llegar a la primera, registra
 * la frontera y termina. Con --allow-window se aplica docs/WINDOWED_TEST_POLICY.md.
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "recomp_trace.h"
#include "native32.h"
#include "mp2_modules_gen.h"
#include "mp2_host.h"

char g_mp2_game_root[MAX_PATH];
char g_mp2_guest_dir[MAX_PATH];
char g_mp2_sandbox[MAX_PATH];
int g_mp2_allow_window;
int g_mp2_widescreen;
char g_mp2_cmdline[1024];
wchar_t g_mp2_cmdline_w[1024];
const int g_mp2_module_count = sizeof g_mp2_modules / sizeof g_mp2_modules[0];

#define GUEST_LOW 0x00400000u
#define GUEST_SPAN (MP2_GUEST_END - GUEST_LOW)
#define GAME_THREAD_STACK (256u << 20)

const mp2_module_info_t* mp2_module_at(int index) {
    return (const mp2_module_info_t*)&g_mp2_modules[index];
}

static int launch_child(void) {
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    SetEnvironmentVariableA("MP2_RECOMP_CHILD", "1");
    STARTUPINFOA si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(self, GetCommandLineA(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "[MP2:BOOT] lanzador: CreateProcess falló (%lu)\n", GetLastError());
        return 2;
    }
    if (!VirtualAllocEx(pi.hProcess, (void*)(uintptr_t)GUEST_LOW, GUEST_SPAN, MEM_RESERVE, PAGE_NOACCESS)) {
        fprintf(stderr, "[MP2:BOOT] lanzador: no se reservó 0x%08X+0x%X (%lu)\n", GUEST_LOW, GUEST_SPAN, GetLastError());
        TerminateProcess(pi.hProcess, 2);
        return 2;
    }
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 0;
    GetExitCodeProcess(pi.hProcess, &rc);
    fprintf(stderr, "[MP2:BOOT] lanzador: el proceso del juego terminó con 0x%08lX\n", rc);
    return (int)rc;
}

static int is_dynamic(const mp2_module_t* m) { return !m->is_static; }

/* Mapea todos los módulos antes de enlazar: native32_bind necesita ver los
 * módulos invitados para llevar sus imports a código recompilado. */
static int map_modules(void) {
    char path[MAX_PATH];
    for (int i = 0; i < g_mp2_module_count; i++) {
        const mp2_module_t* m = &g_mp2_modules[i];
        snprintf(path, sizeof path, "%s\\%s", g_mp2_guest_dir, m->name);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
            fprintf(stderr, "[MP2:BOOT] falta %s\n", path);
            return 0;
        }
        uint32_t span = native32_map(path, m->base);
        if (!span) {
            fprintf(stderr, "[MP2:BOOT] no se pudo mapear %s en 0x%08X\n", m->name, m->base);
            return 0;
        }
        fprintf(stderr, "[MP2:BOOT] %-32s 0x%08X +0x%X%s\n", m->name, m->base, span, is_dynamic(m) ? " (dinámico)" : "");
    }
    return 1;
}

static int bind_modules(void) {
    int missing = 0;
    for (int i = 0; i < g_mp2_module_count; i++) {
        const mp2_module_t* m = &g_mp2_modules[i];
        int n = native32_bind(m->base, g_mp2_shims, g_mp2_nshims);
        if (n) fprintf(stderr, "[MP2:IMPORT] %s: %d imports sin resolver\n", m->name, n);
        missing += n;
    }
    return missing;
}

/* El cargador de Windows corre DllMain en orden de dependencias; el orden de
 * g_mp2_modules ya está comprobado contra los imports (merge_dispatch.py). */
int mp2_run_dllmain(const char* name, uint32_t base, uint32_t entry) {
    uint32_t args[3] = { base, DLL_PROCESS_ATTACH, 0 };
    fprintf(stderr, "[MP2:DLL] DllMain %s (0x%08X)\n", name, entry);
    fflush(stderr);
    native32_call_guest(entry, 3, args);
    fprintf(stderr, "[MP2:DLL] DllMain %s -> eax=%08X\n", name, g_eax);
    return g_eax != 0;
}

/* Latido: distingue un bucle (función y contadores quietos) de un progreso lento.
 * Lecturas sin bloqueo, solo diagnóstico. */
static uint32_t g_stop_after_seconds;

static DWORD WINAPI watchdog(void* unused) {
    (void)unused;
    uint32_t last_icalls = 0, last_native = 0;
    DWORD started = GetTickCount();
    for (;;) {
        Sleep(10000);
        if (g_stop_after_seconds && GetTickCount() - started >= g_stop_after_seconds * 1000) {
            fprintf(stderr, "[MP2:BOOT] límite de %u s alcanzado\n", g_stop_after_seconds);
            mp2_d3d8_write_report();
            mp2_measure_write();
            fflush(stderr);
            TerminateProcess(GetCurrentProcess(), 0x4D503203);
        }
        uint32_t icalls = g_icall_count, native = g_native_ring_idx;
        const char* import = g_cur_import;
        fprintf(stderr, "[MP2:CPU] latido sub_%08X indirectas +%u nativas +%u import %s\n", g_cur_func,
                icalls - last_icalls, native - last_native, import ? import : "-");
        for (uint32_t i = 1; i <= 6 && i <= native; i++) {
            native32_call_rec_t* r = &g_native_ring[(native - i) & (NATIVE32_RING - 1)];
            const char* n = native32_name(r->target);
            fprintf(stderr, "[MP2:CPU]   %-40s desde sub_%08X (%08X %08X %08X) -> %08X\n", n ? n : "?", r->from, r->a0, r->a1, r->a2, r->ret);
        }
        fflush(stderr);
        last_icalls = icalls;
        last_native = native;
    }
}

static DWORD WINAPI game_thread(void* unused) {
    (void)unused;
    uint32_t exe_entry = 0;
    for (int i = 0; i < g_mp2_module_count; i++) {
        const mp2_module_t* m = &g_mp2_modules[i];
        if (m->base == GUEST_LOW) { exe_entry = m->entry; continue; }
        if (!m->is_static) continue;
        if (!mp2_run_dllmain(m->name, m->base, m->entry)) {
            fprintf(stderr, "[MP2:BOOT] DllMain de %s rechazó PROCESS_ATTACH\n", m->name);
            return 5;
        }
    }
    fprintf(stderr, "[MP2:BOOT] MILESTONE todos los DllMain estáticos completados\n");
    fprintf(stderr, "[MP2:BOOT] entrando al entry point original 0x%08X\n", exe_entry);
    fflush(stderr);
    native32_call_guest(exe_entry, 0, NULL);
    fprintf(stderr, "[MP2:BOOT] el entry point retornó (eax=%08X)\n", g_eax);
    return g_eax;
}

int main(int argc, char** argv) {
    if (!getenv("MP2_RECOMP_CHILD")) return launch_child();

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)(uintptr_t)GUEST_LOW, &mbi, sizeof mbi) || mbi.State != MEM_RESERVE ||
        mbi.RegionSize < GUEST_SPAN) {
        fprintf(stderr, "[MP2:BOOT] el rango invitado no está reservado\n");
        return 2;
    }
    const char* game_args = "-nodialog -window";
    static char registry_path[MAX_PATH], script_path[MAX_PATH];
    const char* registry = NULL;
    const char* input_script = NULL;
    int audio = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--allow-window")) g_mp2_allow_window = 1;
        else if (!strcmp(argv[i], "--widescreen")) g_mp2_widescreen = 1;
        else if (!strcmp(argv[i], "--audio")) audio = 1;
        else if (!strcmp(argv[i], "--game-args") && i + 1 < argc) game_args = argv[++i];
        else if (!strcmp(argv[i], "--registry") && i + 1 < argc) { GetFullPathNameA(argv[++i], MAX_PATH, registry_path, NULL); registry = registry_path; }
        else if (!strcmp(argv[i], "--stop-after-frames") && i + 1 < argc) g_mp2_stop_after_frames = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--stop-after-seconds") && i + 1 < argc) g_stop_after_seconds = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--capture-every") && i + 1 < argc) g_capture_every = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--input-script") && i + 1 < argc) { GetFullPathNameA(argv[++i], MAX_PATH, script_path, NULL); input_script = script_path; }
        else if (!strcmp(argv[i], "--game-root") && i + 1 < argc) GetFullPathNameA(argv[++i], MAX_PATH, g_mp2_game_root, NULL);
        else if (!strcmp(argv[i], "--guest-dir") && i + 1 < argc) GetFullPathNameA(argv[++i], MAX_PATH, g_mp2_guest_dir, NULL);
        else if (!strcmp(argv[i], "--sandbox") && i + 1 < argc) GetFullPathNameA(argv[++i], MAX_PATH, g_mp2_sandbox, NULL);
        else if (!strcmp(argv[i], "--trace-native")) native32_trace_native = 1;
        else if (!strcmp(argv[i], "--trace-callbacks")) native32_trace_callbacks = 1;
        else {
            int used = recomp_trace_arg(argc, argv, i);
            if (used > 0) i += used - 1;
        }
    }
    if (!g_mp2_game_root[0] || !g_mp2_guest_dir[0] || !g_mp2_sandbox[0]) {
        fprintf(stderr, "uso: mp2_pc_host --game-root DIR --guest-dir DIR --sandbox DIR\n");
        return 2;
    }
    CreateDirectoryA(g_mp2_sandbox, NULL);
    if (!SetCurrentDirectoryA(g_mp2_game_root)) {
        fprintf(stderr, "[MP2:BOOT] no existe %s\n", g_mp2_game_root);
        return 2;
    }
    fprintf(stderr, "[MP2:BOOT] %u funciones recompiladas, raíz %s\n", recomp_dispatch_count, g_mp2_game_root);

    snprintf(g_mp2_cmdline, sizeof g_mp2_cmdline, "\"%s\\MaxPayne2.exe\" %s", g_mp2_game_root, game_args);
    MultiByteToWideChar(CP_ACP, 0, g_mp2_cmdline, -1, g_mp2_cmdline_w, 1024);
    fprintf(stderr, "[MP2:BOOT] línea de comandos: %s\n", g_mp2_cmdline);
    /* Trabajas en esta máquina: sin sonido salvo que se pida. Volumen de la sesión de este proceso. */
    if (!audio) waveOutSetVolume((HWAVEOUT)0, 0);

    native32_init();
    native32_tls_array = (uint32_t)(uintptr_t)calloc(16, 4);
    mp2_install_crash_handler();
    mp2_registry_init(registry ? registry : "");
    mp2_d3d8_trace_init();
    if (input_script) mp2_input_script_load(input_script);
    if (!map_modules()) return 2;
    int missing = bind_modules();
    mp2_measure_init();
    fprintf(stderr, "[MP2:IMPORT] enlace completo, %d sin resolver\n", missing);

    /* El lifter no modela 3DNow!: el código con despacho por CPU debe ir por SSE/x87. */
    g_cpuid_edx_ext &= ~0xC0000000u;
    /* Ni SSE3 ni posteriores, como en Android: el CRT moderno del plugin elige sus
     * rutas por CPUID, y el lifter llega hasta SSE2. */
    g_cpuid_ecx1 = 0;

    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    HANDLE th = CreateThread(NULL, GAME_THREAD_STACK, game_thread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    WaitForSingleObject(th, INFINITE);
    DWORD rc = 0;
    GetExitCodeThread(th, &rc);
    fflush(stderr);
    return (int)rc;
}
