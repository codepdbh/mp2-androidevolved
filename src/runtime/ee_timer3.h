#pragma once
#include <cstdint>
#include <stdexcept>
namespace mp2r {
// Bus-clock counter model. Gate/HBlank inputs require an external video provider.
class EeTimerRegisters {
    uint16_t count_=0,target_=0xffff,mode_=0;
    uint32_t base_;
    uint64_t remainder_{};
public:
    explicit EeTimerRegisters(uint32_t base=0x10001800):base_(base) {}
    bool owns(uint32_t a) const { return a==base_ || a==base_+0x10 || a==base_+0x20; }
    uint32_t read(uint32_t a) const {
        switch(a-base_) { case 0:return count_; case 0x10:return mode_; case 0x20:return target_; }
        throw std::runtime_error("Unknown timer3 register");
    }
    void write(uint32_t a,uint32_t value) {
        switch(a-base_) {
            case 0:count_=uint16_t(value);remainder_=0;return;
            case 0x20:target_=uint16_t(value);return;
            case 0x10:mode_=uint16_t(((mode_&0xc00)&~(value&0xc00))|(value&0x3ff));remainder_=0;return;
        }
        throw std::runtime_error("Unknown timer3 register");
    }
    void latchTarget() { mode_|=0x400; }
    void latchOverflow() { mode_|=0x800; }
    bool needsClock() const { return (mode_&0x80)!=0; }
    bool needsVideoClock() const { return needsClock() && ((mode_&3)==3 || (mode_&4)); }
    // Returns newly latched enabled target/overflow events. W1C rearms each event.
    bool advanceBusCycles(uint64_t cycles) {
        if(!needsClock()) return false;
        if(needsVideoClock()) throw std::runtime_error("Timer video/gate clock required");
        const uint64_t divisor=(mode_&3)==0?1:(mode_&3)==1?16:256;
        uint64_t ticks=cycles/divisor+(cycles%divisor+remainder_)/divisor;
        remainder_=(cycles%divisor+remainder_)%divisor;
        if(!ticks) return false;
        const uint64_t to_target=target_>count_?target_-count_:65536u+target_-count_;
        const uint64_t to_overflow=65536u-count_;
        const auto previous=mode_;
        if((mode_&0x100) && ticks>=to_target) mode_|=0x400;
        if((mode_&0x200) && ticks>=to_overflow &&
           (!(mode_&0x40) || target_==0 || to_overflow<=to_target)) mode_|=0x800;
        if((mode_&0x40) && target_ && ticks>=to_target) count_=uint16_t((ticks-to_target)%target_);
        else count_=uint16_t((uint64_t(count_)+ticks)&0xffff);
        return ((mode_^previous)&0xc00)!=0;
    }
};
using EeTimer3Registers=EeTimerRegisters;
}
