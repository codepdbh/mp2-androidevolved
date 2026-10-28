#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
namespace mp2r {
// GIF packet processing (PATH3 / DMA channel 2): GIFtag NLOOP/EOP/PRE/PRIM/FLG/NREG/REGS,
// PACKED (A+D and per-register expansion), REGLIST and IMAGE modes, delivering GS register
// writes to a callback. Rasterization lives in the GS unit that consumes these writes.
class GsGif {
public:
    // reg: GS register address (0x00-0x63); value: 64-bit register data.
    std::function<void(uint8_t reg,uint64_t value)> write_register;
    std::function<void(const uint8_t* data,uint32_t bytes)> image;   // HWREG / IMAGE data (host->local)
    std::string stop_reason;
    uint64_t tags{},packed_writes{},image_qwords{};
    // Feeds whole qwords; a GIF packet may span several DMA blocks.
    void feed(const uint8_t* q,uint32_t count) {
        for(uint32_t i=0;i<count;++i) qword(q+i*16);
    }
private:
    uint32_t nloop_{},nreg_{},flg_{},reg_index_{};
    uint64_t regs_{};
    bool eop_{},active_{};
    uint64_t q_{0x3f800000};                              // Q of the last ST (PACKED ST)
    void qword(const uint8_t* q) {
        uint64_t lo,hi; std::memcpy(&lo,q,8); std::memcpy(&hi,q+8,8);
        if(!active_) {                                     // GIFtag
            nloop_=uint32_t(lo&0x7fff); eop_=(lo>>15)&1; flg_=uint32_t((lo>>58)&3); nreg_=uint32_t((lo>>60)&0xf); if(!nreg_) nreg_=16;
            regs_=hi; reg_index_=0; ++tags;
            if(nloop_) {
                q_=0x3f800000;                             // GIF temporary Q resets to 1.0 at a nonempty tag
                if(flg_==0 && ((lo>>46)&1)) write_register(0x00,(lo>>47)&0x7ff); // PRE applies only to PACKED
            }
            active_=nloop_!=0;
            return;
        }
        if(flg_==2 || flg_==3) {                           // IMAGE (FLG=3 behaves as IMAGE)
            if(image) image(q,16); ++image_qwords;
            if(--nloop_==0) active_=false;
            return;
        }
        if(flg_==0) {                                      // PACKED
            uint32_t reg=uint32_t((regs_>>(reg_index_*4))&0xf);
            packed(reg,lo,hi); ++packed_writes;
            advance();
            return;
        }
        // REGLIST: two 64-bit register values per qword
        for(uint64_t v:{lo,hi}) {
            if(!active_) break;
            uint32_t reg=uint32_t((regs_>>(reg_index_*4))&0xf);
            if(reg!=0xe && reg!=0xf) write_register(uint8_t(reg),v);
            advance();
        }
    }
    void advance() {
        if(++reg_index_>=nreg_) { reg_index_=0; if(--nloop_==0) active_=false; }
    }
    // PACKED expansion per the GS user's manual (register descriptors 0x0-0xF).
    void packed(uint32_t reg,uint64_t lo,uint64_t hi) {
        auto f=[&](int bit,int count,uint64_t v){ return (v>>bit)&((uint64_t(1)<<count)-1); };
        switch(reg) {
            case 0x0: write_register(0x00,f(0,11,lo)); break;                                         // PRIM
            case 0x1: write_register(0x01,f(0,8,lo)|f(32,8,lo)<<8|f(0,8,hi)<<16|f(32,8,hi)<<24|(q_<<32)); break; // RGBAQ
            case 0x2: q_=f(0,32,hi); write_register(0x02,lo); break;                                  // ST
            case 0x3: write_register(0x03,f(0,14,lo)|f(32,14,lo)<<16); break;                         // UV
            case 0x4: case 0xc: {                                                                     // XYZF2/3
                uint64_t v=f(0,16,lo)|f(32,16,lo)<<16|f(4,24,hi)<<32|f(36,8,hi)<<56;
                write_register(reg==0xc || f(47,1,hi)?0x0c:0x04,v); break;
            }
            case 0x5: case 0xd: {                                                                     // XYZ2/3
                uint64_t v=f(0,16,lo)|f(32,16,lo)<<16|f(0,32,hi)<<32;
                write_register(reg==0xd || f(47,1,hi)?0x0d:0x05,v); break;
            }
            case 0x6: write_register(0x06,lo); break; case 0x7: write_register(0x07,lo); break;     // TEX0_1/2
            case 0x8: write_register(0x08,lo); break; case 0x9: write_register(0x09,lo); break;     // CLAMP_1/2
            case 0xa: write_register(0x0a,f(36,8,hi)<<56); break;                                     // FOG
            case 0xe: write_register(uint8_t(hi&0xff),lo); break;                                     // A+D
            case 0xf: break;                                                                          // NOP
            default: stop_reason="gif_packed_register "+std::to_string(reg);
        }
    }
};
}
