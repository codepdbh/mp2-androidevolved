#include "iop_kernel.h"
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* message){ if(!ok) throw std::runtime_error(message); }
int main() {
    mp2r::IopSif sif; mp2r::IopKernel kernel(sif);
    uint32_t base=sif.allocate(256);
    uint32_t program[]={0xac042000,0x8ca80000,0x8d090000,0xac092004,0xac1c2008,0x24020001,0x03e00008,0};
    std::memcpy(sif.ram(base,sizeof program),program,sizeof program);
    kernel.resident_modules[12]={base,256,base,base+128,true,false};
    auto freeBefore=sif.totalFree();
    auto result=kernel.stopModule(12,std::string("one\0two\0",8));
    check(result==std::pair<int,int>{0,1},"removable entry accepts stop");
    check(int32_t(kernel.read(0x2000,4))==-3,"stop entry receives negative argc including other");
    check(kernel.read(0x2004,4)==0x6568746f,"stop argv begins with other");
    check(kernel.read(0x2008,4)==base+128,"stop entry uses module GP");
    check(sif.totalFree()==freeBefore,"temporary loader stack and stop arguments are released");
    check(kernel.stopModule(12,{}).first==-208,"repeated stop reports KE_ALREADY_STOPPED");
    check(kernel.stopModule(99,{}).first==-202,"unknown module reports KE_UNKNOWN_MODULE");
    kernel.resident_modules[13]={base,256,base,base+128,false,false};
    check(kernel.stopModule(13,{}).first==-211,"non removable module is not executed");
    kernel.resident_modules[14]={base,256,base,base+128,true,false};
    kernel.write(base+5*4,0x24020000,4);
    check(kernel.stopModule(14,{}).first==-209 && !kernel.resident_modules[14].stopped,"resident return refuses stop");
    kernel.write(base+5*4,0x24020001,4);
    kernel.resident_modules[15]={base,256,base,base+128,true,false};
    bool slept=false;
    kernel.ticks.push_back([&](mp2r::IopKernel& k){
        if(!slept && k.cpu.pc==base+4) {
            slept=true; k.cpu.r[4]=500;
            k.hle.at({"thbase",33})(k); // DelayThread needs a valid current thread
        }
    });
    auto before=mp2r::EmuClock::now();
    check(kernel.stopModule(15,{}).first==0 && slept,"loader entry resumes after blocking");
    check(mp2r::EmuClock::now()-before>=std::chrono::microseconds(500),"loader waits advance IOP time");
    kernel.reset(); check(kernel.resident_modules.empty(),"reboot discards module lifecycle state");
    check(kernel.stop_reason.empty(),"module lifecycle stopped the runtime");
    std::cout<<"PASS: real stop entry, negative argc, argv/GP, errors, argument lifetime and reset\n";
}
