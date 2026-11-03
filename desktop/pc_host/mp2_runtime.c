/*
 * mp2_runtime.c - lo que el código recompilado exige al host: fallos ruidosos e
 * informe del estado invitado. Adaptado de nfs_runtime.c (nfsu2-recompiled).
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"
#include "recomp_trace.h"
#include "native32.h"
#include "mp2_host.h"

static volatile LONG g_reporting;

static int readable(uint32_t va, uint32_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (va < 0x10000 || !VirtualQuery((void*)(uintptr_t)va, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return va + n <= (uint32_t)(uintptr_t)mbi.BaseAddress + (uint32_t)mbi.RegionSize;
}

static const char* module_of(uint32_t va) {
    for (int i = 0; i < g_mp2_module_count; i++) {
        const mp2_module_info_t* m = mp2_module_at(i);
        if (va >= m->base && va - m->base < m->size) return m->name;
    }
    return NULL;
}

static void report(const char* kind, const char* why) {
    if (InterlockedExchange(&g_reporting, 1)) return;
    fprintf(stderr, "==== MP2 %s: %s\n", kind, why);
    const char* mod = module_of(g_cur_func);
    fprintf(stderr, "función invitada: sub_%08X (%s)  último import: %s\n", g_cur_func, mod ? mod : "?",
            g_cur_import ? g_cur_import : "-");
    fprintf(stderr, "EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X EBP=%08X ESP=%08X\n",
            g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_ebp, g_esp);
    fprintf(stderr, "pila invitada (direcciones dentro de un módulo):\n");
    int shown = 0;
    for (int i = 0; i < 400 && shown < 24; i++) {
        uint32_t a = g_esp + 4u * i;
        if (!readable(a, 4)) break;
        uint32_t v = *(uint32_t*)(uintptr_t)a;
        const char* owner = module_of(v);
        if (owner) { fprintf(stderr, "  [esp+%03X] %08X %s\n", 4 * i, v, owner); shown++; }
    }
    fprintf(stderr, "últimas llamadas nativas:\n");
    for (uint32_t i = 1; i <= 24 && i <= g_native_ring_idx; i++) {
        native32_call_rec_t* r = &g_native_ring[(g_native_ring_idx - i) & (NATIVE32_RING - 1)];
        const char* n = native32_name(r->target);
        fprintf(stderr, "  %-44s desde sub_%08X (%08X %08X %08X) -> %08X\n", n ? n : "?", r->from, r->a0, r->a1, r->a2, r->ret);
    }
    fprintf(stderr, "llamadas indirectas: %u\n", g_icall_count);
    fflush(stderr);
}

void mp2_stop(const char* kind, const char* detail, int code) {
    report(kind, detail);
    mp2_d3d8_write_report();
    mp2_measure_write();
    TerminateProcess(GetCurrentProcess(), (UINT)code);
}

void recomp_unimpl(uint32_t va, const char* what) {
    char why[300];
    snprintf(why, sizeof why, "RECOMP_UNIMPL en 0x%08X: %s", va, what);
    mp2_stop("FALLO", why, 3);
}

void recomp_unresolved(const char* kind, uint32_t va, uint32_t from) {
    char why[300];
    const char* n = native32_name(va);
    const char* mod = module_of(va);
    snprintf(why, sizeof why, "%s a dirección sin recompilar 0x%08X (%s) desde sub_%08X %s", kind, va,
             mod ? mod : "fuera de módulos", from, n ? n : "");
    mp2_stop("FALLO", why, 4);
}

static uint32_t g_host_lo, g_host_hi;

/* Cada función invitada es una función C del host: la pila nativa es el rastro real. */
static void dump_host_stack(const CONTEXT* context) {
    const uint32_t* sp = (const uint32_t*)(uintptr_t)context->Esp;
    fprintf(stderr, "[MP2:STACK] pila nativa, funciones invitadas:\n");
    int shown = 0;
    for (int i = 0; i < 4096 && shown < 32 && readable((uint32_t)(uintptr_t)(sp + i), 4); i++) {
        uint32_t word = sp[i], best_func = 0, best_va = 0;
        if (word < g_host_lo || word >= g_host_hi) continue;
        for (uint32_t k = 0; k < recomp_dispatch_count; k++) {
            uint32_t f = (uint32_t)(uintptr_t)recomp_dispatch_table[k].func;
            if (f <= word && f > best_func) { best_func = f; best_va = recomp_dispatch_table[k].address; }
        }
        if (best_func && word - best_func < 0x40000) {
            const char* owner = module_of(best_va);
            fprintf(stderr, "[MP2:STACK]   sub_%08X +0x%X %s\n", best_va, word - best_func, owner ? owner : "");
            shown++;
        }
    }
}

