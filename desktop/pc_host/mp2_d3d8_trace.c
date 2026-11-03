/*
 * mp2_d3d8_trace.c - qué parte de Direct3D 8 usa Max Payne 2, medido en ejecución.
 * Mismo enfoque que d3d9_trace.c de NFSU2: native32 ve toda llamada a COM, y las
 * ranuras de las vtables reciben nombre cuando se devuelven los objetos.
 *
 * Aquí se aplica además la política de escritorio sobre COM: el dispositivo
 * siempre en ventana y DirectInput sin exclusividad, para no capturar el ratón.
 * El informe <sandbox>/d3d8_usage.txt es la especificación del renderer de Android.
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "native32.h"
#include "mp2_host.h"

#define ND3D 16
#define NDEV 97
static const char* k_d3d[ND3D] = {
    "QueryInterface", "AddRef", "Release", "RegisterSoftwareDevice", "GetAdapterCount", "GetAdapterIdentifier",
    "GetAdapterModeCount", "EnumAdapterModes", "GetAdapterDisplayMode", "CheckDeviceType", "CheckDeviceFormat",
    "CheckDeviceMultiSampleType", "CheckDepthStencilMatch", "GetDeviceCaps", "GetAdapterMonitor", "CreateDevice" };
static const char* k_dev[NDEV] = {
    "QueryInterface", "AddRef", "Release", "TestCooperativeLevel", "GetAvailableTextureMem", "ResourceManagerDiscardBytes",
    "GetDirect3D", "GetDeviceCaps", "GetDisplayMode", "GetCreationParameters", "SetCursorProperties", "SetCursorPosition",
    "ShowCursor", "CreateAdditionalSwapChain", "Reset", "Present", "GetBackBuffer", "GetRasterStatus", "SetGammaRamp",
    "GetGammaRamp", "CreateTexture", "CreateVolumeTexture", "CreateCubeTexture", "CreateVertexBuffer", "CreateIndexBuffer",
    "CreateRenderTarget", "CreateDepthStencilSurface", "CreateImageSurface", "CopyRects", "UpdateTexture", "GetFrontBuffer",
    "SetRenderTarget", "GetRenderTarget", "GetDepthStencilSurface", "BeginScene", "EndScene", "Clear", "SetTransform",
    "GetTransform", "MultiplyTransform", "SetViewport", "GetViewport", "SetMaterial", "GetMaterial", "SetLight", "GetLight",
    "LightEnable", "GetLightEnable", "SetClipPlane", "GetClipPlane", "SetRenderState", "GetRenderState", "BeginStateBlock",
    "EndStateBlock", "ApplyStateBlock", "CaptureStateBlock", "DeleteStateBlock", "CreateStateBlock", "SetClipStatus",
    "GetClipStatus", "GetTexture", "SetTexture", "GetTextureStageState", "SetTextureStageState", "ValidateDevice", "GetInfo",
    "SetPaletteEntries", "GetPaletteEntries", "SetCurrentTexturePalette", "GetCurrentTexturePalette", "DrawPrimitive",
    "DrawIndexedPrimitive", "DrawPrimitiveUP", "DrawIndexedPrimitiveUP", "ProcessVertices", "CreateVertexShader",
    "SetVertexShader", "GetVertexShader", "DeleteVertexShader", "SetVertexShaderConstant", "GetVertexShaderConstant",
    "GetVertexShaderDeclaration", "GetVertexShaderFunction", "SetStreamSource", "GetStreamSource", "SetIndices", "GetIndices",
    "CreatePixelShader", "SetPixelShader", "GetPixelShader", "DeletePixelShader", "SetPixelShaderConstant",
    "GetPixelShaderConstant", "GetPixelShaderFunction", "DrawRectPatch", "DrawTriPatch", "DeletePatch" };

enum { D3D_CREATEDEVICE = 15 };
enum { DEV_RESET = 14, DEV_PRESENT = 15, DEV_CREATETEXTURE = 20, DEV_CREATECUBE = 22, DEV_SETRS = 50, DEV_SETTSS = 63, DEV_CREATEVS = 75, DEV_CREATEPS = 87 };
#define DI_CREATEDEVICE 3
#define DID_GETDEVICESTATE 9
#define DID_GETDEVICEDATA 10
#define DID_SETDATAFORMAT 11
#define DID_SETCOOPERATIVE 13
#define DISCL_NONEXCLUSIVE_ 2u
#define DISCL_FOREGROUND_ 4u
#define DISCL_BACKGROUND_ 8u

uint32_t g_mp2_create8_va;       /* Direct3DCreate8, anotado por el shim de GetProcAddress */
uint32_t g_mp2_stop_after_frames;
static uint32_t g_d3d_vt[ND3D], g_dev_vt[NDEV];
static uint32_t g_di_createdevice, g_did_setcoop, g_did_state, g_did_data, g_did_format;
static uint64_t g_state_reads, g_data_reads;
static uint64_t g_count[NDEV];
static uint32_t g_frames;
uint32_t g_capture_every = 150;
static char g_params[300];

