#include "ee_semaphores.h"
#include "kernel_syscall_table.h"
#include <vector>
#include <cstring>
#include <iostream>
#include <stdexcept>
void check(bool ok) { if(!ok) throw std::runtime_error("EE semaphore contract mismatch"); }
int main() {
    mp2r::EeSemaphores semas;
    check(semas.create(-1,1,0,0)==-1 && semas.create(2,1,0,0)==-1 && semas.create(0,0,0,0)==-1);
    int first=semas.create(1,3,0x10,0x22),second=semas.create(0,1,0,7);
    check(first>0 && second>0 && first!=second);
    auto* object=semas.find(first);
    check(object && object->count==1 && object->maximum==3 && object->initial==1 && object->attributes==0x10 && object->option==0x22);
    check(!semas.find(0) && !semas.find(-1) && !semas.find(257));
    for(unsigned i=2;i<256;++i) check(semas.create(0,1,0,0)>0);
    check(semas.size()==256 && semas.create(0,1,0,0)==-1);
    std::vector<uint8_t> ram(0x100000,0);
    mp2r::KernelSyscallTable table;
    table.install(ram.data(),0x83,0x116730);
    uint32_t raw=0; std::memcpy(&raw,ram.data()+table.physicalBase()+0x83*4,4);
    check(raw==0x116730 && table.handler(0x83)==raw && table.virtualBase()==0x80060000);
    table.install(ram.data(),0x83,0); check(table.handler(0x83)==0);
    table.install(ram.data(),0x12c,0x80076488);
    check(table.timer3InterruptHandler()==0x80076488 && table.handler(0x12c)==0);
    bool refused=false; try { table.install(ram.data(),256,0x116730); } catch(const std::runtime_error&) { refused=true; } check(refused);
    refused=false; try { mp2r::KernelSyscallTable invalid(0xfffffff0); } catch(const std::runtime_error&) { refused=true; } check(refused);
    std::cout<<"PASS: EE semaphore validation/unique IDs/metadata/exhaustion; native syscall table install/remove/bounds\n";
}
