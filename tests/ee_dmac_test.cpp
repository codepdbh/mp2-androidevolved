#include "ee_dmac.h"
#include <iostream>
#include <stdexcept>
#include <vector>
void check(bool value,const char* what) { if(!value) throw std::runtime_error(what); }
int main() {
    std::vector<uint8_t> ram(0x2000000),spr(0x4000);
    mp2r::EeDmac dmac; dmac.ram=ram.data(); dmac.scratchpad=spr.data(); dmac.ctrl=1;
    std::vector<uint32_t> seen; int tags=0;
    dmac.sinks[2]=[&](const uint8_t* q,uint32_t n,uint32_t,uint32_t,bool has_tag){
        if(has_tag) { ++tags; return; }
        for(uint32_t i=0;i<n;++i) { uint32_t v; std::memcpy(&v,q+i*16,4); seen.push_back(v); }
    };
    auto qw=[&](uint32_t addr,uint32_t a,uint32_t b){ std::memcpy(&ram[addr],&a,4); std::memcpy(&ram[addr+4],&b,4); };
    auto data=[&](uint32_t addr,uint32_t v){ std::memcpy(&ram[addr],&v,4); };
    // 0x1000: cnt qwc=2 (data 11,12) ; 0x1030: ref qwc=1 -> 0x5000 (data 21) ; 0x1040: call qwc=1 (data 31) -> 0x2000
    // 0x2000: cnt qwc=1 (data 41) ; 0x2020: ret qwc=1 (data 51) ; back to 0x1060: end qwc=1 (data 61)
    qw(0x1000,0x10000002,0); data(0x1010,11); data(0x1020,12);
    qw(0x1030,0x30000001,0x5000); data(0x5000,21);
    qw(0x1040,0x50000001,0x2000); data(0x1050,31);
    qw(0x2000,0x10000001,0); data(0x2010,41);
    qw(0x2020,0x60000001,0); data(0x2030,51);
    qw(0x1060,0x70000001,0); data(0x1070,61);
    dmac.write(0x1000a030,0x1000);
    int done=dmac.write(0x1000a000,0x105);                // STR | chain mode | to peripheral
    check(done==2 && !(dmac.read(0x1000a000)&0x100),"chain completes and clears STR");
    check((seen==std::vector<uint32_t>{11,12,21,31,41,51,61}),"chain order with call/ret");
    check(((dmac.read(0x1000a000)>>4)&3)==0,"call stack unwound");
    // Normal mode and TTE (VIF channel receives the tag).
    seen.clear(); data(0x3000,7); data(0x3010,8);
    dmac.write(0x1000a010,0x3000); dmac.write(0x1000a020,2);
    check(dmac.write(0x1000a000,0x101)==2 && (seen==std::vector<uint32_t>{7,8}) && dmac.read(0x1000a010)==0x3020,"normal mode");
    dmac.sinks[1]=dmac.sinks[2];
    qw(0x4000,0x70000000,0); dmac.write(0x10009030,0x4000);
    dmac.write(0x10009000,0x145); check(tags==1,"TTE forwards the tag");
    std::cout<<"PASS: DMAC normal/source-chain (cnt ref call ret end), ASR stack, STR completion, TTE\n";
}
