#include "ps2_runtime_macros.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

// Narrow host adapter for this pre-kernel slice. CPU context, instruction macros
// and RAM stores remain upstream implementations. No guest instruction is patched.
class MP2BootProbeRuntime {
public:
    unsigned interval=4096,checkpoints=0,yields=0;
    bool eeCheckpointDue(uint32_t=32) noexcept {
        ++checkpoints;
        if(checkpoints%interval==0) { ++yields; return true; }
        return false;
    }
    static bool isSpecialAddress(uint32_t a) { return Ps2IsSpecialAddress(a); }
    void Store8(uint8_t*,R5900Context*,uint32_t,uint8_t) {
        throw std::runtime_error("Unexpected special-address byte write in prefix");
    }
    void Store128(uint8_t*,R5900Context*,uint32_t,__m128i) {
        throw std::runtime_error("Unexpected special-address vector write in prefix");
    }
};

// Bind generated platform calls to the declared test adapter; the generated
// function body is included verbatim from its private output directory.
#define PS2Runtime MP2BootProbeRuntime
#include "mp2_boot_prefix_0x100008.cpp"
#undef PS2Runtime

static void check(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
int main() {
    constexpr uint32_t begin=0x5c0d00,end=0x6e4c00,stop=0x1001c4;
    for(unsigned interval:{1u,4096u}) {
        std::vector<uint8_t> ram(32*1024*1024,0xcc);
        std::fill(ram.begin()+begin,ram.begin()+end,0xa5);
        R5900Context ctx{}; auto* cpu=&ctx;
        for(unsigned i=1;i<32;++i) ctx.r[i]=_mm_set1_epi32(0x12345678);
        for(float& f:ctx.f) f=3.25f;
        ctx.hi=ctx.lo=ctx.hi1=ctx.lo1=~uint64_t(0); ctx.sa=17; ctx.fcr31=0xffffffff;
        ctx.pc=0x100008;
        MP2BootProbeRuntime host; host.interval=interval;
        unsigned calls=0;
        while(ctx.pc!=stop && calls<100000) {
            mp2_boot_prefix_0x100008(ram.data(),cpu,&host); ++calls;
        }
        check(ctx.pc==stop,"Prefix failed to reach pre-syscall boundary");
        check(std::all_of(ram.begin()+begin,ram.begin()+end,[](auto b){return b==0;}),"BSS not zero");
        check(std::all_of(ram.begin(),ram.begin()+begin,[](auto b){return b==0xcc;}),"Write below BSS");
        check(std::all_of(ram.begin()+end,ram.end(),[](auto b){return b==0xcc;}),"Write above BSS");
        check(GPR_U32(cpu,28)==0x5c8870,"Wrong GP");
        check(GPR_U32(cpu,5)==0x01fc0000 && GPR_U32(cpu,6)==0x40000,"Wrong stack arguments");
        check(GPR_U32(cpu,7)==0x5c1300 && GPR_U32(cpu,8)==0x100220,"Wrong startup arguments");
        check(GPR_U32(cpu,29)==0 && GPR_U32(cpu,31)==0,"SP/RA clear failed");
        check(GPR_U32(cpu,26)==0x12345678 && GPR_U32(cpu,27)==0x12345678,"Kernel registers changed");
        check(ctx.hi==0 && ctx.lo==0 && ctx.hi1==0 && ctx.lo1==0 && ctx.sa==0 && ctx.fcr31==0,"Special registers not cleared");
        for(float f:ctx.f) check(f==0,"FPR not cleared");
        check(!ctx.in_delay_slot,"Delay slot state leaked");
        std::cout<<"PASS: original startup prefix, BSS bytes="<<end-begin
                 <<" GP=0x5c8870 interval="<<interval<<" resumes="<<calls-1<<". Stops before kernel.\n";
    }
}
