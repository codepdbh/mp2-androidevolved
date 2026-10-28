#pragma once
#include "emu_clock.h"
#include "iop_kernel.h"
#include <chrono>
#include <map>
#include <vector>
namespace mp2r {
// Native libsd (SPU2 driver library) for IOP modules. Holds parameter/switch/address
// state per core and voice, performs voice/block transfers into a 2MB sound RAM and
// raises the registered transfer interrupt handler on completion. Voices advance through
// SPU RAM on the 48 kHz clock: 28 samples per 16-byte ADPCM block at PITCH/0x1000, applying
// the block flags (bit 2 loop start -> LSA, bit 0 end -> ENDX, with bit 1 repeat from LSA,
// otherwise the voice stops). KON restarts at SSA and clears ENDX; KOFF only starts the
// envelope release, so addressing continues. Block transfers feed the
// core's sound data input (ADMA) at 48 kHz stereo 16-bit, 192000 bytes/s: a loop transfer
// raises the handler at each half of its buffer. Mixing and audio output are not
// implemented yet; mixing-dependent queries stop explicitly.
class IopLibsd {
public:
    std::vector<uint8_t> spu_ram=std::vector<uint8_t>(0x200000);
    std::map<uint32_t,uint32_t> params,switches,addresses,core_attr;
    std::array<uint32_t,2> trans_handler{},trans_arg{};
    uint32_t spu2_handler{},spu2_arg{};
    std::array<uint32_t,2> trans_status{};
    uint64_t transfers{},bytes{};
    struct Block { bool active{},loop{}; uint32_t base{},size{},half{}; uint64_t halves{}; std::chrono::steady_clock::time_point start{}; };
    std::array<Block,2> blocks{};
    static constexpr uint64_t adma_bytes_per_second=48000*4;
    struct Voice { bool on{}; uint32_t ssa{},lsa{},nax{},pitch{}; double pos{}; };
    std::array<std::array<Voice,24>,2> voices{};
    std::array<uint32_t,2> endx{};
    std::chrono::steady_clock::time_point voice_clock=mp2r::EmuClock::now();
    void keyOn(uint32_t core,uint32_t mask) {
        for(uint32_t v=0;v<24;++v) if(mask&(1u<<v)) { auto& x=voices[core][v]; x.on=true; x.nax=x.ssa&0x1ffff0u; x.pos=0; endx[core]&=~(1u<<v); }
    }
    void advanceVoices(double samples) {
        for(uint32_t c=0;c<2;++c) for(uint32_t v=0;v<24;++v) {
            auto& x=voices[c][v];
            if(!x.on) continue;
            x.pos+=samples*double(std::min<uint32_t>(x.pitch,0x3fff))/4096.0;
            for(int guard=0;x.on && x.pos>=28 && guard<100000;++guard) {
                x.pos-=28;
                if(irq_armed[c] && (irqa[c]&0x1ffff0u)==x.nax) { irq_armed[c]=false; irq_pending|=1u<<c; }   // voice read IRQA
                uint8_t flags=spu_ram[(x.nax+1)&0x1fffff];
                if(flags&4) x.lsa=x.nax;
                if(flags&1) { endx[c]|=1u<<v; if(flags&2) x.nax=x.lsa&0x1ffff0u; else { x.on=false; break; } }
                else x.nax=(x.nax+16)&0x1ffff0u;
            }
        }
    }
    // SPU2 IRQ: a voice of the core reading the block at IRQA (SD_ADDR_IRQA) with SD_C_IRQ_ENABLE
    // set raises the interrupt once; software re-arms it by setting IRQ_ENABLE again. The SPU2
    // also writes its capture areas every sample (per core CD L/R and voice 1/3 output, 0x200
    // halfwords each, halfword addresses 0x000-0xFFF, one ring position per 48 kHz sample), so an
    // IRQA inside them fires once per 512 samples.
    uint64_t capture_samples{};
    double capture_fraction{};
    std::array<uint32_t,2> irqa{};
    std::array<bool,2> irq_armed{};
    uint32_t irq_pending{};
    std::array<bool,2> voice_trans_pending{};
    unsigned trace_count{},status_trace{};
    void finishVoiceTrans(IopKernel& k,uint32_t c) {
        voice_trans_pending[c]=false; trans_status[c]=1;
        if(k.log && ++trace_count<=60) *k.log<<"libsd VoiceTrans done ch="<<c<<" handler=0x"<<std::hex<<trans_handler[c]<<std::dec<<"\n";
        if(trans_handler[c]) k.callGuest(trans_handler[c],{c,trans_arg[c]});
    }
    void tick(IopKernel& k) {
        for(uint32_t c=0;c<2;++c) if(voice_trans_pending[c]) finishVoiceTrans(k,c);
        auto now=mp2r::EmuClock::now();
        double samples=double(std::chrono::duration_cast<std::chrono::nanoseconds>(now-voice_clock).count())*48000e-9;
        advanceVoices(samples);
        voice_clock=now;
        uint64_t before=capture_samples; capture_samples=uint64_t(capture_fraction+=samples);
        for(uint32_t c=0;c<2;++c) {
            uint32_t word=(irqa[c]>>1)&0xfffff;
            if(!irq_armed[c] || word>=0x1000) continue;
            uint64_t offset=word&0x1ff;                                // ring position written at sample n: n % 512
            uint64_t next=before-(before%512)+offset; if(next<before) next+=512;
            if(next<capture_samples) { irq_armed[c]=false; irq_pending|=1u<<c; }
        }
        if(irq_pending && k.log && ++trace_count<=200) *k.log<<"libsd SPU2 IRQ cores="<<irq_pending<<'\n';
        if(irq_pending) { uint32_t cores=irq_pending; irq_pending=0; if(spu2_handler) k.callGuest(spu2_handler,{cores,spu2_arg}); }
        for(uint32_t c=0;c<2;++c) {
            auto& b=blocks[c];
            if(!b.active || b.size<2) continue;
            uint64_t ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(now-b.start).count());
            uint64_t due=ns*adma_bytes_per_second/(uint64_t(b.size/2)*1000000000ull);
            while(b.active && b.halves<due && k.stop_reason.empty()) {
                ++b.halves; b.half^=1; bytes+=b.size/2;
                if(!b.loop && b.halves>=2) b.active=false;
                if(b.loop || !b.active) { trans_status[c]=1; if(trans_handler[c]) k.callGuest(trans_handler[c],{c,trans_arg[c]}); }
            }
        }
    }

