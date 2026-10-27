#include "ee_tlb.h"
#include <iostream>
#include <stdexcept>
void check(bool value) { if(!value) throw std::runtime_error("EE TLB contract mismatch"); }
int main() {
    mp2r::EeTlb tlb;
    mp2r::EeTlbRegisters r;
    r.entryhi=0; tlb.probe(r); check(r.index==0x80000000u); // unwritten slots never match
    // ExpandScratchPad shape from the copied PAL payload: 4KB pair at 0x70004000, G set in both halves.
    r={1,47,1,0,0x70004000,(0x00100000>>6)|0x1f,(0x00101000>>6)|0x1f};
    check(tlb.writeIndexed(r));
    r={}; r.entryhi=0x70004000; tlb.probe(r); check(r.index==1);
    r.entryhi=0x70004017; tlb.probe(r); check(r.index==1); // global ignores ASID
    r.entryhi=0x70006000; tlb.probe(r); check(r.index&0x80000000u);
    check(tlb.read(r) && r.entryhi==0x70004000 && r.entrylo0==0x401f && r.entrylo1==0x405f && r.pagemask==0);
    // Reserved bits are dropped; S survives only in EntryLo0; ASID gates non-global entries.
    r={2,47,1,0xffffffff,0xffffffff,0xffffffff,0xffffffff};
    check(tlb.writeIndexed(r));
    const auto& e=tlb.get(2);
    check(e.pagemask==0x01ffe000 && e.entryhi==0xffffe0ff && e.entrylo0==0x83fffffe && e.entrylo1==0x03fffffe && e.global);
    r={3,47,1,0,0x00200005,0x2,0x2}; check(tlb.writeIndexed(r) && !tlb.get(3).global);
    r.entryhi=0x00200005; tlb.probe(r); check(r.index==3);
    r.entryhi=0x00200006; tlb.probe(r); check(r.index&0x80000000u);
    // A 16MB PageMask widens the compared VPN2 range.
    r={4,47,1,0x01ffe000,0x10000000,0x3,0x3}; check(tlb.writeIndexed(r));
    r.entryhi=0x10fff000; tlb.probe(r); check(r.index==4);
    // Random stays in [Wired,47] and wraps deterministically.
    r={0,2,2,0,0x20000000,0x3,0x3}; check(tlb.writeRandom(r) && r.random==47 && tlb.get(2).entryhi==0x20000000);
    r.entryhi=0x20002000; check(tlb.writeRandom(r) && r.random==46 && tlb.get(47).entryhi==0x20002000);
    r.random=0; r.entryhi=0x20004000; check(tlb.writeRandom(r) && tlb.get(47).entryhi==0x20004000);
    r.wired=48; check(!tlb.writeRandom(r));
    r.index=48; check(!tlb.writeIndexed(r) && !tlb.read(r));
    std::cout<<"PASS: TLB indexed/random writes, read-back, masks, ASID/global/PageMask probes, invalid index refusal\n";
}
