#include "iop_kernel.h"
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
int main() {
    mp2r::IopSif sif; mp2r::IopKernel kernel(sif);
    std::vector<uint8_t> ee(0x10000);
    kernel.ee_memory=[&](uint32_t a){ return ee.data()+a; };
    // Synthetic IOP callback stores a0 and gp, then returns. No disc code needed.
    uint32_t code[]={0x34081800,0xad040000,0xad1c0004,0x03e00008,0};
    for(unsigned i=0;i<5;++i) kernel.write(0x1000+i*4,code[i],4);
    kernel.write(0x2000,0x11223344,4);
    kernel.write(0x2100,0x2000,4); kernel.write(0x2104,0x3000,4); kernel.write(0x2108,4,4);
    auto dma=[&](int index){
        kernel.cpu.r[4]=0x2100; kernel.cpu.r[5]=1; kernel.cpu.r[6]=0x1000; kernel.cpu.r[7]=0xabcdef;
        kernel.hle.at({"sifman",index})(kernel);
    };
    kernel.cpu.r[4]=0x1900; kernel.hle.at({"intrman",17})(kernel);
    check(kernel.read(0x1900,4)==1,"suspend saves enabled state");
    kernel.cpu.r[28]=0x12345678; dma(32);
    uint32_t copied; std::memcpy(&copied,ee.data()+0x3000,4);
    check(copied==0x11223344 && kernel.cpu.r[2]==1,"DMA data and transfer ID");
    kernel.run(1); check(kernel.read(0x1800,4)==0,"callback deferred while interrupts disabled");
    kernel.cpu.r[28]=0xbad; kernel.cpu.r[4]=kernel.read(0x1900,4); kernel.hle.at({"intrman",18})(kernel);
    kernel.run(1);
    check(kernel.read(0x1800,4)==0xabcdef && kernel.read(0x1804,4)==0x12345678,"callback argument and registered GP");
    check(kernel.cpu.r[28]==0xbad && kernel.dma_completions_delivered==1,"caller context restored");
    kernel.run(1); check(kernel.dma_completions_delivered==1,"completion delivered once");
    kernel.write(0x1800,0,4); dma(7); kernel.run(1);
    check(kernel.read(0x1800,4)==0 && kernel.dma_completions_delivered==1,"plain DMA has no callback");
    dma(32); kernel.reset(); kernel.run(1);
    check(kernel.read(0x1800,4)==0 && kernel.dma_completions_delivered==1,"reboot discards pending callbacks");
    check(kernel.stop_reason.empty(),"IOP callback stopped");
    std::cout<<"PASS: SIF DMA completion after copy/resume, argument/GP/context, single delivery/plain DMA/reset\n";
}
