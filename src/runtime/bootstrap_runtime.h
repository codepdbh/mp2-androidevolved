#pragma once
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include <array>
#include <stdexcept>

namespace mp2r {
// Native bootstrap service boundary. This does not implement the EE scheduler,
// interrupts, IOP or a general kernel. Unresolved operations stop explicitly.
class BootstrapRuntime {
public:
    using GuestBranchKind=PS2Runtime::GuestBranchKind;
    struct Thread { uint32_t gp{},stack{},size{},args{},root{},initial_sp{}; } thread;
    uint32_t heap_base{},heap_size_request{},blocked_target{},blocked_source{};
    std::array<uint32_t,128> syscalls{};
    unsigned syscall_count{},checkpoints{};
    bool blocked{};
    bool eeCheckpointDue(uint32_t=32) noexcept { return ++checkpoints%4096==0; }
    static bool isSpecialAddress(uint32_t a) { return Ps2IsSpecialAddress(a); }
    void handleSyscall(uint8_t*,R5900Context* cpu,uint32_t encoded) {
        auto number=encoded?encoded:GPR_U32(cpu,3);
        if(syscall_count<syscalls.size()) syscalls[syscall_count]=number;
        ++syscall_count;
        if(number==0x3c) {
            thread={GPR_U32(cpu,4),GPR_U32(cpu,5),GPR_U32(cpu,6),GPR_U32(cpu,7),GPR_U32(cpu,8),0};
            uint64_t top=uint64_t(thread.stack)+thread.size;
            if(thread.stack==0 || thread.size==0 || top>PS2_RAM_SIZE || (thread.gp&3))
                throw std::runtime_error("Unsupported bootstrap thread arguments");
            thread.initial_sp=static_cast<uint32_t>(top)&~15u;
            SET_GPR_U32(cpu,28,thread.gp);
            SET_GPR_U32(cpu,2,thread.initial_sp);
        } else if(number==0x3d) {
            if(!thread.initial_sp) throw std::runtime_error("Heap setup before main thread");
            heap_base=(GPR_U32(cpu,4)+15u)&~15u;
            heap_size_request=GPR_U32(cpu,5);
            if(heap_base>=thread.stack || heap_size_request!=0xffffffffu)
                throw std::runtime_error("Unsupported bootstrap heap arguments");
            // Record the rest-of-memory request; no allocator is claimed here.
            SET_GPR_U32(cpu,2,heap_base);
        } else throw std::runtime_error("Unimplemented bootstrap syscall");
    }
    bool dispatchGuestBranch(uint8_t*,R5900Context*,uint32_t target,uint32_t source,
                             uint32_t,GuestBranchKind,const char*) {
        blocked=true; blocked_target=target; blocked_source=source;
        return false; // stop, never pretend the missing function returned
    }
    void Store8(uint8_t*,R5900Context*,uint32_t,uint8_t) { throw std::runtime_error("Unexpected MMIO Store8"); }
    void Store128(uint8_t*,R5900Context*,uint32_t,__m128i) { throw std::runtime_error("Unexpected MMIO Store128"); }
};
}