    void install(IopKernel& kernel) {
        auto add=[&](int index,IopKernel::Hle fn){ kernel.hle[{"libsd",index}]=std::move(fn); };
        kernel.ticks.push_back([this](IopKernel& k){ tick(k); });
        add(4,[this](IopKernel& k){ params.clear(); switches.clear(); addresses.clear(); k.ret(0); });             // sceSdInit
        add(5,[this](IopKernel& k){                                                                             // sceSdSetParam
            uint32_t entry=k.arg(0)&0xffff,value=k.arg(1)&0xffff; params[entry]=value;
            if((entry&0xff00)==0x0200) voices[entry&1][(entry>>1)&31].pitch=value;   // SD_VP_PITCH
            k.ret(0);
        });
        add(6,[this](IopKernel& k){ k.ret(params[k.arg(0)&0xffff]); });                                         // sceSdGetParam
        add(7,[this](IopKernel& k){                                                                             // sceSdSetSwitch
            uint32_t entry=k.arg(0)&0xffff; switches[entry]=k.arg(1);
            tick(k);                                                                   // voices up to date before KON
            if((entry&0xff00)==0x1500) keyOn(entry&1,k.arg(1)&0xffffff);              // SD_S_KON
            if(k.log && ++trace_count<=200 && (entry&0xff00)==0x1500 && k.arg(1)) *k.log<<"libsd KON core "<<(entry&1)<<" mask=0x"<<std::hex<<k.arg(1)<<std::dec<<'\n';
            k.ret(0);
        });
        add(8,[this](IopKernel& k){
            uint32_t entry=k.arg(0)&0xffff;
            if((entry>>8)==0x17) { tick(k); k.ret(endx[entry&1]); return; }          // SD_S_ENDX
            k.ret(switches[entry]);
        });
        add(9,[this](IopKernel& k){                                                                             // sceSdSetAddr
            uint32_t entry=k.arg(0)&0xffff,value=k.arg(1)&0x1fffff; addresses[entry]=value;
            auto& x=voices[entry&1][(entry>>1)&31];
            if((entry&0xffc0)==0x2000 && ((entry>>1)&31)<24) x.ssa=value;            // SD_VA_SSA  (0x2000 + param<<6 | voice<<1 | core)
            if((entry&0xffc0)==0x2040 && ((entry>>1)&31)<24) x.lsa=value;            // SD_VA_LSAX
            if((entry&0xff00)==0x1f00) irqa[entry&1]=value;                           // SD_ADDR_IRQA
            if(k.log && ++trace_count<=200 && ((entry&0xff00)==0x1f00 || (entry&0xff00)==0x2000)) *k.log<<"libsd SetAddr 0x"<<std::hex<<entry<<" = 0x"<<value<<std::dec<<'\n';
            k.ret(0);
        });
        add(10,[this](IopKernel& k){                                                                            // sceSdGetAddr
            uint32_t entry=k.arg(0)&0xffff;
            if((entry&0xffc0)==0x2080 && ((entry>>1)&31)<24) { tick(k); k.ret(voices[entry&1][(entry>>1)&31].nax); return; }   // SD_VA_NAX
            k.ret(addresses[entry]);
        });
        add(11,[this](IopKernel& k){                                                                            // sceSdSetCoreAttr
            uint32_t entry=k.arg(0)&0xffff; core_attr[entry]=k.arg(1);
            if((entry&~1u)==4) { tick(k); irq_armed[entry&1]=k.arg(1)!=0; }          // SD_C_IRQ_ENABLE
            k.ret(0);
        });
        add(12,[this](IopKernel& k){ k.ret(core_attr[k.arg(0)&0xffff]); });                                     // sceSdGetCoreAttr
        add(17,[this](IopKernel& k){                                                                            // sceSdVoiceTrans
            uint32_t channel=k.arg(0)&1,mode=k.arg(1),iop=k.arg(2),spu=k.arg(3)&0x1fffff,size=k.arg(4);
            size=std::min<uint32_t>(size,uint32_t(spu_ram.size())-spu);
            if(mode&1) std::memcpy(k.sif.ram(iop,size),spu_ram.data()+spu,size);   // SD_TRANS_READ
            else std::memcpy(spu_ram.data()+spu,k.sif.ram(iop,size),size);
            ++transfers; bytes+=size; trans_status[channel]=0;
            // SD_TRANS_BY_IO (bit 3) is a synchronous PIO copy without interrupt; DMA transfers
            // raise the transfer interrupt after the call returns.
            if(mode&8) trans_status[channel]=1; else voice_trans_pending[channel]=true;
            if(k.log && ++trace_count<=60) *k.log<<"libsd VoiceTrans ch="<<channel<<" mode=0x"<<std::hex<<mode<<" spu=0x"<<spu<<" size=0x"<<size<<std::dec<<"\n";
            k.ret(size);
        });
        add(18,[this](IopKernel& k){                                                                            // sceSdBlockTrans
            uint32_t channel=k.arg(0)&1,mode=k.arg(1)&0xffff,iop=k.arg(2),size=k.arg(3);
            auto& b=blocks[channel];
            if(k.log) *k.log<<"libsd BlockTrans ch="<<channel<<" mode=0x"<<std::hex<<mode<<" iop=0x"<<iop<<" size=0x"<<size<<std::dec<<'\n';
            switch(mode&0xf) {
                case 2: k.ret(b.active?(b.base+b.half*(b.size/2))&0xffffff:0); b.active=false; return;   // SD_TRANS_MODE_STOP
                case 0: case 3: break;                                                          // WRITE / WRITE_FROM (start address arg 4)
                default: k.stop("libsd_block_trans_mode "+std::to_string(mode)); return;     // READ / WRITE_FROM
            }
            if(mode&~0x1fu) { k.stop("libsd_block_trans_mode "+std::to_string(mode)); return; }
            b={true,(mode&0x10)!=0,iop,size,0,0,mp2r::EmuClock::now()};
            if((mode&0xf)==3 && size>=2 && k.arg(4)-iop>=size/2 && k.arg(4)-iop<size) b.half=1;   // starts in the second half
            trans_status[channel]=0; ++transfers;
            k.ret(0);
        });
        add(19,[this](IopKernel& k){                                                                            // sceSdVoiceTransStatus
            uint32_t c=k.arg(0)&1;
            if(voice_trans_pending[c] && k.arg(1)==1) finishVoiceTrans(k,c);           // SD_TRANS_STATUS_WAIT
            k.ret(voice_trans_pending[c]?0:1);
        });
        add(20,[this](IopKernel& k){                                                                            // sceSdBlockTransStatus
            tick(k);
            const auto& b=blocks[k.arg(0)&1];
            k.ret(b.active?(b.half<<24)|((b.base+b.half*(b.size/2))&0xffffff):0);
            if(k.log && (++status_trace<=40 || status_trace%2000==0)) *k.log<<"libsd BlockTransStatus #"<<status_trace<<" -> 0x"<<std::hex<<k.cpu.r[2]<<std::dec<<" halves="<<b.halves<<'\n';
        });
        add(23,[](IopKernel& k){ k.ret(0); });                                                                  // sceSdSetEffectAttr (no reverb yet)
        add(26,[this](IopKernel& k){ uint32_t c=k.arg(0)&1; k.ret(trans_handler[c]); trans_handler[c]=k.arg(1); trans_arg[c]=k.arg(2); });
        add(27,[this](IopKernel& k){ k.ret(spu2_handler); spu2_handler=k.arg(0); spu2_arg=k.arg(1); });
        add(28,[this](IopKernel& k){ k.ret(trans_arg[k.arg(0)&1]); });                                         // sceSdGetTransIntrHandlerArgument
        add(29,[this](IopKernel& k){ k.ret(spu2_arg); });                                                       // sceSdGetSpu2IntrHandlerArgument
    }
};
}