typedef struct { uint32_t v[128], n[128]; int len; } vset_t;
static vset_t s_rs, s_tss, s_texfmt, s_cubefmt;
static uint32_t s_vs, s_ps;

static void vs_add(vset_t* s, uint32_t v) {
    for (int i = 0; i < s->len; i++)
        if (s->v[i] == v) { s->n[i]++; return; }
    if (s->len < 128) { s->v[s->len] = v; s->n[s->len++] = 1; }
}

static void dump_set(FILE* f, const char* title, const vset_t* s) {
    fprintf(f, "%s (%d distintos):", title, s->len);
    for (int i = 0; i < s->len; i++) fprintf(f, " %u(x%u)", s->v[i], s->n[i]);
    fputc('\n', f);
}

void mp2_d3d8_write_report(void) {
    if (!g_dev_vt[0]) return;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\d3d8_usage.txt", g_mp2_sandbox);
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "Direct3D 8 de Max Payne 2 (medido), %u frames presentados\n\nCreateDevice: %s\n\nMétodos:\n", g_frames, g_params);
    for (int i = 0; i < NDEV; i++)
        if (g_count[i]) fprintf(f, "  %-28s %llu\n", k_dev[i], g_count[i]);
    fprintf(f, "\nSetRenderState y texturas:\n");
    dump_set(f, "SetRenderState D3DRENDERSTATETYPE", &s_rs);
    dump_set(f, "SetTextureStageState tipo", &s_tss);
    dump_set(f, "CreateTexture D3DFORMAT", &s_texfmt);
    dump_set(f, "CreateCubeTexture D3DFORMAT", &s_cubefmt);
    fprintf(f, "Vertex shaders creados: %u\nPixel shaders creados: %u\n", s_vs, s_ps);
    fprintf(f, "DirectInput: GetDeviceState %llu, GetDeviceData %llu\n", g_state_reads, g_data_reads);
    fclose(f);
}

/* Evidencia del frame: el back buffer real, justo antes de Present. PrintWindow
 * devuelve negro con ventanas D3D, así que la captura no pasa por la ventana. */
enum { DEV_GETBACKBUFFER = 16, DEV_CREATEIMAGESURFACE = 27, DEV_COPYRECTS = 28 };
enum { SURF_RELEASE = 2, SURF_GETDESC = 8, SURF_LOCKRECT = 9, SURF_UNLOCKRECT = 10 };
typedef HRESULT(__stdcall* getbb_t)(void*, UINT, UINT, void**);
typedef HRESULT(__stdcall* createimg_t)(void*, UINT, UINT, UINT, void**);
typedef HRESULT(__stdcall* copyrects_t)(void*, void*, void*, UINT, void*, void*);
typedef ULONG(__stdcall* release_t)(void*);
typedef HRESULT(__stdcall* desc_t)(void*, uint32_t*);
typedef struct { INT pitch; void* bits; } locked_t;
typedef HRESULT(__stdcall* lock_t)(void*, locked_t*, const RECT*, DWORD);
typedef HRESULT(__stdcall* unlock_t)(void*);
#define VT(obj, slot) ((*(void***)(obj))[slot])

