#pragma once
#include "iop_kernel.h"
#include <map>
#include <vector>
namespace mp2r {
// Native libsd (SPU2 driver library) for IOP modules. Holds parameter/switch/address
// state per core and voice, performs voice/block transfers into a 2MB sound RAM and
// raises the registered transfer interrupt handler on completion. Mixing and audio
// output are not implemented yet; mixing-dependent queries stop explicitly.
class IopLibsd {
public:
    std::vector<uint8_t> spu_ram=std::vector<uint8_t>(0x200000);
    std::map<uint32_t,uint32_t> params,switches,addresses,core_attr;
    std::array<uint32_t,2> trans_handler{},trans_arg{};
    uint32_t spu2_handler{},spu2_arg{};
    std::array<uint32_t,2> trans_status{};
    uint64_t transfers{},bytes{};

    void install(IopKernel& kernel) {
        auto add=[&](int index,IopKernel::Hle fn){ kernel.hle[{"libsd",index}]=std::move(fn); };
        add(4,[this](IopKernel& k){ params.clear(); switches.clear(); addresses.clear(); k.ret(0); });             // sceSdInit
        add(5,[this](IopKernel& k){ params[k.arg(0)&0xffff]=k.arg(1)&0xffff; k.ret(0); });                     // sceSdSetParam
        add(6,[this](IopKernel& k){ k.ret(params[k.arg(0)&0xffff]); });                                         // sceSdGetParam
        add(7,[this](IopKernel& k){ switches[k.arg(0)&0xffff]=k.arg(1); k.ret(0); });                           // sceSdSetSwitch
        add(8,[this](IopKernel& k){
            // ENDX/voice status reports would require mixing the voices; stop rather than guess.
            uint32_t entry=k.arg(0)&0xffff;
            if((entry>>8)==0x17) { k.stop("libsd_voice_status_requires_mixer "+std::to_string(entry)); return; } // SD_S_ENDX
            k.ret(switches[entry]);
        });
        add(9,[this](IopKernel& k){ addresses[k.arg(0)&0xffff]=k.arg(1); k.ret(0); });                         // sceSdSetAddr
        add(10,[this](IopKernel& k){ k.ret(addresses[k.arg(0)&0xffff]); });                                     // sceSdGetAddr
        add(11,[this](IopKernel& k){ core_attr[k.arg(0)&0xffff]=k.arg(1); k.ret(0); });                         // sceSdSetCoreAttr
        add(12,[this](IopKernel& k){ k.ret(core_attr[k.arg(0)&0xffff]); });                                     // sceSdGetCoreAttr
        add(17,[this](IopKernel& k){                                                                            // sceSdVoiceTrans
            uint32_t channel=k.arg(0)&1,mode=k.arg(1),iop=k.arg(2),spu=k.arg(3)&0x1fffff,size=k.arg(4);
            size=std::min<uint32_t>(size,uint32_t(spu_ram.size())-spu);
            if(mode&1) std::memcpy(k.sif.ram(iop,size),spu_ram.data()+spu,size);   // SD_TRANS_READ
            else std::memcpy(spu_ram.data()+spu,k.sif.ram(iop,size),size);
            ++transfers; bytes+=size; trans_status[channel]=1;
            if(trans_handler[channel]) k.callGuest(trans_handler[channel],{channel,trans_arg[channel]});
            k.ret(size);
        });
        add(19,[this](IopKernel& k){ k.ret(trans_status[k.arg(0)&1]); });                                      // sceSdVoiceTransStatus
        add(23,[](IopKernel& k){ k.ret(0); });                                                                  // sceSdSetEffectAttr (no reverb yet)
        add(26,[this](IopKernel& k){ uint32_t c=k.arg(0)&1; k.ret(trans_handler[c]); trans_handler[c]=k.arg(1); trans_arg[c]=k.arg(2); });
        add(27,[this](IopKernel& k){ k.ret(spu2_handler); spu2_handler=k.arg(0); spu2_arg=k.arg(1); });
    }
};
}
