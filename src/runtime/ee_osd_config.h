#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
namespace mp2r {
// MP2R session preferences, not values read from a commercial PS2 BIOS/NVRAM.
class EeOsdConfig {
    uint32_t raw_=(uint32_t(-240)&0x7ffu)<<21 | 3u<<16 | 1u<<13 | 1u<<4 | 2u<<1;
    std::array<uint8_t,4> extended_{0,0x80,0,0};
    static bool range(std::span<uint8_t> ram,uint32_t address,uint32_t size) {
        return address!=0 && address<=ram.size() && size<=ram.size()-address;
    }
public:
    uint32_t raw() const { return raw_; }
    bool get(std::span<uint8_t> ram,uint32_t address) const {
        if((address&3) || !range(ram,address,4)) return false;
        std::memcpy(ram.data()+address,&raw_,4); return true;
    }
    bool set(std::span<uint8_t> ram,uint32_t address) {
        if((address&3) || !range(ram,address,4)) return false;
        std::memcpy(&raw_,ram.data()+address,4); return true;
    }
    bool transferExtended(std::span<uint8_t> ram,uint32_t address,uint32_t bytes,uint32_t offset,bool write) {
        if(offset>4 || bytes>4-offset) return false;
        if(!bytes) return true;
        if(!range(ram,address,bytes)) return false;
        if(write) std::memcpy(extended_.data()+offset,ram.data()+address,bytes);
        else std::memcpy(ram.data()+address,extended_.data()+offset,bytes);
        return true;
    }
};
}
