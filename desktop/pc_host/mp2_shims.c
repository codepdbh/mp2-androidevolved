/*
 * mp2_shims.c - imports que el host no deja pasar tal cual a Windows.
 *
 * Tres motivos, y nada más:
 *   - módulos: los invitados no son módulos de Windows, así que GetModuleHandle,
 *     LoadLibrary, GetProcAddress y compañía deben resolverlos aquí;
 *   - escritorio: docs/WINDOWED_TEST_POLICY.md. Ninguna ventana, diálogo o cambio
 *     de modo en esta fase: se registra la frontera y el proceso termina;
 *   - sistema: el juego no toca registro, prioridades ni archivos fuera de su sandbox.
 * Lo no cubierto va a Windows sin cambios. Nada devuelve cero para ocultar un bloqueo.
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"
#include "native32.h"
#include "mp2_host.h"

#define ARG(i) (((uint32_t*)(uintptr_t)(g_esp + 4))[i])
#define RET(value, nargs) do { g_eax = (uint32_t)(value); g_esp += 4 + 4 * (nargs); } while (0)

static const mp2_module_info_t* find_module(const char* name) {
    const char* file = name;
    for (const char* p = name; *p; p++)
        if (*p == '\\' || *p == '/') file = p + 1;
    char stem[MAX_PATH];
    strncpy(stem, file, MAX_PATH - 1);
    stem[MAX_PATH - 1] = 0;
    if (!strchr(stem, '.')) strcat(stem, ".dll");
    for (int i = 0; i < g_mp2_module_count; i++) {
        const mp2_module_info_t* m = mp2_module_at(i);
        if (!_stricmp(m->name, stem)) return m;
    }
    return NULL;
}

static const mp2_module_info_t* module_by_base(uint32_t base) {
    for (int i = 0; i < g_mp2_module_count; i++) {
        const mp2_module_info_t* m = mp2_module_at(i);
        if (m->base == base) return m;
    }
    return NULL;
}

static const mp2_module_info_t* exe_module(void) { return module_by_base(0x00400000u); }

/* ---- línea de comandos: la del juego, no la del host ---- */
static void s_GetCommandLineA(void) { RET(g_mp2_cmdline, 0); }
static void s_GetCommandLineW(void) { RET(g_mp2_cmdline_w, 0); }

/* ---- módulos ---- */
static void s_GetModuleHandleA(void) {
    const char* name = (const char*)(uintptr_t)ARG(0);
    if (!name) { RET(0x00400000u, 1); return; }
    const mp2_module_info_t* m = find_module(name);
    RET(m ? m->base : (uint32_t)(uintptr_t)GetModuleHandleA(name), 1);
}

static void s_GetModuleHandleW(void) {
    const wchar_t* wname = (const wchar_t*)(uintptr_t)ARG(0);
    if (!wname) { RET(0x00400000u, 1); return; }
    char name[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, wname, -1, name, MAX_PATH, NULL, NULL);
    const mp2_module_info_t* m = find_module(name);
    RET(m ? m->base : (uint32_t)(uintptr_t)GetModuleHandleW(wname), 1);
}

/* La ruta que vería el juego original: la carpeta de instalación. */
static int guest_module_path(uint32_t handle, char* out, size_t size) {
    const mp2_module_info_t* m = handle ? module_by_base(handle) : exe_module();
    if (!m) return 0;
    int in_driver = !_stricmp(m->name, "binkw32.dll") || !_stricmp(m->name, "e2_d3d8_driver_mfc.dll");
    snprintf(out, size, "%s\\%s%s", g_mp2_game_root, in_driver ? "e2driver\\" : "", m->name);
    return 1;
}

static void s_GetModuleFileNameA(void) {
    char path[MAX_PATH];
    char* buffer = (char*)(uintptr_t)ARG(1);
    uint32_t size = ARG(2);
    if (!guest_module_path(ARG(0), path, sizeof path)) {
        RET(GetModuleFileNameA((HMODULE)(uintptr_t)ARG(0), buffer, size), 3);
        return;
    }
    strncpy(buffer, path, size);
    if (size) buffer[size - 1] = 0;
    RET(strlen(buffer), 3);
}

