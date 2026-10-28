#include "vu0_compare.h"
#include <iostream>
#include <stdexcept>
struct Cpu { uint32_t vu0_mac_flags{}; uint16_t vu0_status{}; };
void check(bool ok,const char* message){ if(!ok) throw std::runtime_error(message); }
int main(){
    Cpu cpu;
    mp2r::vu0CompareSub(&cpu,_mm_set_ps(1,2,3,4),_mm_set_ps(1,3,2,5),15);
    check(cpu.vu0_mac_flags==0xa1,"mixed signs, equal W: MAC X/Z sign and W zero");
    check((cpu.vu0_status&0xcf)==0xc3,"current and sticky zero/sign status");
    mp2r::vu0CompareSub(&cpu,_mm_set1_ps(1),_mm_setzero_ps(),8);
    check(cpu.vu0_mac_flags==0 && cpu.vu0_status==0xc0,"masked lanes cleared, sticky status retained");
    auto smallest=_mm_castsi128_ps(_mm_set1_epi32(0x00800001));
    auto normal=_mm_castsi128_ps(_mm_set1_epi32(0x00800000));
    mp2r::vu0CompareSub(&cpu,smallest,normal,8);
    check(cpu.vu0_mac_flags==0x808,"underflow marks U and Z");
    mp2r::vu0CompareSub(&cpu,_mm_castsi128_ps(_mm_set1_epi32(0x7f7fffff)),_mm_castsi128_ps(_mm_set1_epi32(0xff7fffff)),8);
    check(cpu.vu0_mac_flags==0x8000,"overflow marks O");
    std::cout<<"PASS: original VSUB-to-VF0 comparison flags, masks, sticky status, underflow and overflow\n";
}