static void save_back_buffer(void* device, const char* name) {
    void* back = NULL, *copy = NULL;
    uint32_t desc[8];
    if (((getbb_t)VT(device, DEV_GETBACKBUFFER))(device, 0, 0, &back) < 0 || !back) return;
    ((desc_t)VT(back, SURF_GETDESC))(back, desc);   /* Format, Type, Usage, Pool, Size, MSType, Width, Height */
    uint32_t format = desc[0], w = desc[6], h = desc[7];
    HRESULT made = ((createimg_t)VT(device, DEV_CREATEIMAGESURFACE))(device, w, h, format, &copy);
    if (made >= 0 && ((copyrects_t)VT(device, DEV_COPYRECTS))(device, back, NULL, 0, copy, NULL) >= 0) {
        locked_t lock;
        if (((lock_t)VT(copy, SURF_LOCKRECT))(copy, &lock, NULL, 0x10 /* READONLY */) >= 0) {
            char path[MAX_PATH];
            uint64_t sum = 0;
            uint32_t distinct_probe = 0, first = *(uint32_t*)lock.bits;
            for (uint32_t y = 0; y < h; y++) {
                const uint32_t* row = (const uint32_t*)((const uint8_t*)lock.bits + (size_t)y * lock.pitch);
                for (uint32_t x = 0; x < w; x++) {
                    sum += (row[x] & 0xFF) + ((row[x] >> 8) & 0xFF) + ((row[x] >> 16) & 0xFF);
                    distinct_probe |= (row[x] ^ first) & 0xFFFFFF;
                }
            }
            snprintf(path, sizeof path, "%s\\%s", g_mp2_sandbox, name);
            FILE* f = fopen(path, "wb");
            if (f) {
                BITMAPFILEHEADER fh;
                BITMAPINFOHEADER bi;
                memset(&fh, 0, sizeof fh);
                memset(&bi, 0, sizeof bi);
                bi.biSize = sizeof bi;
                bi.biWidth = (LONG)w;
                bi.biHeight = -(LONG)h;
                bi.biPlanes = 1;
                bi.biBitCount = 32;
                fh.bfType = 0x4D42;
                fh.bfOffBits = sizeof fh + sizeof bi;
                fh.bfSize = fh.bfOffBits + w * h * 4;
                fwrite(&fh, sizeof fh, 1, f);
                fwrite(&bi, sizeof bi, 1, f);
                for (uint32_t y = 0; y < h; y++) fwrite((const uint8_t*)lock.bits + (size_t)y * lock.pitch, 4, w, f);
                fclose(f);
            }
            fprintf(stderr, "[MP2:D3D] back buffer %s %ux%u fmt=%u brillo medio=%.1f %s t=%lu ms\n", name, w, h, format,
                    (double)sum / (3.0 * w * h), distinct_probe ? "con contenido" : "color uniforme", GetTickCount());
            ((unlock_t)VT(copy, SURF_UNLOCKRECT))(copy);
        }
    }
    if (copy) ((release_t)VT(copy, SURF_RELEASE))(copy);
    ((release_t)VT(back, SURF_RELEASE))(back);
}

static void force_windowed(uint32_t* pp, const char* api) {
    /* D3DPRESENT_PARAMETERS de D3D8. */
    fprintf(stderr, "[MP2:D3D] %s pedido: %ux%u fmt=%u buffers=%u msaa=%u swap=%u ventana=%u profundidad=%u/%u flags=%X hz=%u intervalo=%X\n",
            api, pp[0], pp[1], pp[2], pp[3], pp[4], pp[5], pp[7], pp[8], pp[9], pp[10], pp[11], pp[12]);
    pp[7] = 1;       /* Windowed */
    pp[11] = 0;      /* FullScreen_RefreshRateInHz: debe ser 0 en ventana */
    pp[12] = 0;      /* FullScreen_PresentationInterval: D3DPRESENT_INTERVAL_DEFAULT */
    snprintf(g_params, sizeof g_params, "%ux%u fmt=%u buffers=%u msaa=%u swap=%u profundidad=%u/%u flags=%X",
             pp[0], pp[1], pp[2], pp[3], pp[4], pp[5], pp[8], pp[9], pp[10]);
}