static void s_GetModuleFileNameW(void) {
    char path[MAX_PATH];
    wchar_t* buffer = (wchar_t*)(uintptr_t)ARG(1);
    uint32_t size = ARG(2);
    if (!guest_module_path(ARG(0), path, sizeof path)) {
        RET(GetModuleFileNameW((HMODULE)(uintptr_t)ARG(0), buffer, size), 3);
        return;
    }
    int n = MultiByteToWideChar(CP_ACP, 0, path, -1, buffer, size);
    RET(n ? n - 1 : 0, 3);
}

/* Las DLL dinámicas (driver gráfico, Bink) corren su DllMain al primer LoadLibrary. */
static unsigned char g_dyn_loaded[64];

static uint32_t load_guest(const mp2_module_info_t* m) {
    int index = 0;
    while (mp2_module_at(index) != m) index++;
    if (m->is_static || g_dyn_loaded[index]) return m->base;
    g_dyn_loaded[index] = 1;
    uint32_t saved_esp = g_esp;
    /* Sus propias dependencias dinámicas, antes (Bink para el driver). */
    if (!_stricmp(m->name, "e2_d3d8_driver_mfc.dll")) {
        const mp2_module_info_t* bink = find_module("binkw32.dll");
        if (bink) load_guest(bink);
    }
    int ok = mp2_run_dllmain(m->name, m->base, m->entry);
    g_esp = saved_esp;
    return ok ? m->base : 0;
}

static void s_LoadLibraryA(void) {
    const char* name = (const char*)(uintptr_t)ARG(0);
    const mp2_module_info_t* m = find_module(name);
    fprintf(stderr, "[MP2:LOAD] LoadLibraryA(\"%s\") %s\n", name, m ? "-> invitado" : "-> Windows");
    if (m) { uint32_t handle = load_guest(m); RET(handle, 1); return; }
    if (!_stricmp(name, "d3d8.dll") || !_stricmp(name, "d3d9.dll")) {
        if (!g_mp2_allow_window) {
            mp2_stop("FRONTERA", "el motor pide Direct3D: primer punto gráfico alcanzado", MP2_EXIT_BOUNDARY);
            return;
        }
        /* Siempre el del sistema: el d3d9.dll de la carpeta del juego es un cargador ASI. */
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat(path, "\\");
        strcat(path, name);
        RET(LoadLibraryA(path), 1);
        return;
    }
    RET(LoadLibraryA(name), 1);
}

static void s_LoadLibraryW(void) {
    char name[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, (const wchar_t*)(uintptr_t)ARG(0), -1, name, MAX_PATH, NULL, NULL);
    const mp2_module_info_t* m = find_module(name);
    if (m) { uint32_t handle = load_guest(m); RET(handle, 1); return; }
    RET(LoadLibraryW((const wchar_t*)(uintptr_t)ARG(0)), 1);
}

static void s_LoadLibraryExA(void) {
    const char* name = (const char*)(uintptr_t)ARG(0);
    const mp2_module_info_t* m = find_module(name);
    if (m) { uint32_t handle = load_guest(m); RET(handle, 3); return; }
    RET(LoadLibraryExA(name, (HANDLE)(uintptr_t)ARG(1), ARG(2)), 3);
}

static void s_FreeLibrary(void) {
    if (module_by_base(ARG(0))) { RET(1, 1); return; }
    RET(FreeLibrary((HMODULE)(uintptr_t)ARG(0)), 1);
}

static void s_GetProcAddress(void) {
    uint32_t handle = ARG(0);
    const char* name = (const char*)(uintptr_t)ARG(1);
    if (module_by_base(handle)) { RET(native32_export(handle, name), 2); return; }
    /* MFC obtiene RegCreateKeyExA y compañía así. Sin esto se saltan el registro
     * virtual y escriben en el del usuario: ocurrió, ver docs/BLOCKERS.md. */
    if ((uintptr_t)name >= 0x10000) {
        uint32_t shim = native32_shim_va(name);
        if (shim) { RET(shim, 2); return; }
    }
    uint32_t va = (uint32_t)(uintptr_t)GetProcAddress((HMODULE)(uintptr_t)handle, name);
    if (va && (uintptr_t)name >= 0x10000) {
        native32_add_name(va, "native", name);
        if (!strcmp(name, "Direct3DCreate8")) g_mp2_create8_va = va;
    }
    RET(va, 2);
}

static void s_DisableThreadLibraryCalls(void) { RET(1, 1); }

