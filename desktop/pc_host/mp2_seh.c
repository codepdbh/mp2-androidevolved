/*
 * mp2_seh.c - despacho de excepciones estructuradas sobre la pila invitada.
 *
 * Todos los módulos del motor lanzan y capturan excepciones C++ (2 067 bloques
 * try, en su mayoría catch(...)). El código recompilado guarda sus marcos en
 * fs:[0] de un TIB invitado que Windows no recorre, así que aquí se hace lo que
 * haría ntdll: recorrer la cadena y llamar a cada manejador, que son los de
 * msvcr71 recompilados (__CxxFrameHandler, _except_handler3).
 *
 * El catch no necesita longjmp. _JumpToContinuation salta a la continuación, que
 * corre encima de los marcos C del throw; al retornar, RECOMP_SEH_CHECK hace que
 * los marcos muertos retornen en cadena hasta el llamador real.
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
#define EXCEPTION_NONCONTINUABLE_FLAG 1u
#define EXCEPTION_UNWINDING_FLAG 2u
#define EXCEPTION_EXIT_UNWIND_FLAG 4u
#define STATUS_UNWIND_CODE 0xC0000027u
#define CHAIN_END 0xFFFFFFFFu
#define CXX_EXCEPTION 0xE06D7363u

typedef enum { HANDLER_CONTINUE_EXECUTION = 0, HANDLER_CONTINUE_SEARCH = 1, HANDLER_NESTED = 2, HANDLER_COLLIDED = 3 } handler_result_t;

static uint32_t chain_head(void) { return *(uint32_t*)(uintptr_t)g_fs_base; }
static void set_chain_head(uint32_t frame) { *(uint32_t*)(uintptr_t)g_fs_base = frame; }

/* Tipo lanzado por _CxxThrowException: ThrowInfo -> CatchableTypeArray ->
 * CatchableType -> TypeDescriptor. Solo para los informes. */
static const char* cxx_type_name(const EXCEPTION_RECORD* record) {
    if (record->ExceptionCode != CXX_EXCEPTION || record->NumberParameters < 3) return NULL;
    uint32_t throw_info = (uint32_t)record->ExceptionInformation[2];
    if (!throw_info) return "(rethrow)";
    uint32_t array = *(uint32_t*)(uintptr_t)(throw_info + 12);
    uint32_t type = *(uint32_t*)(uintptr_t)(array + 4);
    uint32_t descriptor = *(uint32_t*)(uintptr_t)(type + 4);
    return (const char*)(uintptr_t)(descriptor + 8);
}

/* Llama a un manejador invitado (cdecl). Devuelve 1 si la pila quedó por encima
 * de `floor`: una continuación ya corrió y este marco está muerto. */
static int call_handler(uint32_t handler, uint32_t record, uint32_t frame, uint32_t context, uint32_t dispatch,
                        uint32_t floor, uint32_t* result) {
    recomp_func_t fn = recomp_lookup(handler);
    if (!fn) {
        char text[120];
        snprintf(text, sizeof text, "manejador SEH sin recompilar 0x%08X (marco 0x%08X)", handler, frame);
        mp2_stop("SEH", text, MP2_EXIT_SEH);
        return 1;
    }
    uint32_t saved = g_esp, caller = g_cur_func;
    g_esp -= 16;
    ((uint32_t*)(uintptr_t)g_esp)[0] = record;
    ((uint32_t*)(uintptr_t)g_esp)[1] = frame;
    ((uint32_t*)(uintptr_t)g_esp)[2] = context;
    ((uint32_t*)(uintptr_t)g_esp)[3] = dispatch;
    g_esp -= 4;
    *(uint32_t*)(uintptr_t)g_esp = RECOMP_RETADDR;
    fn();
    g_cur_func = caller;
    if (g_esp > floor) return 1;
    *result = g_eax;
    g_esp = saved;
    return 0;
}

