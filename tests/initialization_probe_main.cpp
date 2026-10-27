#include "initialization_probe.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
int main(int argc,char** argv) {
    if(argc!=2) { std::cerr<<"Usage: initialization_probe <verified PAL ELF>\n"; return 2; }
    try {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<uint8_t> elf((std::istreambuf_iterator<char>(file)),{});
        for(uint32_t base:{0x60000u,0x50000u}) {
            auto result=mp2r::runInitialization(elf.data(),elf.size(),std::cout,base);
            if(result.reason!="Unimplemented syscall 0x4b" || result.semaphores!=3 || result.pc!=0x10d698 || !result.alarm_patch_installed ||
               result.threads!=2 || result.context_switches!=2 || result.semaphore_waits!=1) return 1;
        }
        auto budget=mp2r::runInitialization(elf.data(),elf.size(),std::cout,0x60000,32);
        if(budget.reason!="checkpoint_budget" || budget.semaphores!=0) return 1;
        std::cout<<"PASS: original helper thread started, blocked on its semaphore and restored main context; precise stop at GetOsdConfigParam. No gameplay.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