/* ---- Entrada sintética ----
 * El guion sustituye lo que devuelve DirectInput: X_Input recibe un teclado y un
 * ratón que no existen, y tu ratón y tu teclado reales no llegan al juego. Es la
 * misma costura que usará PcActionMapper en Android.
 *   <frame> m <dx> <dy> <botones>     botones: máscara, 1 izquierdo, 2 derecho
 *   <frame> k <DIK> <0|1>             tecla, por su código DirectInput
 */
#define MAX_EVENTS 1024
typedef struct { uint32_t frame; char kind; int a, b, c; } input_event_t;
static input_event_t g_events[MAX_EVENTS];
static int g_nevents, g_next_event;
static uint8_t g_keys[256];
static int g_mouse_dx, g_mouse_dy, g_mouse_buttons, g_prev_buttons;
static int g_scripted;
typedef struct { uint32_t device; int size; } device_kind_t;
static device_kind_t g_devices[8];
static int g_ndevices;
void mp2_input_script_load(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "[MP2:INPUT] no se pudo abrir %s\n", path); return; }
    char line[128];
    while (fgets(line, sizeof line, f) && g_nevents < MAX_EVENTS) {
        input_event_t e = { 0 };
        if (line[0] == '#') continue;
        int n = sscanf(line, "%u %c %d %d %d", &e.frame, &e.kind, &e.a, &e.b, &e.c);
        if (n >= 4) g_events[g_nevents++] = e;
    }
    fclose(f);
    g_scripted = 1;
    fprintf(stderr, "[MP2:INPUT] guion con %d eventos\n", g_nevents);
}

static void input_advance(uint32_t frame) {
    while (g_next_event < g_nevents && g_events[g_next_event].frame <= frame) {
        const input_event_t* e = &g_events[g_next_event++];
        if (e->kind == 'm') { g_mouse_dx += e->a; g_mouse_dy += e->b; g_mouse_buttons = e->c; }
        else if (e->kind == 'k' && e->a >= 0 && e->a < 256) g_keys[e->a] = e->b ? 0x80 : 0;
    }
}

static int device_size(uint32_t device) {
    for (int i = 0; i < g_ndevices; i++)
        if (g_devices[i].device == device) return g_devices[i].size;
    return 0;
}

static void fill_state(uint32_t device, uint32_t size, uint8_t* out) {
    (void)device;
    memset(out, 0, size);
    if (size == 256) { memcpy(out, g_keys, 256); return; }
    if (size == 16 || size == 20) {   /* DIMOUSESTATE: lX, lY, lZ, botones */
        ((int*)out)[0] = g_mouse_dx;
        ((int*)out)[1] = g_mouse_dy;
        for (int b = 0; b < 4; b++) out[12 + b] = (g_mouse_buttons >> b) & 1 ? 0x80 : 0;
        g_mouse_dx = g_mouse_dy = 0;
    }
}

/* DIDEVICEOBJECTDATA: desplazamiento, dato, marca de tiempo, secuencia. */
static uint32_t g_data_capacity;

static void fill_buffered(uint32_t device, uint32_t item, uint8_t* out, uint32_t* count) {
    /* DirectInput ya sobrescribió *count: la capacidad se anotó antes de la llamada. */
    uint32_t capacity = g_data_capacity, n = 0;
    *count = 0;
    if (!out || item < 16) return;
#define PUT(ofs, data) do { if (n < capacity) { uint32_t* r = (uint32_t*)(out + n * item); r[0] = (ofs); r[1] = (uint32_t)(data); r[2] = GetTickCount(); r[3] = n; n++; } } while (0)
    int size = device_size(device);
    if (size == 16 || size == 20) {
        if (g_mouse_dx) PUT(0, g_mouse_dx);
        if (g_mouse_dy) PUT(4, g_mouse_dy);
        for (int b = 0; b < 4; b++)
            if (((g_mouse_buttons ^ g_prev_buttons) >> b) & 1) PUT(12 + b, (g_mouse_buttons >> b) & 1 ? 0x80 : 0);
        if (g_mouse_buttons != g_prev_buttons)
            fprintf(stderr, "[MP2:INPUT] frame %u botones %X -> %X (capacidad %u, escritos %u, tamaño %u)\n", g_frames, g_prev_buttons, g_mouse_buttons, capacity, n, item);
        g_prev_buttons = g_mouse_buttons;
        g_mouse_dx = g_mouse_dy = 0;
    } else if (size == 256) {
        static uint8_t reported[256];
        for (int k = 0; k < 256; k++)
            if (g_keys[k] != reported[k]) { PUT(k, g_keys[k]); reported[k] = g_keys[k]; }
    }
#undef PUT
    *count = n;
}

