#include "ee_timer3.h"
#include "ee_intc.h"
#include <iostream>
#include <stdexcept>
void check(bool value) { if(!value) throw std::runtime_error("EE timer/INTC contract mismatch"); }
int main() {
    constexpr uint32_t base=0x10001000;
    mp2r::EeTimerRegisters timer(base);
    check(timer.read(base)==0 && timer.read(base+0x10)==0 && timer.read(base+0x20)==0xffff);
    check(!timer.advanceBusCycles(1000000) && timer.read(base)==0);
    timer.write(base+0x20,3); timer.write(base+0x10,0x181);
    check(!timer.advanceBusCycles(47) && timer.read(base)==2);
    check(timer.advanceBusCycles(1) && timer.read(base)==3 && timer.read(base+0x10)==0x581);
    check(!timer.advanceBusCycles(16)); // flag stays latched until W1C
    timer.write(base+0x10,0x581); check(timer.read(base+0x10)==0x181);
    timer.write(base,0xffff); timer.write(base+0x10,0x282);
    check(!timer.advanceBusCycles(255) && timer.read(base)==0xffff);
    check(timer.advanceBusCycles(1) && timer.read(base)==0 && timer.read(base+0x10)==0xa82);
    timer.write(base+0x10,0xa82); check(timer.read(base+0x10)==0x282);
    timer.write(base,0); timer.write(base+0x20,5); timer.write(base+0x10,0x1c0);
    check(timer.advanceBusCycles(17) && timer.read(base)==2); // reset on target, three periods
    timer.write(base+0x10,0); auto frozen=timer.read(base);
    check(!timer.advanceBusCycles(9999) && timer.read(base)==frozen);
    timer.write(base+0x10,0x83);
    bool refused=false; try { timer.advanceBusCycles(1); } catch(const std::runtime_error&) { refused=true; }
    check(refused && timer.needsVideoClock());
    mp2r::EeIntc intc;
    int first=intc.add(11,0x1174c0,0,7),last=intc.add(11,0x123400,-1,8);
    int middle=intc.add(11,0x123440,last,9);
    check(first>0 && middle>0 && last>0 && intc.head(11)==first);
    check(intc.get(first)->next==middle && intc.get(middle)->next==last && intc.get(last)->next==0);
    check(intc.add(12,0x123480,first,0)==-1 && intc.add(15,0x123480,0,0)==-1 && intc.add(11,3,0,0)==-1);
    check(intc.remove(12,middle)==-1 && intc.remove(11,middle)==middle && intc.get(first)->next==last);
    intc.raise(11); check(intc.status()==0x800 && !intc.pending());
    intc.toggleMask(0x800); check(intc.pending()==0x800);
    intc.acknowledge(0x400); check(intc.pending()==0x800);
    intc.acknowledge(0x800); check(!intc.pending() && intc.mask()==0x800);
    intc.toggleMask(0x800); check(intc.mask()==0);
    std::cout<<"PASS: timer reset/dividers/fraction/target/overflow/W1C/zero-return/video refusal; INTC ordering/removal/mask/acknowledgement\n";
}
