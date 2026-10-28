#include "gs_gif.h"
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
void check(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
int main() {
    mp2r::GsGif gif;
    std::vector<std::pair<uint8_t,uint64_t>> writes;
    gif.write_register=[&](uint8_t r,uint64_t v){ writes.emplace_back(r,v); };
    auto feed=[&](uint64_t lo,uint64_t hi){ uint64_t q[2]={lo,hi}; gif.feed(reinterpret_cast<const uint8_t*>(q),1); };
    auto packed=[&](uint8_t descriptor,uint64_t lo,uint64_t hi){ feed(1ull|(1ull<<60),descriptor); feed(lo,hi); };
    uint64_t xy=0x1234ull|(0xabcdull<<32),z=0x789abcde;
    uint64_t xyz=0x1234ull|(0xabcdull<<16)|(z<<32);
    packed(5,xy,z); check(writes.back()==std::make_pair(uint8_t(5),xyz),"XYZ2 packed fields");
    packed(0xd,xy,z); check(writes.back()==std::make_pair(uint8_t(0xd),xyz),"XYZ3 packed fields and suppressed kick");
    packed(5,xy,z|(1ull<<47)); check(writes.back()==std::make_pair(uint8_t(0xd),xyz),"XYZ2 ADC suppresses kick");
    uint64_t zf=(0x123456ull<<4)|(0x9aull<<36);
    uint64_t xyzf=0x1234ull|(0xabcdull<<16)|(0x123456ull<<32)|(0x9aull<<56);
    packed(4,xy,zf); check(writes.back()==std::make_pair(uint8_t(4),xyzf),"XYZF2 packed fog/depth");
    packed(0xc,xy,zf); check(writes.back()==std::make_pair(uint8_t(0xc),xyzf),"XYZF3 packed fog/depth and suppressed kick");
    // REGLIST uses compact 64-bit GS data instead of PACKED expansion.
    feed(1ull|(1ull<<58)|(1ull<<60),0xd); feed(xyz,0);
    check(writes.back()==std::make_pair(uint8_t(0xd),xyz),"XYZ3 REGLIST remains compact");
    // ST supplies Q to subsequent PACKED RGBA within a tag, including split DMA feeds.
    feed(2ull|(1ull<<60),2); feed(0,0x3e800000); feed(0,0x3f000000);
    packed(1,0,0);
    check(writes.back().second==0x3f80000000000000ull,"new GIFtag resets temporary Q to one");
    feed(1ull|(2ull<<60),0x12); feed(0,0x3e800000); feed(0,0);
    check(writes.back().second==0x3e80000000000000ull,"ST Q reaches RGBA in the same tag across feeds");
    auto count=writes.size();
    feed((1ull<<46)|(7ull<<47),0);
    check(writes.size()==count,"empty tag ignores PRE");
    feed(1ull|(1ull<<46)|(7ull<<47)|(1ull<<58)|(1ull<<60),0xf); feed(0,0);
    check(writes.size()==count,"REGLIST ignores PRE");
    check(gif.stop_reason.empty(),"GIF stopped unexpectedly");
    std::cout<<"PASS: PACKED XYZ2/3 and XYZF2/3 coordinate/depth/fog/ADC; compact REGLIST\n";
}
