#pragma once
#include <cstdint>
namespace mp2r {
// PAL field timing from the native clock: 50 fields/s; VBLANK_START (INTC2) opens
// the blanking interval and VBLANK_END (INTC3) closes it. The interval length is
// an MP2R approximation of the PAL blanking lines, not a measured GS value.
class EeVblank {
public:
    static constexpr uint64_t field_ns=20000000,blank_ns=1600000;
    enum Event : unsigned { Start=1u<<2, End=1u<<3 };
private:
    uint64_t position_{};
    uint64_t fields_{};
public:
    // Returns the INTC bits raised while advancing; several fields may elapse at once.
    unsigned advance(uint64_t ns) {
        unsigned raised=0;
        while(ns) {
            uint64_t boundary=position_<blank_ns?blank_ns:field_ns;
            uint64_t step=boundary-position_;
            if(ns<step) { position_+=ns; break; }
            ns-=step; position_=boundary;
            if(position_==blank_ns) raised|=End;
            else { position_=0; ++fields_; raised|=Start; }
        }
        return raised;
    }
    bool inBlank() const { return position_<blank_ns; }
    uint64_t fields() const { return fields_; }
    bool oddField() const { return fields_&1; }
};
}
