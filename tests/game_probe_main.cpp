// Whole-program run: every translated original function is registered; execution
// continues until the first unsupported service, device or missing body.
#include "initialization_probe.h"
#include "emu_clock.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
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
        auto disc=std::filesystem::path(argv[1]).parent_path().string(); // ELF sits in the extracted disc root
        mp2r::RuntimeCallbacks callbacks;
        callbacks.profile=std::getenv("MP2R_PROFILE")!=nullptr;
        uint64_t pad_samples=0,cross_samples=0;
        // Explicit headless input experiment: pulse Cross for one second every four
        // seconds of emulated time. The original pad drivers and game consume it.
        if(std::getenv("MP2R_TEST_CROSS")) {
            auto start=mp2r::EmuClock::now();
            callbacks.poll_pad=[start,&pad_samples,&cross_samples]{
                ++pad_samples;
                auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(mp2r::EmuClock::now()-start).count();
                mp2r::GamePadInput input;
                if(elapsed>=1000 && elapsed%4000>=1000 && elapsed%4000<2000) { input.pressed=0x4000; input.pressure[6]=255; ++cross_samples; }
                return input;
            };
            std::cout<<"TEST INPUT: Cross pulses through SIO2 (emulated clock)\n";
        }
        // Optional host-side controller file: hex button mask, then RX RY LX LY
        // bytes. Missing/invalid masks release buttons rather than retaining them.
        if(const char* path=std::getenv("MP2R_PAD_FILE")) {
            callbacks.poll_pad=[path=std::string(path),&pad_samples,&cross_samples]{
                mp2r::GamePadInput pad; unsigned bits=0,axes[4]={128,128,128,128};
                std::ifstream file(path); if(file>>std::hex>>bits) {
                    pad.pressed=uint16_t(bits);
                    for(unsigned n=0;n<4;++n) if(file>>std::dec>>axes[n]) pad.sticks[n]=uint8_t(std::min(255u,axes[n]));
                }
                static constexpr uint16_t masks[]={0x20,0x80,0x10,0x40,0x1000,0x2000,0x4000,0x8000,0x400,0x800,0x100,0x200};
                for(unsigned n=0;n<12;++n) if(pad.pressed&masks[n]) pad.pressure[n]=255;
                ++pad_samples; if(pad.pressed&0x4000) ++cross_samples;
                return pad;
            };
            std::cout<<"TEST INPUT: controller file through SIO2\n";
        }
        auto result=mp2r::runInitialization(elf.data(),elf.size(),std::cout,argv[2],0x60000,4000000000u,budget,disc,callbacks);
        if(callbacks.poll_pad) std::cout<<"TEST INPUT samples="<<pad_samples<<" cross_pressed_samples="<<cross_samples<<'\n';
        std::cout<<"RESULT reason="<<result.reason<<" pc=0x"<<std::hex<<result.pc<<" source=0x"<<result.source
                 <<" address=0x"<<result.memory_address<<std::dec<<" steps="<<result.functions<<'\n';
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
