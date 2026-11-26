#include "frame_rate.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
int main() {
    FrameRateCounter rate;
    assert(rate.value(0)==0);
    for(unsigned i=0;i<=60;++i)rate.presented(uint64_t(i)*1000000000ull/60);
    assert(std::fabs(rate.value(1000000000ull)-60)<0.01f);
    assert(rate.value(4000000000ull)==0);
    rate.presented(4000000000ull);
    assert(rate.value(4000000000ull)==0);
    for(unsigned i=1;i<=30;++i)rate.presented(4000000000ull+uint64_t(i)*1000000000ull/30);
    assert(std::fabs(rate.value(5000000000ull)-30)<0.01f);
    rate.presented(1); // clock discontinuity resets the sampling window
    assert(rate.value(1)==0);
}
