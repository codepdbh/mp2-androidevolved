#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace mp2r {
// R5900 joint TLB: 48 even/odd page pairs. Contents start invalid; MP2R does not
// reproduce mappings that a commercial BIOS would leave behind.
struct EeTlbRegisters { uint32_t index{},random{47},wired{},pagemask{},entryhi{},entrylo0{},entrylo1{}; };
class EeTlb {
public:
    static constexpr unsigned size=48;
    struct Entry { uint32_t pagemask{},entryhi{},entrylo0{},entrylo1{}; bool global{},written{}; };
private:
    std::array<Entry,size> entries_{};
    static constexpr uint32_t probe_failure=0x80000000u;
    bool store(unsigned slot,const EeTlbRegisters& r) {
        if(slot>=size) return false;
        entries_[slot]={r.pagemask&0x01ffe000u,r.entryhi&0xffffe0ffu,r.entrylo0&0x83fffffeu,r.entrylo1&0x03fffffeu,
                        (r.entrylo0&r.entrylo1&1u)!=0,true};
        return true;
    }
public:
    const Entry& get(unsigned slot) const { return entries_.at(slot); }
    bool writeIndexed(const EeTlbRegisters& r) { return store(r.index&0x3fu,r); }
    // Hardware decrements Random every cycle; MP2R advances it once per TLBWR so
    // traces stay deterministic. Callers locate the slot with TLBP, as the PAL code does.
    bool writeRandom(EeTlbRegisters& r) {
        if(r.wired>=size) return false;
        if(r.random<r.wired || r.random>=size) r.random=size-1;
        if(!store(r.random,r)) return false;
        r.random=r.random==r.wired?size-1:r.random-1;
        return true;
    }
    bool read(EeTlbRegisters& r) const {
        auto slot=r.index&0x3fu;
        if(slot>=size) return false;
        const auto& e=entries_[slot];
        r.pagemask=e.pagemask; r.entryhi=e.entryhi;
        r.entrylo0=e.entrylo0|(e.global?1u:0u); r.entrylo1=e.entrylo1|(e.global?1u:0u);
        return true;
    }
    std::optional<unsigned> find(uint32_t entryhi) const {
        for(unsigned slot=0;slot<size;++slot) {
            const auto& e=entries_[slot];
            if(!e.written) continue;
            auto vpn2=~(e.pagemask|0x1fffu);
            if(((e.entryhi^entryhi)&vpn2)==0 && (e.global || ((e.entryhi^entryhi)&0xffu)==0)) return slot;
        }
        return std::nullopt;
    }
    void probe(EeTlbRegisters& r) const {
        if(auto slot=find(r.entryhi)) r.index=*slot;
        else r.index|=probe_failure;
    }
};
}
