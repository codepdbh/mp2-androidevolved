#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace mp2r {
// Plain storage the EE reaches outside main RAM: the 16KB scratchpad (virtual
// 0x70000000, TLB-mapped by the kernel) and VU0/VU1 micro/data memories mapped at
// physical 0x11000000. Registers and devices are not modelled here.
class EeSpecialMemory {
    std::array<uint8_t,0x4000> scratchpad_{};
    std::array<uint8_t,0x10000> vu_{}; // 0x11000000-0x1100FFFF; VU0 regions mirror below 0x11008000
    static constexpr uint32_t physical(uint32_t address) {
        return address>=0x80000000u && address<0xc0000000u?address&0x1fffffffu:address;
    }
    uint8_t* locate(uint32_t address,uint32_t bytes) {
        if(address>=0x70000000u && address<0x70004000u) {
            auto offset=address-0x70000000u;
            return offset+bytes<=scratchpad_.size()?scratchpad_.data()+offset:nullptr;
        }
        auto p=physical(address);
        if(p>=0x11000000u && p<0x11010000u) {
            auto offset=p-0x11000000u;
            if(offset<0x8000u) offset&=0x4000u|0x0fffu; // VU0 4KB code at 0x11000000, 4KB data at 0x11004000
            return offset+bytes<=vu_.size()?vu_.data()+offset:nullptr;
        }
        return nullptr;
    }
public:
    bool read(uint32_t address,void* value,uint32_t bytes) {
        auto* p=locate(address,bytes); if(!p) return false;
        std::memcpy(value,p,bytes); return true;
    }
    bool write(uint32_t address,const void* value,uint32_t bytes) {
        auto* p=locate(address,bytes); if(!p) return false;
        std::memcpy(p,value,bytes); return true;
    }
    uint8_t* scratchpad() { return scratchpad_.data(); }
    uint8_t* vu(uint32_t offset) { return vu_.data()+offset; }
};
}
