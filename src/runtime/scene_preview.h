#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
namespace mp2r {
// Temporary PAL preview compensation for the observed inverted 512-square
// scene output. This is not GS hardware behaviour. Movie buffers (FRAME mode)
// and raw diagnostic dumps retain their original orientation.
inline bool compensateScenePreview(std::vector<uint32_t>& pixels,uint32_t w,uint32_t h,
                                  uint64_t smode2,uint64_t xyoffset) {
    if(w!=512 || h!=512 || (smode2&3)!=1 || xyoffset!=0x700000007000ull || pixels.size()!=size_t(w)*h) return false;
    for(uint32_t y=0;y<h/2;++y)
        std::swap_ranges(pixels.begin()+size_t(y)*w,pixels.begin()+size_t(y+1)*w,pixels.begin()+size_t(h-1-y)*w);
    return true;
}
}
