#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace mp2r {
// EE DMA controller, transfer engine for memory -> peripheral channels (VIF0/VIF1/GIF/...).
// Channel register block at 0x10008000 + 0x1000*n for n<=7 (VIF0, VIF1, GIF, IPU_FROM, IPU_TO,
// SIF0, SIF1, SIF2), SPR_FROM 0x1000D000, SPR_TO 0x1000D400: CHCR +0x00, MADR +0x10, QWC +0x20,
// TADR +0x30, ASR0 +0x40, ASR1 +0x50, SADR +0x80. Transfers run to completion when STR is set;
// source chain tags: refe(0) cnt(1) next(2) ref(3) refs(4) call(5) ret(6) end(7), two-level ASR
// stack, IRQ bit 31 with CHCR.TIE ending the chain. Memory addresses with bit 31 select SPR.
class EeDmac {
public:
    struct Channel { uint32_t chcr{},madr{},qwc{},tadr{},asr0{},asr1{},sadr{}; };
    using Sink=std::function<void(const uint8_t* qwords,uint32_t count,uint32_t tag_lo,uint32_t tag_hi,bool has_tag)>;
    std::array<Channel,10> channels{};
    std::array<Sink,10> sinks{};
    uint32_t ctrl{},pcr{},sqwc{},rbsr{},rbor{},stadr{},enable=0x1201;   // D_ENABLER reset value
    std::string stop_reason;
    uint8_t* ram{}; uint8_t* scratchpad{};
    uint64_t qwords_transferred{};

