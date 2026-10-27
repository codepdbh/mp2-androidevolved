// Whole-program run: every translated original function is registered; execution
// continues until the first unsupported service, device or missing body.
#include "initialization_probe.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
int main(int argc,char** argv) {
    if(argc<3 || argc>4) { std::cerr<<"Usage: game_probe <verified PAL ELF> <SYSTEM.CNF BOOT2 path> [max executions]\n"; return 2; }
    try {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<uint8_t> elf((std::istreambuf_iterator<char>(file)),{});
        uint64_t budget=argc==4?std::strtoull(argv[3],nullptr,10):50000000ull;
        auto result=mp2r::runInitialization(elf.data(),elf.size(),std::cout,argv[2],0x60000,4000000000u,budget);
        std::cout<<"RESULT reason="<<result.reason<<" pc=0x"<<std::hex<<result.pc<<" source=0x"<<result.source
                 <<" address=0x"<<result.memory_address<<std::dec<<" steps="<<result.functions<<'\n';
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
