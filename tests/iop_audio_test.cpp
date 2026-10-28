#include "iop_libsd.h"
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* message){ if(!ok) throw std::runtime_error(message); }
int main(){
    mp2r::IopSif sif; mp2r::IopKernel kernel(sif); mp2r::IopLibsd sd; sd.install(kernel);
    uint32_t base=sif.allocate(2048);
    for(unsigned half=0;half<2;++half) for(unsigned n=0;n<256;++n) {
        int16_t l=int16_t(half*1000+n),r=int16_t(-int(l)-1);
        std::memcpy(sif.ram(base+half*1024+n*2,2),&l,2);
        std::memcpy(sif.ram(base+half*1024+512+n*2,2),&r,2);
    }
    std::vector<int16_t> output; unsigned calls=0;
    sd.stream_pcm=[&](unsigned core,const int16_t* pcm,uint32_t frames){
        check(core==0 && frames==256,"stream format/core"); ++calls; output.insert(output.end(),pcm,pcm+frames*2);
    };
    auto transfer=[&](uint32_t mode){ kernel.cpu.r[4]=0; kernel.cpu.r[5]=mode; kernel.cpu.r[6]=base; kernel.cpu.r[7]=2048; kernel.hle.at({"libsd",18})(kernel); };
    transfer(0x10);
    mp2r::EmuClock::advance(5400000); sd.tick(kernel);
    check(calls==1 && output[0]==0 && output[1]==-1 && output[510]==255 && output[511]==-256,"consumed first half is deinterleaved before refill");
    mp2r::EmuClock::advance(5400000); sd.tick(kernel);
    check(calls==2 && output[512]==1000 && output[513]==-1001 && sd.streamed_frames==512,"second half and frame count");
    transfer(2); mp2r::EmuClock::advance(10000000); sd.tick(kernel);
    check(calls==2,"stopped stream emits no PCM");
    std::cout<<"PASS: AutoDMA planar stereo halves, ordering, frame count and stop\n";
}
