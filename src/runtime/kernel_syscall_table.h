#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
namespace mp2r {
class KernelSyscallTable {
    std::array<uint32_t,256> handlers_{};
    uint32_t physical_base_;
    uint32_t timer3_interrupt_handler_{};
public:
    // MP2R-owned guest kernel ABI region, not an address inferred from a PS2 BIOS.
    // The original PAL routine discovers this table by searching for its callbacks.
    explicit KernelSyscallTable(uint32_t base=0x00060000):physical_base_(base) {
        if((base&15) || base<0x1000 || base>0x80000-1024) throw std::runtime_error("Invalid native kernel table region");
    }
    uint32_t physicalBase() const { return physical_base_; }
    uint32_t virtualBase() const { return physical_base_|0x80000000u; }
    void install(uint8_t* ram,uint32_t number,uint32_t handler) {
        // Original alarm patch uses this extended slot for INTC12, not a syscall.
        if(number==0x12c) { timer3_interrupt_handler_=handler; return; }
        if(number>=handlers_.size()) throw std::runtime_error("Unsupported syscall-table index");
        handlers_[number]=handler;
        std::memcpy(ram+physical_base_+number*4,&handler,4);
    }
    uint32_t handler(uint32_t number) const { return number<handlers_.size()?handlers_[number]:0; }
    uint32_t timer3InterruptHandler() const { return timer3_interrupt_handler_; }
};
}
