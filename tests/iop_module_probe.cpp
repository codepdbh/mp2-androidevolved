// Private-data check: an original disc IRX loads, relocates, links and runs its threads
// on the MP2R IOP kernel until its RPC server waits for requests.
#include "iop_kernel.h"
#include "iop_modules.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
int main(int argc,char** argv) {
    if(argc!=3) { std::cerr<<"Usage: iop_module_probe <module.irx> <expected sid hex>\n"; return 2; }
    try {
        std::ifstream f(argv[1],std::ios::binary);
        std::vector<uint8_t> irx((std::istreambuf_iterator<char>(f)),{});
        std::vector<uint8_t> ee(0x2000000);
        std::ostringstream log;
        mp2r::IopSif sif; sif.log=&log; mp2r::registerResidentIopServices(sif);
        mp2r::IopKernel kernel(sif); kernel.log=&log;
        kernel.ee_memory=[&](uint32_t a){ return ee.data()+a; };
        bool resident=kernel.loadModule(irx,argv[1],"");
        kernel.run(5000000);
        std::cout<<log.str();
        uint32_t sid=uint32_t(std::stoul(argv[2],nullptr,16));
        if(!kernel.stop_reason.empty()) { std::cerr<<"stop: "<<kernel.stop_reason<<" pc=0x"<<std::hex<<kernel.cpu.pc<<'\n'; return 1; }
        if(!resident || !sif.hasServer(sid) || !kernel.idle()) { std::cerr<<"module did not reach its RPC loop\n"; return 1; }
        std::cout<<"PASS: "<<argv[1]<<" relocated, linked and serving sid 0x"<<std::hex<<sid<<std::dec
                 <<" after "<<kernel.instructions()<<" IOP instructions\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