/* ---- escritorio: fronteras de esta fase ---- */
static void stop_window(const char* api, const char* detail) {
    char text[512];
    snprintf(text, sizeof text, "%s %s", api, detail ? detail : "");
    mp2_stop("FRONTERA", text, MP2_EXIT_BOUNDARY);
}

static const char* atom_or_text(uint32_t value) {
    return value < 0x10000 ? "(átomo)" : (const char*)(uintptr_t)value;
}

/* La ventana del juego, normal y sin robar nada. La pantalla completa de verdad
 * la pide el juego a D3D, y el host la fuerza a ventana (mp2_d3d8_trace.c). */
#define TOP_LEVEL(style, parent) (!((style) & WS_CHILD) && !(parent))
static void s_CreateWindowExA(void) {
    if (!g_mp2_allow_window) { stop_window("CreateWindowExA", atom_or_text(ARG(1))); return; }
    uint32_t ex = ARG(0), style = ARG(3), parent = ARG(8);
    int x = (int)ARG(4), y = (int)ARG(5), w = (int)ARG(6), h = (int)ARG(7);
    fprintf(stderr, "[MP2:WIN] CreateWindowExA clase=%s título=%s estilo=%08X ex=%08X %d,%d %dx%d padre=%08X\n",
            atom_or_text(ARG(1)), ARG(2) ? (const char*)(uintptr_t)ARG(2) : "", style, ex, x, y, w, h, parent);
    if (TOP_LEVEL(style, parent) && (style & WS_VISIBLE || style & WS_POPUP || w > 0)) {
        ex &= ~(uint32_t)WS_EX_TOPMOST;
        style &= ~(uint32_t)(WS_MAXIMIZE | WS_POPUP);
        style |= WS_OVERLAPPEDWINDOW;
        RECT r = { 0, 0, MP2_WINDOW_W, MP2_WINDOW_H };
        AdjustWindowRectEx(&r, style, FALSE, ex);
        x = CW_USEDEFAULT; y = CW_USEDEFAULT;
        w = r.right - r.left; h = r.bottom - r.top;
    }
    HWND window = CreateWindowExA(ex, (const char*)(uintptr_t)ARG(1), (const char*)(uintptr_t)ARG(2), style, x, y, w, h,
                                  (HWND)(uintptr_t)parent, (HMENU)(uintptr_t)ARG(9), (HINSTANCE)(uintptr_t)ARG(10),
                                  (void*)(uintptr_t)ARG(11));
    fprintf(stderr, "[MP2:WIN] -> %p (error %lu)\n", window, window ? 0 : GetLastError());
    RET(window, 12);
}
static void s_CreateWindowExW(void) { stop_window("CreateWindowExW", NULL); }
static void s_ShowWindow(void) {
    int show = (int)ARG(1);
    if (!g_mp2_allow_window) { stop_window("ShowWindow", NULL); return; }
    char window_class[16];
    if (GetClassNameA((HWND)(uintptr_t)ARG(0), window_class, sizeof window_class) && !strcmp(window_class, "#32770")) {
        RET(ShowWindow((HWND)(uintptr_t)ARG(0), SW_HIDE), 2);   /* los diálogos no se muestran */
        return;
    }
    /* Maximizar y activar quedan fuera: la ventana aparece sin quitarte el foco. */
    if (show == SW_SHOWMAXIMIZED || show == SW_SHOW || show == SW_SHOWNORMAL || show == SW_RESTORE) show = SW_SHOWNOACTIVATE;
    /* Sin foco, el juego se pausa en su bucle principal. Se le avisa de que está
     * activo; si luego la ventana pierde el foco de verdad, se pausa como siempre. */
    if (show == SW_SHOWNOACTIVATE) {
        PostMessageA((HWND)(uintptr_t)ARG(0), WM_ACTIVATEAPP, TRUE, 0);
        PostMessageA((HWND)(uintptr_t)ARG(0), WM_ACTIVATE, WA_ACTIVE, 0);
    }
    RET(ShowWindow((HWND)(uintptr_t)ARG(0), show), 2);
}
static void s_SetForegroundWindow(void) {
    fprintf(stderr, "[MP2:POLICY] SetForegroundWindow ignorado\n");
    RET(1, 1);
}
static void s_SetWindowPos(void) {
    uint32_t after = ARG(1), flags = ARG(6);
    /* Ni encima de las demás ni activándose. */
    if (after == (uint32_t)(uintptr_t)HWND_TOPMOST || after == (uint32_t)(uintptr_t)HWND_TOP) flags |= SWP_NOZORDER;
    flags |= SWP_NOACTIVATE;
    RET(SetWindowPos((HWND)(uintptr_t)ARG(0), (HWND)(uintptr_t)after, (int)ARG(2), (int)ARG(3), (int)ARG(4), (int)ARG(5), flags), 7);
}
/* Consultas sí; cambiar ajustes del sistema (teclas especiales, salvapantallas) no. */
static void s_SystemParametersInfoA(void) {
    uint32_t action = ARG(0);
    switch (action) {
    case SPI_GETSTICKYKEYS: case SPI_GETTOGGLEKEYS: case SPI_GETFILTERKEYS: case SPI_GETSCREENSAVEACTIVE: case SPI_GETWORKAREA:
        RET(SystemParametersInfoA(action, ARG(1), (void*)(uintptr_t)ARG(2), ARG(3)), 4);
        return;
    }
    fprintf(stderr, "[MP2:POLICY] SystemParametersInfoA(%X) ignorado\n", action);
    RET(1, 4);
}
static void s_DialogBoxParamA(void) { stop_window("DialogBoxParamA", NULL); }
static void s_DialogBoxIndirectParamA(void) { stop_window("DialogBoxIndirectParamA", NULL); }
static void s_CreateDialogParamA(void) { stop_window("CreateDialogParamA", NULL); }
/* Título de un DLGTEMPLATE o DLGTEMPLATEEX. */
static void dialog_title(const uint8_t* t, char* out, size_t size) {
    int ex = ((const uint16_t*)t)[1] == 0xFFFF;
    const uint16_t* w = (const uint16_t*)(t + (ex ? 26 : 18));
    for (int field = 0; field < 2; field++) {   /* menú y clase */
        if (*w == 0xFFFF) w += 2;
        else { while (*w) w++; w++; }
    }
    WideCharToMultiByte(CP_ACP, 0, (const wchar_t*)w, -1, out, (int)size, NULL, NULL);
}

