#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#ifdef USE_SSE2NEON
#include <sse2neon.h>
#else
#include <immintrin.h>
#endif
namespace mp2r {
// VU0 comparison idiom: VSUB to immutable VF0, followed by CFC2 MAC.
// Preserve VF0 while producing the flags consumed by the original geometry code.
template<class Context>
inline void vu0CompareSub(Context* cpu,__m128 left,__m128 right,unsigned mask) {
    alignas(16) uint32_t a[4],b[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(a),_mm_castps_si128(left));
    _mm_store_si128(reinterpret_cast<__m128i*>(b),_mm_castps_si128(right));
    auto number=[](uint32_t v)->double {
        unsigned exp=(v>>23)&255;
        if(!exp) return std::copysign(0.0,(v>>31)?-1.0:1.0);
        if(exp==255) return double(std::bit_cast<float>(v-0x00800000u))*2;
        return double(std::bit_cast<float>(v));
    };
    uint32_t mac=0;
    for(unsigned lane=0;lane<4;++lane) if(mask&(8u>>lane)) {
        double difference=number(a[lane])-number(b[lane]),magnitude=std::fabs(difference);
        unsigned flag=3-lane;
        if(std::signbit(difference)) mac|=1u<<(4+flag);
        if(magnitude<0x1p-126) { mac|=1u<<flag; if(magnitude!=0) mac|=1u<<(8+flag); }
        if(magnitude>=0x1p128) mac|=1u<<(12+flag);
    }
    cpu->vu0_mac_flags=mac;
    uint16_t status=uint16_t((mac&15?1:0)|(mac&0xf0?2:0)|(mac&0xf00?4:0)|(mac&0xf000?8:0));
    cpu->vu0_status=uint16_t((cpu->vu0_status&0xff0)|status|(status<<6));
}
}
