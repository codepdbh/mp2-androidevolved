#include "ee_vif.h"
#include <iostream>
#include <stdexcept>
#include <vector>
void check(bool value,const char* what) { if(!value) throw std::runtime_error(what); }
int main() {
    std::vector<uint8_t> code(0x4000),data(0x4000);
    mp2r::EeVif vif; vif.vu_code=code.data(); vif.code_size=0x4000; vif.vu_data=data.data(); vif.data_size=0x4000;
    std::vector<uint32_t> direct; unsigned starts=0;
    vif.direct=[&](const uint8_t* q,uint32_t n){ for(uint32_t i=0;i<n*4;++i) { uint32_t w; std::memcpy(&w,q+i*4,4); direct.push_back(w); } };
    vif.start_microprogram=[&](uint32_t){ ++starts; return true; };
    auto feed=[&](std::vector<uint32_t> w){ vif.feed(reinterpret_cast<const uint8_t*>(w.data()),uint32_t(w.size())); check(vif.stop_reason.empty(),vif.stop_reason.c_str()); };
    auto at=[&](uint32_t qw,int i){ uint32_t v; std::memcpy(&v,&data[qw*16+i*4],4); return v; };
    // UNPACK V4-32, 2 vectors at qword 4
    feed({0x6c020004,1,2,3,4,5,6,7,8});
    check(at(4,0)==1 && at(4,3)==4 && at(5,0)==5 && at(5,3)==8,"V4-32");
    // UNPACK S-8 signed, broadcast, 3 vectors at 10 (inputs padded to a word)
    feed({0x6203000a,0x0000ff02});
    check(at(10,0)==2 && at(10,3)==2 && at(11,1)==0xffffffff && at(12,2)==0,"S-8 sign extension");
    feed({0x6401000e,0x3e800000,0x3f000000});
    check(at(14,0)==at(14,2) && at(14,1)==at(14,3) && at(14,3)==0x3f000000,"V2-32 duplicates XY into ZW");
    feed({0x6501000f,0x8001ffff});
    check(at(15,0)==0xffffffff && at(15,1)==0xffff8001 && at(15,2)==at(15,0) && at(15,3)==at(15,1),"V2-16 signed duplication");
    feed({0x66014010,0x000080ff});
    check(at(16,0)==255 && at(16,1)==128 && at(16,2)==255 && at(16,3)==128,"V2-8 unsigned duplication");
    // Skipping write CL=2 WL=1: 3 vectors land at 20, 22, 24
    feed({0x01000102,0x6c030014,9,9,9,9,8,8,8,8,7,7,7,7});
    check(at(20,0)==9 && at(22,0)==8 && at(24,0)==7 && at(21,0)==0,"skipping write");
    // Filling write CL=1 WL=2 with COL: 4 vectors from 2 inputs -> 30=in,31=col,32=in,33=col
    feed({0x31000000,0xc0,0xc1,0xc2,0xc3,0x01000201,0x6c04001e,1,1,1,1,2,2,2,2});
    check(at(30,0)==1 && at(31,0)==0xc1 && at(32,0)==2 && at(33,2)==0xc1,"filling write uses COL");
    // Offset mode with ROW, CL=WL=1, and a mask protecting component 3
    feed({0x01000101,0x30000000,100,200,300,400,0x05000001,0x20000000,0x000000c0,0x7c010028,1,2,3,4});
    check(at(40,0)==101 && at(40,2)==303 && at(40,3)==0,"offset mode + write protect");
    // The same STMASK must be ignored when the UNPACK m bit is clear.
    feed({0x6c010029,1,2,3,4});
    check(at(41,0)==101 && at(41,2)==303 && at(41,3)==404,"unmasked UNPACK ignores STMASK");
    // MPG two doublewords at code 0x10; DIRECT one qword; double buffering
    feed({0x05000000,0x20000000,0,0x4a020002,0xa,0xb,0xc,0xd,0x50000001,0x11,0x22,0x33,0x44});
    uint32_t c; std::memcpy(&c,&code[0x10],4); check(c==0xa && direct.size()==4 && direct[3]==0x44,"MPG and DIRECT");
    feed({0x03000040,0x02000020,0x14000000,0x14000000});
    check(starts==2 && vif.top==0x60 && vif.tops==0x40,"BASE/OFFSET double buffering");
    std::cout<<"PASS: VIF UNPACK V4-32/S-8, skipping and filling cycles, offset+mask, MPG, DIRECT, TOPS double buffering\n";
}