/* El diálogo de inicio enumera los modos y guarda los ajustes: corre el código
 * original, oculto, y se pulsa Play (IDOK). Cualquier otro diálogo, como un error
 * o un aviso de memoria, se registra y detiene el proceso. */
static void s_CreateDialogIndirectParamA(void) {
    char title[128];
    dialog_title((const uint8_t*)(uintptr_t)ARG(1), title, sizeof title);
    if (!g_mp2_allow_window || strcmp(title, "Max Payne 2")) { stop_window("CreateDialogIndirectParamA", title); return; }
    fprintf(stderr, "[MP2:WIN] diálogo de inicio: oculto, se pulsa Play\n");
    HWND dialog = CreateDialogIndirectParamA((HINSTANCE)(uintptr_t)ARG(0), (LPCDLGTEMPLATEA)(uintptr_t)ARG(1),
                                             (HWND)(uintptr_t)ARG(2), (DLGPROC)(uintptr_t)ARG(3), (LPARAM)ARG(4));
    fprintf(stderr, "[MP2:WIN] -> %p (error %lu)\n", dialog, dialog ? 0 : GetLastError());
    if (dialog) {
        ShowWindow(dialog, SW_HIDE);
        PostMessageA(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    }
    RET(dialog, 5);
}
/* Nunca se cambia la resolución del escritorio. */
static void s_ChangeDisplaySettingsA(void) {
    fprintf(stderr, "[MP2:POLICY] ChangeDisplaySettingsA ignorado\n");
    RET(DISP_CHANGE_SUCCESSFUL, 2);
}
static void s_ChangeDisplaySettingsExA(void) {
    fprintf(stderr, "[MP2:POLICY] ChangeDisplaySettingsExA ignorado\n");
    RET(DISP_CHANGE_SUCCESSFUL, 5);
}
#define VIRTUAL_HOOK 0x4D500001u
/* MFC y X_Input enganchan solo su propio hilo. Un gancho global tocaría al resto
 * del escritorio. */
static void s_SetWindowsHookExA(void) {
    uint32_t id = ARG(0), thread = ARG(3);
    fprintf(stderr, "[MP2:WIN] SetWindowsHookExA tipo=%d hilo=%u\n", (int)id, thread);
    if (id == WH_KEYBOARD_LL && !thread) {
        /* MaxPayne2.exe lo pone para anular teclas del sistema: no llega a Windows. */
        fprintf(stderr, "[MP2:POLICY] gancho global de teclado no instalado\n");
        RET(VIRTUAL_HOOK, 4);
        return;
    }
    if (!thread) { stop_window("SetWindowsHookExA global", NULL); return; }
    RET(SetWindowsHookExA((int)id, (HOOKPROC)(uintptr_t)ARG(1), NULL, thread), 4);
}
static void s_UnhookWindowsHookEx(void) {
    if (ARG(0) == VIRTUAL_HOOK) { RET(1, 1); return; }
    RET(UnhookWindowsHookEx((HHOOK)(uintptr_t)ARG(0)), 1);
}
static void s_ClipCursor(void) {
    fprintf(stderr, "[MP2:POLICY] ClipCursor ignorado\n");
    RET(1, 1);
}
static void s_SetCursorPos(void) { RET(1, 2); }
static void s_ShellExecuteExA(void) { stop_window("ShellExecuteExA", NULL); }
static void s_MessageBoxA(void) {
    char text[512];
    snprintf(text, sizeof text, "\"%s\": %s", ARG(2) ? (const char*)(uintptr_t)ARG(2) : "",
             ARG(1) ? (const char*)(uintptr_t)ARG(1) : "");
    stop_window("MessageBoxA", text);
}
static void s_MessageBoxW(void) { stop_window("MessageBoxW", NULL); }

/* ---- sistema: prioridades y registro ---- */
static void s_SetPriorityClass(void) {
    fprintf(stderr, "[MP2:POLICY] SetPriorityClass(%08X) ignorado\n", ARG(1));
    RET(1, 2);
}
static void s_SetThreadPriority(void) {
    fprintf(stderr, "[MP2:POLICY] SetThreadPriority(%d) ignorado\n", (int)ARG(1));
    RET(1, 2);
}

/* ---- sistema: archivos ---- */
/* Toda escritura termina en el sandbox, con el nombre base del archivo: la
 * instalación y Mis documentos quedan intactos. */
static const char* sandbox_path(const char* path, char* out, size_t size) {
    const char* file = path;
    for (const char* p = path; *p; p++)
        if (*p == '\\' || *p == '/') file = p + 1;
    snprintf(out, size, "%s\\%s", g_mp2_sandbox, file);
    return out;
}

static void s_CreateFileA(void) {
    const char* name = (const char*)(uintptr_t)ARG(0);
    uint32_t access = ARG(1), disposition = ARG(4);
    char redirected[MAX_PATH];
    if ((access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) || disposition != OPEN_EXISTING) {
        name = sandbox_path(name, redirected, sizeof redirected);
        fprintf(stderr, "[MP2:FILE] escritura redirigida a %s\n", name);
    }
    HANDLE h = CreateFileA(name, access, ARG(2), (LPSECURITY_ATTRIBUTES)(uintptr_t)ARG(3), disposition, ARG(5),
                           (HANDLE)(uintptr_t)ARG(6));
    RET(h, 7);
}

static void s_CreateFileW(void) {
    const wchar_t* wname = (const wchar_t*)(uintptr_t)ARG(0);
    uint32_t access = ARG(1), disposition = ARG(4);
    wchar_t redirected[MAX_PATH];
    if ((access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) || disposition != OPEN_EXISTING) {
        char name[MAX_PATH], path[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, wname, -1, name, MAX_PATH, NULL, NULL);
        sandbox_path(name, path, sizeof path);
        MultiByteToWideChar(CP_ACP, 0, path, -1, redirected, MAX_PATH);
        fprintf(stderr, "[MP2:FILE] escritura redirigida a %s\n", path);
        wname = redirected;
    }
    HANDLE h = CreateFileW(wname, access, ARG(2), (LPSECURITY_ATTRIBUTES)(uintptr_t)ARG(3), disposition, ARG(5),
                           (HANDLE)(uintptr_t)ARG(6));
    RET(h, 7);
}

static void s_CreateDirectoryA(void) {
    fprintf(stderr, "[MP2:FILE] CreateDirectoryA(%s) no se crea fuera del sandbox\n", (const char*)(uintptr_t)ARG(0));
    SetLastError(ERROR_ALREADY_EXISTS);
    RET(0, 2);
}

static void s_DeleteFileA(void) {
    char path[MAX_PATH];
    RET(DeleteFileA(sandbox_path((const char*)(uintptr_t)ARG(0), path, sizeof path)), 1);
}

static void s_CopyFileA(void) {
    char path[MAX_PATH];
    RET(CopyFileA((const char*)(uintptr_t)ARG(0), sandbox_path((const char*)(uintptr_t)ARG(1), path, sizeof path), ARG(2)), 3);
}

/* ---- fin del proceso y excepciones ---- */
static void s_ExitProcess(void) {
    char text[64];
    snprintf(text, sizeof text, "ExitProcess(%u)", ARG(0));
    mp2_stop("SALIDA", text, MP2_EXIT_GAME);
}

native32_shim_t g_mp2_shims[] = {
    { "GetModuleHandleA", s_GetModuleHandleA }, { "GetModuleHandleW", s_GetModuleHandleW },
    { "GetModuleFileNameA", s_GetModuleFileNameA }, { "GetModuleFileNameW", s_GetModuleFileNameW },
    { "LoadLibraryA", s_LoadLibraryA }, { "LoadLibraryW", s_LoadLibraryW }, { "LoadLibraryExA", s_LoadLibraryExA },
    { "FreeLibrary", s_FreeLibrary }, { "GetProcAddress", s_GetProcAddress },
    { "DisableThreadLibraryCalls", s_DisableThreadLibraryCalls },
    { "CreateWindowExA", s_CreateWindowExA }, { "CreateWindowExW", s_CreateWindowExW },
    { "DialogBoxParamA", s_DialogBoxParamA }, { "DialogBoxIndirectParamA", s_DialogBoxIndirectParamA },
    { "CreateDialogParamA", s_CreateDialogParamA }, { "CreateDialogIndirectParamA", s_CreateDialogIndirectParamA },
    { "ChangeDisplaySettingsA", s_ChangeDisplaySettingsA }, { "ChangeDisplaySettingsExA", s_ChangeDisplaySettingsExA },
    { "SetWindowsHookExA", s_SetWindowsHookExA }, { "UnhookWindowsHookEx", s_UnhookWindowsHookEx },{ "ClipCursor", s_ClipCursor }, { "SetCursorPos", s_SetCursorPos },
    { "ShellExecuteExA", s_ShellExecuteExA }, { "MessageBoxA", s_MessageBoxA }, { "MessageBoxW", s_MessageBoxW },
    { "SetPriorityClass", s_SetPriorityClass }, { "SetThreadPriority", s_SetThreadPriority },
    { "RegOpenKeyA", mp2_RegOpenKeyA }, { "RegOpenKeyExA", mp2_RegOpenKeyExA }, { "RegCreateKeyA", mp2_RegCreateKeyA },
    { "RegCreateKeyExA", mp2_RegCreateKeyExA }, { "RegCloseKey", mp2_RegCloseKey }, { "RegQueryValueExA", mp2_RegQueryValueExA },
    { "RegQueryValueA", mp2_RegQueryValueA }, { "RegSetValueExA", mp2_RegSetValueExA }, { "RegSetValueA", mp2_RegSetValueA },
    { "RegDeleteKeyA", mp2_RegDeleteKeyA }, { "RegDeleteValueA", mp2_RegDeleteValueA }, { "RegEnumKeyA", mp2_RegEnumKeyA },
    { "GetCommandLineA", s_GetCommandLineA }, { "GetCommandLineW", s_GetCommandLineW },
    { "ShowWindow", s_ShowWindow }, { "SetForegroundWindow", s_SetForegroundWindow },
    { "SystemParametersInfoA", s_SystemParametersInfoA }, { "SetWindowPos", s_SetWindowPos },
    { "CreateFileA", s_CreateFileA }, { "CreateFileW", s_CreateFileW }, { "CreateDirectoryA", s_CreateDirectoryA },
    { "DeleteFileA", s_DeleteFileA }, { "CopyFileA", s_CopyFileA },
    { "ExitProcess", s_ExitProcess }, { "RaiseException", mp2_seh_RaiseException }, { "RtlUnwind", mp2_seh_RtlUnwind },
};
int g_mp2_nshims = sizeof g_mp2_shims / sizeof g_mp2_shims[0];
