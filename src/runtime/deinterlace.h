#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace mp2r {
// Blend the latest two interlaced fields for a stable progressive preview.
// Raw GS dumps remain unfiltered. Motion may ghost; this is a blend deinterlacer.
class FieldBlend {
    std::array<std::vector<uint32_t>,2> fields_;
    std::vector<uint32_t> output_;
    uint32_t width_{},height_{};
    bool have_previous_{},previous_odd_{};
public:
    const std::vector<uint32_t>& process(const std::vector<uint32_t>& pixels,uint32_t width,uint32_t height,bool interlaced,bool odd) {
        if(!interlaced || width!=width_ || height!=height_) {
            fields_[0].clear(); fields_[1].clear(); width_=width; height_=height;
            have_previous_=false;
        }
        if(!interlaced) return pixels;
        // Presentation may skip an arbitrary number of emulated fields. A
        // repeated parity must not reuse an opposite field from an old scene.
        if(have_previous_ && odd==previous_odd_) fields_[odd?0:1].clear();
        previous_odd_=odd; have_previous_=true;
        fields_[odd?1:0]=pixels;
        if(fields_[0].size()!=pixels.size() || fields_[1].size()!=pixels.size()) return pixels;
        output_.resize(pixels.size());
        for(size_t n=0;n<pixels.size();++n) {
            uint32_t a=fields_[0][n],b=fields_[1][n];
            output_[n]=((a&b)+(((a^b)&0x00fefefeu)>>1))|0xff000000u;
        }
        return output_;
    }
};
}