/* Punto de vigilancia por hardware, para cazar quién pisa un dato invitado: se pone con
 * mp2_watch_write, por ejemplo en la entrada de una función. Se informa cuando lo escrito
 * tiene el aspecto indicado (mask, value). */
#define MP2_WATCH_CODE 0xE0000DB6u
static uint32_t g_watch_address, g_watch_mask, g_watch_value;
static DWORD g_watch_thread;

void mp2_watch_write(uint32_t address, uint32_t mask, uint32_t value) {
    g_watch_address = address;
    g_watch_mask = mask;
    g_watch_value = value;
    g_watch_thread = GetCurrentThreadId();
    RaiseException(MP2_WATCH_CODE, 0, 0, NULL);
}

static LONG CALLBACK crash_veh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode == MP2_WATCH_CODE) {
        ep->ContextRecord->Dr0 = g_watch_address;
        ep->ContextRecord->Dr7 = 0xD0001;   /* DR0 local, escritura, 4 bytes */
        ep->ContextRecord->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && g_watch_address) {
        uint32_t now = *(uint32_t*)(uintptr_t)g_watch_address;
        if ((now & g_watch_mask) == g_watch_value) {
            char why[200];
            snprintf(why, sizeof why, "escritura vigilada en %08X: %08X, ESP invitado %08X", g_watch_address, now, g_esp);
            ep->ContextRecord->Dr7 = 0;
            dump_host_stack(ep->ContextRecord);
            mp2_stop("WATCH", why, 8);
        }
        ep->ContextRecord->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    uint32_t pc = (uint32_t)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    int fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_STACK_OVERFLOW ||
                code == EXCEPTION_PRIV_INSTRUCTION;
    if (fatal && ((pc >= g_host_lo && pc < g_host_hi) || native32_in_guest(pc))) {
        char why[256];
        if (code == EXCEPTION_ACCESS_VIOLATION)
            snprintf(why, sizeof why, "excepción %08lX (%s de %08lX) en host EIP %08X", code,
                     ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "lectura" :
                     ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "escritura" : "ejecución",
                     (unsigned long)ep->ExceptionRecord->ExceptionInformation[1], pc);
        else
            snprintf(why, sizeof why, "excepción %08lX en host EIP %08X", code, pc);
        dump_host_stack(ep->ContextRecord);
        mp2_stop("CRASH", why, 6);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Lo que nadie maneja, también dentro de una DLL nativa: módulo, dirección y estado. */
static LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* ep) {
    uint32_t pc = (uint32_t)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    HMODULE module = NULL;
    char file[MAX_PATH] = "?", why[512];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (const char*)(uintptr_t)pc, &module))
        GetModuleFileNameA(module, file, sizeof file);
    snprintf(why, sizeof why, "excepción %08lX sin manejar en %08X (%s +0x%X), datos %08lX %08lX", ep->ExceptionRecord->ExceptionCode, pc, file,
             module ? pc - (uint32_t)(uintptr_t)module : 0, (unsigned long)ep->ExceptionRecord->ExceptionInformation[0],
             (unsigned long)ep->ExceptionRecord->ExceptionInformation[1]);
    dump_host_stack(ep->ContextRecord);
    mp2_stop("CRASH", why, 7);
    return EXCEPTION_CONTINUE_SEARCH;
}

void mp2_install_crash_handler(void) {
    SetUnhandledExceptionFilter(unhandled_filter);
    HMODULE self = GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)self + ((IMAGE_DOS_HEADER*)self)->e_lfanew);
    g_host_lo = (uint32_t)(uintptr_t)self;
    g_host_hi = g_host_lo + nt->OptionalHeader.SizeOfImage;
    AddVectoredExceptionHandler(0, crash_veh);
}
