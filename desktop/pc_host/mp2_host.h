#ifndef MP2_HOST_H
#define MP2_HOST_H
#include <stdint.h>
#include "native32.h"

/* Misma forma que mp2_module_t de mp2_modules_gen.h, sin depender del generado. */
typedef struct { const char* name; uint32_t base, size, entry; int is_static; } mp2_module_info_t;

extern char g_mp2_game_root[];
extern char g_mp2_guest_dir[];
extern char g_mp2_sandbox[];
extern const int g_mp2_module_count;
const mp2_module_info_t* mp2_module_at(int index);
int mp2_run_dllmain(const char* name, uint32_t base, uint32_t entry);

extern native32_shim_t g_mp2_shims[];
extern int g_mp2_nshims;

void mp2_install_crash_handler(void);
void mp2_watch_write(uint32_t address, uint32_t mask, uint32_t value);
/* Vuelca el estado invitado y termina el proceso con `code`. */
void mp2_stop(const char* kind, const char* detail, int code);

/* Despacho de excepciones sobre la pila invitada (mp2_seh.c). */
void mp2_seh_RaiseException(void);
void mp2_seh_RtlUnwind(void);

/* Registro virtual (mp2_registry.c). */
void mp2_registry_init(const char* defaults);
void mp2_RegOpenKeyA(void); void mp2_RegOpenKeyExA(void); void mp2_RegCreateKeyA(void); void mp2_RegCreateKeyExA(void);
void mp2_RegCloseKey(void); void mp2_RegQueryValueExA(void); void mp2_RegQueryValueA(void); void mp2_RegSetValueExA(void);
void mp2_RegSetValueA(void); void mp2_RegDeleteKeyA(void); void mp2_RegDeleteValueA(void); void mp2_RegEnumKeyA(void);

/* Traza y política de D3D8/DirectInput (mp2_d3d8_trace.c). */
void mp2_d3d8_trace_init(void);
void mp2_d3d8_write_report(void);
extern uint32_t g_mp2_create8_va;
extern uint32_t g_mp2_stop_after_frames;
extern uint32_t g_capture_every;
void mp2_input_script_load(const char* path);

/* Mediciones para Android (mp2_measure.c). */
void mp2_measure_init(void);
void mp2_measure_call(uint32_t target);
void mp2_measure_write(void);
void mp2_measure_restore_skipped(void);

/* Política de escritorio. */
#define MP2_WINDOW_W 960
#define MP2_WINDOW_H 540
extern int g_mp2_allow_window;
extern int g_mp2_widescreen;
void mp2_load_widescreen(void);
extern char g_mp2_cmdline[];
extern wchar_t g_mp2_cmdline_w[];

/* Códigos de salida del host. */
#define MP2_EXIT_BOUNDARY 0x4D503200   /* llegó a una frontera prevista (ventana, D3D...) */
#define MP2_EXIT_SEH      0x4D503201   /* excepción sin ningún manejador en la cadena invitada */
#define MP2_EXIT_GAME     0x4D503202   /* el juego llamó a ExitProcess */

#endif