    static int channelOf(uint32_t p) {
        if(p>=0x10008000u && p<0x10010000u && (p&0x0fffu)<0x0100u) {
            if(p<0x1000d000u) return int((p-0x10008000u)>>12);
            if(p<0x1000d400u) return 8;
            if(p<0x1000d500u) return 9;
        }
        return -1;
    }
    bool owns(uint32_t a) const {
        auto p=a&0x1fffffffu;
        if(channelOf(p)>=0) return true;
        return p==0x1000e000u || p==0x1000e020u || p==0x1000e030u || p==0x1000e040u || p==0x1000e050u ||
               p==0x1000e060u || p==0x1000f520u || p==0x1000f590u;
    }
    uint32_t read(uint32_t a) const {
        auto p=a&0x1fffffffu; int n=channelOf(p);
        if(n>=0) {
            const auto& c=channels[n];
            switch((p&0x3ffu)&~0xfu) { case 0x00:return c.chcr; case 0x10:return c.madr; case 0x20:return c.qwc; case 0x30:return c.tadr;
                case 0x40:return c.asr0; case 0x50:return c.asr1; case 0x80:return c.sadr; default:return 0; }
        }
        switch(p) { case 0x1000e000u:return ctrl; case 0x1000e020u:return pcr; case 0x1000e030u:return sqwc;
            case 0x1000e040u:return rbsr; case 0x1000e050u:return rbor; case 0x1000e060u:return stadr; case 0x1000f520u:return enable; }
        return 0;
    }
    // Returns the channel whose transfer completed (for D_STAT/INT1), or -1.
    int write(uint32_t a,uint32_t v) {
        auto p=a&0x1fffffffu; int n=channelOf(p);
        if(n>=0) {
            auto& c=channels[n];
            switch((p&0x3ffu)&~0xfu) {
                case 0x00: c.chcr=v; if((v&0x100) && (ctrl&1)) return start(n); return -1;
                case 0x10: c.madr=v; break; case 0x20: c.qwc=v&0xffff; break; case 0x30: c.tadr=v; break;
                case 0x40: c.asr0=v; break; case 0x50: c.asr1=v; break; case 0x80: c.sadr=v; break;
            }
            return -1;
        }
        switch(p) { case 0x1000e000u:ctrl=v;break; case 0x1000e020u:pcr=v;break; case 0x1000e030u:sqwc=v;break;
            case 0x1000e040u:rbsr=v;break; case 0x1000e050u:rbor=v;break; case 0x1000e060u:stadr=v;break; case 0x1000f590u:enable=v;break; }
        return -1;
    }
private:
    const uint8_t* memory(uint32_t address,uint32_t qwords) {
        if(address&0x80000000u) {                                  // scratchpad
            auto off=address&0x3ff0u;
            if(off+qwords*16>0x4000) throw std::runtime_error("DMA beyond scratchpad");
            return scratchpad+off;
        }
        auto off=address&0x1fffff0u;
        if(uint64_t(off)+qwords*16>0x2000000) throw std::runtime_error("DMA beyond RAM");
        return ram+off;
    }
    void deliver(int n,uint32_t address,uint32_t qwords,uint32_t lo=0,uint32_t hi=0,bool has_tag=false) {
        if(!sinks[n]) { stop_reason="dma_channel_without_sink "+std::to_string(n); throw std::runtime_error(stop_reason); }
        sinks[n](qwords?memory(address,qwords):nullptr,qwords,lo,hi,has_tag);
        qwords_transferred+=qwords;
    }
    int start(int n) {
        auto& c=channels[n];
        uint32_t dir=c.chcr&1,mode=(c.chcr>>2)&3;
        if(n!=2 && n!=1 && n!=0) { stop_reason="dma_channel_unsupported "+std::to_string(n); throw std::runtime_error(stop_reason); }
        if(!dir && n!=0 && n!=1 && n!=2) { stop_reason="dma_to_memory_unsupported"; throw std::runtime_error(stop_reason); }
        if(mode==0) {                                              // normal
            deliver(n,c.madr,c.qwc); c.madr+=c.qwc*16; c.qwc=0;
        } else if(mode==1) {                                       // source chain
            bool tte=(c.chcr&0x40)!=0;
            for(int guard=0;guard<1000000;++guard) {
                if(c.qwc) { deliver(n,c.madr,c.qwc); c.madr+=c.qwc*16; c.qwc=0; }
                uint32_t tag[4]; std::memcpy(tag,memory(c.tadr,1),16);
                uint32_t qwc=tag[0]&0xffff,id=(tag[0]>>28)&7,addr=tag[1];
                bool irq=(tag[0]>>31)&1;
                c.chcr=(c.chcr&0xffff)|(tag[0]&0xffff0000u);           // TAG field mirrors the last tag
                if(tte) deliver(n,0,0,tag[2],tag[3],true);              // VIF transfers the tag's upper half
                bool end=false;
                switch(id) {
                    case 0: c.madr=addr; c.qwc=qwc; c.tadr+=16; end=true; break;          // refe
                    case 1: c.madr=c.tadr+16; c.qwc=qwc; c.tadr=c.madr+qwc*16; break;     // cnt
                    case 2: c.madr=c.tadr+16; c.qwc=qwc; c.tadr=addr; break;              // next
                    case 3: case 4: c.madr=addr; c.qwc=qwc; c.tadr+=16; break;            // ref / refs
                    case 5: {                                                             // call
                        c.madr=c.tadr+16; c.qwc=qwc;
                        uint32_t ret=c.madr+qwc*16, asp=(c.chcr>>4)&3;
                        if(asp==0) c.asr0=ret; else if(asp==1) c.asr1=ret; else { stop_reason="dma_call_stack_overflow"; throw std::runtime_error(stop_reason); }
                        c.chcr=(c.chcr&~0x30u)|((asp+1)<<4); c.tadr=addr; break;
                    }
                    case 6: {                                                             // ret
                        c.madr=c.tadr+16; c.qwc=qwc; uint32_t asp=(c.chcr>>4)&3;
                        if(asp==0) end=true;
                        else { c.tadr=asp==2?c.asr1:c.asr0; c.chcr=(c.chcr&~0x30u)|((asp-1)<<4); }
                        break;
                    }
                    case 7: c.madr=c.tadr+16; c.qwc=qwc; end=true; break;                 // end
                }
                if(c.qwc) { deliver(n,c.madr,c.qwc); c.madr+=c.qwc*16; c.qwc=0; }
                if(end || (irq && (c.chcr&0x80))) break;
                if(guard==999999) { stop_reason="dma_chain_runaway"; throw std::runtime_error(stop_reason); }
            }
        } else { stop_reason="dma_mode_unsupported "+std::to_string(mode); throw std::runtime_error(stop_reason); }
        c.chcr&=~0x100u;                                           // STR off: transfer complete
        return n;
    }
};
}
