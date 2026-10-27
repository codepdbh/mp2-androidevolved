#include "initialization_probe.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
int main(int argc,char** argv) {
    if(argc!=3) { std::cerr<<"Usage: initialization_probe <verified PAL ELF> <SYSTEM.CNF BOOT2 path>\n"; return 2; }
    try {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<uint8_t> elf((std::istreambuf_iterator<char>(file)),{});
        for(uint32_t base:{0x60000u,0x50000u}) {
            auto result=mp2r::runInitialization(elf.data(),elf.size(),std::cout,argv[2],base);
            if(result.reason!="missing_function" || result.pc!=0x42f7e0 || result.source!=0x100208 || result.semaphores!=3 ||
               !result.alarm_patch_installed || !result.tlb_services_installed || !result.main_entry_verified ||
               result.threads!=2 || result.context_switches!=2 || result.semaphore_waits!=1) return 1;
        }
        auto budget=mp2r::runInitialization(elf.data(),elf.size(),std::cout,argv[2],0x60000,32);
        if(budget.reason!="checkpoint_budget" || budget.semaphores!=0) return 1;
        std::cout<<"PASS: original InitSys completed (OSD config, TLB services) and crt0 reached main() with disc boot arguments. No gameplay.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