/* Límite de development.toml: el juego no presenta más de 45 frames por segundo.
 * Mientras espera suelta la máquina, para que corran el resto de hilos invitados. */
static void pace_frame(void) {
    static LARGE_INTEGER frequency, last;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (last.QuadPart) {
        double elapsed = (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)frequency.QuadPart;
        if (elapsed < 1000.0 / 45.0) {
            mach_leave();
            Sleep((DWORD)(1000.0 / 45.0 - elapsed));
            mach_enter();
        }
    }
    QueryPerformanceCounter(&last);
}

static void pre_hook(uint32_t target, uint32_t* args) {
    if (g_did_setcoop && target == g_did_setcoop) {
        uint32_t flags = g_scripted ? DISCL_NONEXCLUSIVE_ | DISCL_BACKGROUND_ : DISCL_NONEXCLUSIVE_ | DISCL_FOREGROUND_;
        fprintf(stderr, "[MP2:POLICY] SetCooperativeLevel %X -> %X (sin exclusividad)\n", args[2], flags);
        args[2] = flags;
        /* X_Input nace después del aviso de activación de ShowWindow: se repite, o
         * X_Input::update no consulta nunca los dispositivos. */
        PostMessageA((HWND)(uintptr_t)args[1], WM_ACTIVATEAPP, FALSE, 0);
        PostMessageA((HWND)(uintptr_t)args[1], WM_ACTIVATEAPP, TRUE, 0);
        return;
    }
    if (g_did_data && target == g_did_data) {
        if (args[3]) g_data_capacity = *(uint32_t*)(uintptr_t)args[3];
        return;
    }
    if (g_did_format && target == g_did_format && g_ndevices < 8) {
        /* DIDATAFORMAT.dwDataSize dice si es teclado o ratón. */
        g_devices[g_ndevices].device = args[0];
        g_devices[g_ndevices++].size = (int)((uint32_t*)(uintptr_t)args[1])[3];
        fprintf(stderr, "[MP2:INPUT] dispositivo %08X con estado de %u bytes\n", args[0], ((uint32_t*)(uintptr_t)args[1])[3]);
        return;
    }
    if (!g_di_createdevice) {
        /* DirectInput valida el HINSTANCE: el del módulo invitado no es uno de Windows. */
        const char* name = native32_name(target);
        if (name && !strcmp(name, "DINPUT.dll!DirectInputCreateA")) args[0] = (uint32_t)(uintptr_t)GetModuleHandleA(NULL);
    }
    if (!g_d3d_vt[0]) return;
    if (g_dev_vt[0] && target == g_dev_vt[DEV_PRESENT]) {
        /* g_frames aún no cuenta este frame. */
        uint32_t frame = g_frames + 1;
        if (frame == 1 || frame % g_capture_every == 0 || (g_mp2_stop_after_frames && frame == g_mp2_stop_after_frames)) {
            char shot[32];
            snprintf(shot, sizeof shot, "frame_%05u.bmp", frame);
            save_back_buffer((void*)(uintptr_t)args[0], shot);
            fflush(stderr);
        }
        return;
    }
    if (target == g_d3d_vt[D3D_CREATEDEVICE]) {
        force_windowed((uint32_t*)(uintptr_t)args[5], "CreateDevice");
        fprintf(stderr, "[MP2:D3D] CreateDevice adaptador=%u tipo=%u comportamiento=%X\n", args[1], args[2], args[4]);
    } else if (g_dev_vt[0] && target == g_dev_vt[DEV_RESET]) {
        force_windowed((uint32_t*)(uintptr_t)args[1], "Reset");
    }
}