/* RaiseException(code, flags, count, args): stdcall. */
void mp2_seh_RaiseException(void) {
    uint32_t code = ARG(0), flags = ARG(1), count = ARG(2), args = ARG(3);
    uint32_t floor = g_esp;  /* apunta a la dirección de retorno */

    /* SetThreadName de MSVC: nadie la captura y no tiene efecto en el juego. */
    if (code == 0x406D1388u) {
        g_esp += 4 + 16;
        return;
    }
    uint32_t saved_ebx = g_ebx, saved_esi = g_esi, saved_edi = g_edi, saved_ebp = g_ebp;

    g_esp -= (sizeof(CONTEXT) + sizeof(EXCEPTION_RECORD) + 15) & ~15u;
    CONTEXT* ctx = (CONTEXT*)(uintptr_t)g_esp;
    EXCEPTION_RECORD* rec = (EXCEPTION_RECORD*)(ctx + 1);
    memset(ctx, 0, sizeof *ctx);
    memset(rec, 0, sizeof *rec);
    ctx->ContextFlags = CONTEXT_FULL;
    ctx->Eax = g_eax; ctx->Ebx = g_ebx; ctx->Ecx = g_ecx; ctx->Edx = g_edx;
    ctx->Esi = g_esi; ctx->Edi = g_edi; ctx->Ebp = g_ebp; ctx->Esp = floor + 4 + 16;
    ctx->Eip = g_cur_func;
    rec->ExceptionCode = code;
    rec->ExceptionFlags = flags & EXCEPTION_NONCONTINUABLE_FLAG;
    rec->ExceptionAddress = (void*)(uintptr_t)g_cur_func;
    rec->NumberParameters = count > EXCEPTION_MAXIMUM_PARAMETERS ? EXCEPTION_MAXIMUM_PARAMETERS : count;
    if (args && rec->NumberParameters)
        memcpy(rec->ExceptionInformation, (void*)(uintptr_t)args, rec->NumberParameters * 4);

    const char* type = cxx_type_name(rec);
    fprintf(stderr, "[MP2:SEH] excepción %08X%s%s desde sub_%08X\n", code, type ? " " : "", type ? type : "", g_cur_func);
    /* Las direcciones de retorno de la pila invitada son ficticias: el rastro útil
     * son las últimas llamadas indirectas y nativas. */
    if (type && strcmp(type, "(rethrow)")) {
        if (rec->NumberParameters >= 2 && strstr(type, "PAV")) {
            uint32_t object = *(uint32_t*)(uintptr_t)rec->ExceptionInformation[1];
            const uint32_t* words = (const uint32_t*)(uintptr_t)object;
            fprintf(stderr, "[MP2:SEH]   objeto %08X: %08X %08X %08X %08X %08X\n", object, words[0], words[1], words[2], words[3], words[4]);
        }
        for (uint32_t i = 1; i <= 12 && i <= g_native_ring_idx; i++) {
            native32_call_rec_t* r = &g_native_ring[(g_native_ring_idx - i) & (NATIVE32_RING - 1)];
            const char* n = native32_name(r->target);
            fprintf(stderr, "[MP2:SEH]   nativa %-40s desde sub_%08X (%08X %08X %08X) -> %08X\n", n ? n : "?", r->from, r->a0, r->a1, r->a2, r->ret);
        }
        for (uint32_t i = 1; i <= 16 && i <= g_icall_trace_idx; i++) {
            uint32_t k = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
            fprintf(stderr, "[MP2:SEH]   indirecta %08X desde sub_%08X\n", g_icall_trace[k], g_icall_from[k]);
        }
    }

    /* Un manejador que retorne tras una continuación deja la pila por encima de
     * floor y todos los registros ya son los de la continuación: no se tocan. */
    for (uint32_t frame = chain_head(); frame != CHAIN_END; frame = *(uint32_t*)(uintptr_t)frame) {
        uint32_t handler = *(uint32_t*)(uintptr_t)(frame + 4), result = 0;
        uint32_t record = (uint32_t)(uintptr_t)rec, context = (uint32_t)(uintptr_t)ctx;
        if (call_handler(handler, record, frame, context, 0, floor, &result)) return;
        if (result == HANDLER_CONTINUE_EXECUTION) {
            if (rec->ExceptionFlags & EXCEPTION_NONCONTINUABLE_FLAG)
                mp2_stop("SEH", "un manejador intentó continuar una excepción no continuable", MP2_EXIT_SEH);
            g_ebx = saved_ebx; g_esi = saved_esi; g_edi = saved_edi; g_ebp = saved_ebp;
            g_esp = floor + 4 + 16;
            return;
        }
        if (result != HANDLER_CONTINUE_SEARCH) {
            char text[120];
            snprintf(text, sizeof text, "manejador 0x%08X devolvió %u", handler, result);
            mp2_stop("SEH", text, MP2_EXIT_SEH);
        }
    }
    char text[300];
    snprintf(text, sizeof text, "excepción %08X sin manejador%s%s", code, type ? ": " : "", type ? type : "");
    mp2_stop("SEH", text, MP2_EXIT_SEH);
}

/* RtlUnwind(target_frame, target_ip, record, value): stdcall. Llama a los
 * manejadores con la bandera de unwind, retira sus marcos y retorna: target_ip es
 * siempre la instrucción que sigue a la llamada en _global_unwind2 de msvcr71. */
void mp2_seh_RtlUnwind(void) {
    uint32_t target = ARG(0), record = ARG(2), value = ARG(3);
    uint32_t floor = g_esp;
    uint32_t saved_ebx = g_ebx, saved_esi = g_esi, saved_edi = g_edi, saved_ebp = g_ebp;

    g_esp -= (sizeof(CONTEXT) + sizeof(EXCEPTION_RECORD) + 15) & ~15u;
    CONTEXT* ctx = (CONTEXT*)(uintptr_t)g_esp;
    EXCEPTION_RECORD* own = (EXCEPTION_RECORD*)(ctx + 1);
    memset(ctx, 0, sizeof *ctx);
    memset(own, 0, sizeof *own);
    ctx->ContextFlags = CONTEXT_FULL;
    EXCEPTION_RECORD* rec = record ? (EXCEPTION_RECORD*)(uintptr_t)record : own;
    if (!record) rec->ExceptionCode = STATUS_UNWIND_CODE;
    rec->ExceptionFlags |= EXCEPTION_UNWINDING_FLAG;
    if (!target) rec->ExceptionFlags |= EXCEPTION_EXIT_UNWIND_FLAG;

    for (uint32_t frame = chain_head(); frame != CHAIN_END && frame != target; ) {
        uint32_t handler = *(uint32_t*)(uintptr_t)(frame + 4), result = 0;
        if (call_handler(handler, (uint32_t)(uintptr_t)rec, frame, (uint32_t)(uintptr_t)ctx, 0, floor, &result)) return;
        if (result != HANDLER_CONTINUE_SEARCH && result != HANDLER_COLLIDED) {
            char text[120];
            snprintf(text, sizeof text, "unwind: el manejador 0x%08X devolvió %u", handler, result);
            mp2_stop("SEH", text, MP2_EXIT_SEH);
        }
        frame = *(uint32_t*)(uintptr_t)frame;
        set_chain_head(frame);
    }
    if (target) set_chain_head(target);
    rec->ExceptionFlags &= ~(EXCEPTION_UNWINDING_FLAG | EXCEPTION_EXIT_UNWIND_FLAG);
    g_ebx = saved_ebx; g_esi = saved_esi; g_edi = saved_edi; g_ebp = saved_ebp;
    g_eax = value;
    g_esp = floor + 4 + 16;
}
