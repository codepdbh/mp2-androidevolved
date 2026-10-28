#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
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
    // Peripheral -> memory channels (IPU_FROM): copy up to max_qwords into dst, return the count.
    using Source=std::function<uint32_t(uint8_t* dst,uint32_t max_qwords)>;
    std::array<Source,10> sources{};
    // Flow-controlled memory -> peripheral channels (IPU_TO): qwords the peripheral accepts now.
    // Such a channel stays active (STR set) and continues through pumpTo() as room appears.
    using Capacity=std::function<uint32_t()>;
    std::array<Capacity,10> capacity{};
    std::array<bool,10> chain_end{};
    // D_CTRL starts with DMAE set: the EE kernel enables the DMAC during boot, before any game code.
    uint32_t ctrl=1,pcr{},sqwc{},rbsr{},rbor{},stadr{},enable=0x1201;   // D_ENABLER reset value
    std::string stop_reason;
    // Addresses in the DMAC register block past D_STADR with no documented register (MP2 writes
    // 0x1000E100 after waiting on channel 8): values are kept and read back, with no other effect.
    std::map<uint32_t,uint32_t> undocumented;
    uint8_t* ram{}; uint8_t* scratchpad{};
    uint64_t qwords_transferred{};

    static int channelOf(uint32_t p) {
        static constexpr uint32_t bases[10]={0x10008000u,0x10009000u,0x1000a000u,0x1000b000u,0x1000b400u,
                                             0x1000c000u,0x1000c400u,0x1000c800u,0x1000d000u,0x1000d400u};
        for(int n=0;n<10;++n) if(p>=bases[n] && p<bases[n]+0x100u) return n;
        return -1;
    }
    bool owns(uint32_t a) const {
        auto p=a&0x1fffffffu;
        if(channelOf(p)>=0) return true;
        if(p>=0x1000e070u && p<0x1000f000u && !(p&0xf)) return true;   // undocumented DMAC block registers
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
        if(p>=0x1000e070u && p<0x1000f000u) { auto it=undocumented.find(p); return it==undocumented.end()?0:it->second; }
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
        if(p>=0x1000e070u && p<0x1000f000u) { undocumented[p]=v; return -1; }
        switch(p) { case 0x1000e000u:ctrl=v;break; case 0x1000e020u:pcr=v;break; case 0x1000e030u:sqwc=v;break;
            case 0x1000e040u:rbsr=v;break; case 0x1000e050u:rbor=v;break; case 0x1000e060u:stadr=v;break; case 0x1000f590u:enable=v;break; }
        return -1;
    }
    // Continues a peripheral -> memory transfer; returns the channel when it completes, else -1.
    int pump(int n) {
        auto& c=channels[n];
        if(!(c.chcr&0x100) || !sources[n]) return -1;
        if(((c.chcr>>2)&3)!=0) { stop_reason="dma_to_memory_mode_unsupported"; throw std::runtime_error(stop_reason); }
        while(c.qwc) {
            // Drain the available peripheral output in one contiguous batch.
            // IPU CSC may queue a whole RGB frame: consuming one qword at a time
            // made its vector front-erases quadratic in the frame size.
            uint32_t size=(c.madr&0x80000000u)?0x4000u:0x2000000u;
            uint32_t offset=c.madr&((c.madr&0x80000000u)?0x3ff0u:0x1fffff0u);
            uint32_t count=std::min(c.qwc,(size-offset)/16);
            uint8_t* dst=const_cast<uint8_t*>(memory(c.madr,count));
            uint32_t got=sources[n](dst,count);
            if(got>count) throw std::runtime_error("DMA source exceeded requested capacity");
            if(!got) return -1;                                    // wait for more peripheral output
            c.madr+=got*16; c.qwc-=got; qwords_transferred+=got;
        }
        c.chcr&=~0x100u; return n;
    }
    // Continues a flow-controlled memory -> peripheral transfer; returns the channel when it completes.
    int pumpTo(int n) {
        auto& c=channels[n];
        if(!(c.chcr&0x100) || !(ctrl&1) || !capacity[n]) return -1;
        uint32_t mode=(c.chcr>>2)&3;
        if(mode>1) { stop_reason="dma_mode_unsupported "+std::to_string(mode); throw std::runtime_error(stop_reason); }
        for(int guard=0;guard<1000000;++guard) {
            while(c.qwc) {
                uint32_t room=capacity[n](); if(!room) return -1;  // peripheral FIFO full
                uint32_t k=std::min(room,c.qwc);
                deliver(n,c.madr,k); c.madr+=k*16; c.qwc-=k;
            }
            if(mode==0 || chain_end[n]) { c.chcr&=~0x100u; return n; }
            chain_end[n]=nextTag(n);
        }
        stop_reason="dma_chain_runaway"; throw std::runtime_error(stop_reason);
    }
private:
    // Reads the source-chain tag at TADR and sets up MADR/QWC; returns true when the chain ends
    // after that block (end/refe/ret with an empty stack, or IRQ with CHCR.TIE).
    bool nextTag(int n) {
        auto& c=channels[n];
        uint32_t tag[4]; std::memcpy(tag,memory(c.tadr,1),16);
        uint32_t qwc=tag[0]&0xffff,id=(tag[0]>>28)&7,addr=tag[1];
        bool irq=(tag[0]>>31)&1;
        c.chcr=(c.chcr&0xffff)|(tag[0]&0xffff0000u);               // TAG field mirrors the last tag
        if(c.chcr&0x40) deliver(n,0,0,tag[2],tag[3],true);          // TTE: VIF transfers the tag's upper half
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
        return end || (irq && (c.chcr&0x80));
    }
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
        if(n==3) return pump(3);                                   // IPU_FROM: drains as output appears
        if(capacity[n]) { chain_end[n]=false; return pumpTo(n); }    // IPU_TO: fills the FIFO as it drains
        if(n==8) {                                                 // SPR_FROM: scratchpad (SADR) -> memory, normal mode
            if(mode!=0) { stop_reason="dma_spr_from_mode_unsupported "+std::to_string(mode); throw std::runtime_error(stop_reason); }
            for(uint32_t i=0;i<c.qwc;++i) {
                std::memcpy(const_cast<uint8_t*>(memory(c.madr,1)),scratchpad+(c.sadr&0x3ff0u),16);
                c.madr+=16; c.sadr=(c.sadr+16)&0x3ff0u;
            }
            qwords_transferred+=c.qwc; c.qwc=0; c.chcr&=~0x100u; return n;
        }
        if(n>4 && n!=9) { stop_reason="dma_channel_unsupported "+std::to_string(n); throw std::runtime_error(stop_reason); }
        (void)dir;
        if(mode==0) {                                              // normal
            deliver(n,c.madr,c.qwc); c.madr+=c.qwc*16; c.qwc=0;
        } else if(mode==1) {                                       // source chain
            for(int guard=0;guard<1000000;++guard) {
                if(c.qwc) { deliver(n,c.madr,c.qwc); c.madr+=c.qwc*16; c.qwc=0; }
                bool end=nextTag(n);
                if(c.qwc) { deliver(n,c.madr,c.qwc); c.madr+=c.qwc*16; c.qwc=0; }
                if(end) break;
                if(guard==999999) { stop_reason="dma_chain_runaway"; throw std::runtime_error(stop_reason); }
            }
        } else { stop_reason="dma_mode_unsupported "+std::to_string(mode); throw std::runtime_error(stop_reason); }
        c.chcr&=~0x100u;                                           // STR off: transfer complete
        return n;
    }
};
}
