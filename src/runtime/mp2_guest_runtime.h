#pragma once
// Stable boundary between translated original code and the MP2R platform services.
// Generated translation units only see these declarations, so runtime changes never
// force the ~16k translated bodies to recompile. Definitions forward to MP2Runtime.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include <cstddef>
#include <cstdint>

class MP2GuestRuntime {
public:
    using GuestBranchKind=PS2Runtime::GuestBranchKind;
    static bool isSpecialAddress(uint32_t address) { return Ps2IsSpecialAddress(address); }
    bool dispatchGuestBranch(uint8_t* ram,R5900Context* cpu,uint32_t target,uint32_t from,uint32_t link,
                             GuestBranchKind kind,const char* name);
    bool eeCheckpointDue(uint32_t cycles=32);
    void handleSyscall(uint8_t* ram,R5900Context* cpu,uint32_t encoded);
    void handleBreak(uint8_t* ram,R5900Context* cpu);
    void handleTrap(uint8_t* ram,R5900Context* cpu);
    void vu0StartMicroProgram(uint8_t* ram,R5900Context* cpu,uint32_t address);
    void SignalException(R5900Context* cpu,PS2Exception exception);
    void executeVU0Microprogram(uint8_t* ram,R5900Context* cpu,uint32_t address);
    void handleTLBR(uint8_t* ram,R5900Context* cpu);
    void handleTLBWI(uint8_t* ram,R5900Context* cpu);
    void handleTLBWR(uint8_t* ram,R5900Context* cpu);
    void handleTLBP(uint8_t* ram,R5900Context* cpu);
    void clearLLBit(R5900Context* cpu);
    uint8_t Load8(uint8_t* ram,R5900Context* cpu,uint32_t address);
    uint16_t Load16(uint8_t* ram,R5900Context* cpu,uint32_t address);
    uint32_t Load32(uint8_t* ram,R5900Context* cpu,uint32_t address);
    uint64_t Load64(uint8_t* ram,R5900Context* cpu,uint32_t address);
    __m128i Load128(uint8_t* ram,R5900Context* cpu,uint32_t address);
    void Store8(uint8_t* ram,R5900Context* cpu,uint32_t address,uint8_t value);
    void Store16(uint8_t* ram,R5900Context* cpu,uint32_t address,uint16_t value);
    void Store32(uint8_t* ram,R5900Context* cpu,uint32_t address,uint32_t value);
    void Store64(uint8_t* ram,R5900Context* cpu,uint32_t address,uint64_t value);
    void Store128(uint8_t* ram,R5900Context* cpu,uint32_t address,__m128i value);
    // Used by generated guards for direct calls into bodies that were not translated.
    void stopMissing(R5900Context* cpu,uint32_t target,uint32_t from);
};
using MP2GuestFunction=void(*)(uint8_t*,R5900Context*,MP2GuestRuntime*);
struct MP2GuestEntry { uint32_t pc; MP2GuestFunction fn; };
// Provided by the selected-function glue or by the whole-program registration unit.
extern const MP2GuestEntry mp2rGuestEntries[];
extern const std::size_t mp2rGuestEntryCount;