static void post_hook(uint32_t target, const uint32_t* args, uint32_t ret) {
    mp2_measure_call(target);
    if (g_mp2_create8_va && target == g_mp2_create8_va && ret) {
        const uint32_t* vt = *(uint32_t**)(uintptr_t)ret;
        fprintf(stderr, "[MP2:D3D] MILESTONE Direct3DCreate8(%u) -> objeto real\n", args[0]);
        for (int i = 0; i < ND3D; i++) {
            g_d3d_vt[i] = vt[i];
            native32_add_name(vt[i], "IDirect3D8", k_d3d[i]);
        }
        return;
    }
    if (g_d3d_vt[0] && target == g_d3d_vt[D3D_CREATEDEVICE]) {
        fprintf(stderr, "[MP2:D3D] CreateDevice -> %08X\n", ret);
        if (ret == 0) {
            const uint32_t* vt = **(uint32_t***)(uintptr_t)args[6];
            fprintf(stderr, "[MP2:D3D] MILESTONE dispositivo creado\n");
            for (int i = 0; i < NDEV; i++) {
                g_dev_vt[i] = vt[i];
                native32_add_name(vt[i], "IDirect3DDevice8", k_dev[i]);
            }
        }
        return;
    }
    const char* name = native32_name(target);
    if (name && !strcmp(name, "DINPUT.dll!DirectInputCreateA") && ret == 0) {
        const uint32_t* vt = **(uint32_t***)(uintptr_t)args[2];
        g_di_createdevice = vt[DI_CREATEDEVICE];
        return;
    }
    if (g_di_createdevice && target == g_di_createdevice && ret == 0 && !g_did_setcoop) {
        const uint32_t* vt = **(uint32_t***)(uintptr_t)args[2];
        g_did_setcoop = vt[DID_SETCOOPERATIVE];
        g_did_state = vt[DID_GETDEVICESTATE];
        g_did_data = vt[DID_GETDEVICEDATA];
        g_did_format = vt[DID_SETDATAFORMAT];
        return;
    }
    if (g_did_state && target == g_did_state) {
        g_state_reads++;
        if (g_scripted && ret == 0) fill_state(args[0], args[1], (uint8_t*)(uintptr_t)args[2]);
        return;
    }
    if (g_did_data && target == g_did_data) {
        g_data_reads++;
        if (g_scripted && ret == 0) fill_buffered(args[0], args[1], (uint8_t*)(uintptr_t)args[2], (uint32_t*)(uintptr_t)args[3]);
        return;
    }
    if (!g_dev_vt[0]) return;
    for (int i = 0; i < NDEV; i++) {
        if (g_dev_vt[i] != target) continue;
        g_count[i]++;
        switch (i) {
        case DEV_SETRS: vs_add(&s_rs, args[1]); break;
        case DEV_SETTSS: vs_add(&s_tss, args[2]); break;
        case DEV_CREATETEXTURE: vs_add(&s_texfmt, args[5]); break;
        case DEV_CREATECUBE: vs_add(&s_cubefmt, args[4]); break;
        case DEV_CREATEVS: s_vs++; break;
        case DEV_CREATEPS: s_ps++; break;
        case DEV_PRESENT:
            g_frames++;
            if (g_frames == 1) fprintf(stderr, "[MP2:D3D] MILESTONE primer Present\n");
            input_advance(g_frames);
            pace_frame();
            if (g_frames % 300 == 0) {
                fprintf(stderr, "[MP2:D3D] %u frames\n", g_frames);
                mp2_d3d8_write_report();
                fflush(stderr);
            }
            if (g_mp2_stop_after_frames && g_frames >= g_mp2_stop_after_frames)
                mp2_stop("OBJETIVO", "alcanzado el número de frames pedido", MP2_EXIT_GAME);
            break;
        }
        return;
    }
}

void mp2_d3d8_trace_init(void) {
    native32_pre_hook = pre_hook;
    native32_post_hook = post_hook;
}
