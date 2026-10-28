#include "gs_renderer.h"
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* message){ if(!ok) throw std::runtime_error(message); }
int main(){
    mp2r::GsRenderer fast,reference; reference.fast_clears=false;
    for(size_t n=0;n<fast.vram.size();++n) fast.vram[n]=reference.vram[n]=uint32_t(n*0x1234567u);
    auto draw=[](mp2r::GsRenderer& gs,uint32_t mask,uint64_t test,unsigned x,unsigned y,unsigned w,unsigned h,bool fba){
        gs.write(0x1a,1); gs.write(0,6); gs.write(0x4c,32|(2ull<<16)|(uint64_t(mask)<<32));
        gs.write(0x4e,1ull<<32); gs.write(0x47,test); gs.write(0x4a,fba);
        gs.write(0x40,(127ull<<16)|(127ull<<48));
        gs.write(1,0x70402010); gs.write(5,(uint64_t(y*16)<<16)|(x*16));
        gs.write(5,(uint64_t((y+h)*16)<<16)|((x+w)*16));
    };
    for(uint32_t mask:{0u,0xff000000u,0x00f00f00u,0xffffffffu}) {
        draw(fast,mask,0x30000,8,16,32,24,true); draw(reference,mask,0x30000,8,16,32,24,true);
        check(fast.vram==reference.vram && fast.pixels==reference.pixels,"aligned clear differs from pixel pipeline");
    }
    check(fast.fast_clear_blocks==48,"expected aligned block clears");
    auto blocks=fast.fast_clear_blocks;
    draw(fast,0,0x30000,9,17,31,23,false); draw(reference,0,0x30000,9,17,31,23,false);
    check(fast.vram==reference.vram && fast.fast_clear_blocks==blocks,"unaligned rectangle must use pixel pipeline");
    draw(fast,0,0x30001,8,16,32,24,false); draw(reference,0,0x30001,8,16,32,24,false);
    check(fast.vram==reference.vram && fast.fast_clear_blocks==blocks,"alpha test must use pixel pipeline");
    // IMAGE/HWREG data crosses row and word boundaries, including packed
    // nibbles and the 24-bit stream's partial pixel across successive words.
    for(auto format:{mp2r::GsRenderer::CT32,mp2r::GsRenderer::CT24,mp2r::GsRenderer::CT16,mp2r::GsRenderer::T8,mp2r::GsRenderer::T4}) {
        mp2r::GsRenderer gs;
        unsigned bpp=mp2r::GsRenderer::bitsPerPixel(format),width=7,height=3;
        gs.write(0x50,(uint64_t(1)<<48)|(uint64_t(format)<<56));
        gs.write(0x51,(uint64_t(3)<<32)|(uint64_t(5)<<48));
        gs.write(0x52,width|(uint64_t(height)<<32)); gs.write(0x53,0);
        std::array<uint8_t,96> source{};
        for(unsigned n=0;n<source.size();++n) source[n]=uint8_t(n*37+11);
        gs.image(source.data(),unsigned(source.size()));
        for(unsigned n=0;n<width*height;++n) {
            uint32_t expected=0;
            if(bpp==4) expected=(source[n/2]>>((n%2)*4))&15;
            else for(unsigned j=0;j<bpp/8;++j) expected|=uint32_t(source[n*(bpp/8)+j])<<(j*8);
            check(gs.readPixel(format,0,1,3+n%width,5+n/width)==expected,"IMAGE transfer pixel order");
        }
        check(gs.stop_reason.empty(),"IMAGE transfer stopped");
    }
    {
        mp2r::GsRenderer gs;
        gs.write(0x1a,1); gs.write(0x4c,1ull<<16); gs.write(0x4e,1ull<<32);
        gs.write(0x40,(3ull<<16)|(3ull<<48)); gs.write(0x42,0x44);
        auto pixel=[&](uint64_t prim,uint64_t test,uint32_t color) {
            gs.write(0,prim); gs.write(0x47,test); gs.write(1,color);
            gs.write(5,0); gs.write(5,(16ull<<16)|16);
        };
        gs.writePixel(gs.CT32,0,1,0,0,0x80800000);
        pixel(6|(1<<6),0,0x40000000|0x80);
        check(gs.readPixel(gs.CT32,0,1,0,0)==0x40400040,"blending retains destination colour");
        gs.writePixel(gs.CT32,0,1,0,0,0x80000080);
        pixel(6,0x4000,0x800000ff);
        check(gs.readPixel(gs.CT32,0,1,0,0)==0x80000080,"destination alpha test reads old alpha");
        pixel(6,0xc000,0x800000ff);
        check(gs.readPixel(gs.CT32,0,1,0,0)==0x800000ff,"destination alpha test passes");
    }
    std::cout<<"PASS: GS block clear matches pixel pipeline with masks/FBA and safe fallbacks\n";
}
