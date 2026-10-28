#include "gs_renderer.h"
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
int main() {
    mp2r::GsRenderer gs;
    constexpr uint64_t tex2mask=(uint64_t(0x3f)<<20)|(~uint64_t(0)<<37);
    for(unsigned context=0;context<2;++context) {
        const uint64_t initial=0x123456789abcdef0ull,update=0xfedcba9876543210ull;
        gs.write(uint8_t(6+context),initial); gs.write(uint8_t(0x16+context),update);
        check(gs.regs[6+context]==((initial&~tex2mask)|(update&tex2mask)),"TEX2 updates PSM and palette fields only");
    }
    gs.writePixel(gs.CT32,0,1,0,0,0x800000ff);
    gs.writePixel(gs.CT32,32,1,0,0,0x80ff0000);
    std::vector<uint32_t> pixels; uint32_t w{},h{};
    auto frame=[&](uint64_t mode,uint64_t d1=0,uint64_t d2=0,uint32_t bg=0){
        check(gs.frame(mode,1ull<<9,d1,(1ull<<9)|1,d2,pixels,w,h,bg),"display rejected");
    };
    frame(0x8021); check(pixels[0]==0xff0000ff,"first circuit only");
    frame(2); check(pixels[0]==0xffff0000,"second circuit only");
    frame(0x8023); check(pixels[0]==0xff0000ff,"fixed alpha 128 selects first circuit");
    frame(0x23); check(pixels[0]==0xffff0000,"fixed alpha zero selects second circuit");
    frame(0x4023); check(pixels[0]==0xff7f007f,"fixed alpha mixes circuits");
    frame(3); check(pixels[0]==0xff0000ff,"pixel alpha 128 selects first circuit");
    gs.writePixel(gs.CT32,0,1,0,0,0x000000ff);
    frame(3); check(pixels[0]==0xffff0000,"transparent first circuit");
    frame(0x80a3,0,0,0x0000ff00); check(pixels[0]==0xff0000ff,"SLBG with fixed full alpha");
    frame(0xa3,0,0,0x0000ff00); check(pixels[0]==0xff00ff00,"SLBG selects background");
    frame(0x8021,(3ull<<32)|(3ull<<44)|(1ull<<23)|(1ull<<27));
    check(w==2 && h==2,"horizontal and vertical magnification");
    for(uint32_t y=0;y<4;++y) gs.writePixel(gs.CT32,0,1,0,y,0x80000010+y);
    check(gs.frame(0x8021,1ull<<9,6ull<<44,0,0,pixels,w,h,0,3) && h==4,"interlaced FRAME halves read height, rounding up");
    check(pixels[0]==0xff000010 && pixels[3]==0xff000013,"interlaced FRAME reads consecutive source rows");
    check(gs.frame(0x8021,1ull<<9,6ull<<44,0,0,pixels,w,h,0,1) && h==7,"FIELD mode retains full framebuffer height");
    check(!gs.frame(0,0,0,0,0,pixels,w,h),"both circuits disabled");
    {
        mp2r::GsRenderer texture;
        texture.write(0x1a,1); texture.write(0x4c,1ull<<16); texture.write(0x4e,1ull<<32);
        texture.write(0x40,(3ull<<16)|(3ull<<48));
        texture.write(6,32|(1ull<<14)|(1ull<<26)|(1ull<<30)|(1ull<<34)|(1ull<<35));
        texture.write(8,5); // clamp both coordinates
        texture.writePixel(texture.CT32,32,1,0,0,0x80000080);
        texture.writePixel(texture.CT32,32,1,1,0,0x80008000);
        texture.writePixel(texture.CT32,32,1,0,1,0x80800000);
        texture.writePixel(texture.CT32,32,1,1,1,0x80808080);
        auto sample=[&](uint64_t filtering,uint32_t uv) {
            texture.write(0x14,filtering); texture.write(0,6|0x110); texture.write(3,uv|(uint64_t(uv)<<16));
            texture.write(5,0); texture.write(5,(16ull<<16)|16);
            return texture.readPixel(texture.CT32,0,1,0,0);
        };
        check(sample(0,16)==0x80808080,"nearest selects the addressed texel");
        check(sample(0x60,16)==0x80404040,"bilinear interpolates four texel centres including alpha");
        check(sample(0x60,8)==0x80000080,"bilinear texel centre is exact");
        check(sample(0x60,0)==0x80000080,"bilinear clamps each neighbour at texture boundary");
    }
    std::cout<<"PASS: GS read circuits, PMODE fixed/pixel alpha/background and display magnification\n";
}
