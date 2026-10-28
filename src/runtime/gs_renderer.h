#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <vector>
namespace mp2r {
// Software Graphics Synthesizer: 4 MB local memory with the GS page/block/column swizzles,
// host->local and local->local transfers, and the drawing pipeline (points, lines, triangles,
// strips, fans, sprites; Gouraud, Z, texture mapping with CLUT/TEXA/TFX, fog, alpha test,
// destination alpha test, alpha blending, FBMSK/ZMSK, scissor, FBA). Texture sampling is
// nearest-neighbour and dithering is not applied. The display circuit reads PMODE/DISPFB/DISPLAY.
class GsRenderer {
public:
    std::vector<uint32_t> vram=std::vector<uint32_t>(1024*1024);   // 4 MB as 32-bit words
    std::array<uint64_t,0x64> regs{};
    std::string stop_reason;
    uint64_t primitives{},pixels{};
    std::ostream* trace{}; uint64_t trace_from{}; unsigned trace_left{40};

    // ---------------- swizzles (word / halfword / byte / nibble addresses)
    static constexpr int block32[4][8]={{0,1,4,5,16,17,20,21},{2,3,6,7,18,19,22,23},{8,9,12,13,24,25,28,29},{10,11,14,15,26,27,30,31}};
    static constexpr int block32z[4][8]={{24,25,28,29,8,9,12,13},{26,27,30,31,10,11,14,15},{16,17,20,21,0,1,4,5},{18,19,22,23,2,3,6,7}};
    static constexpr int block16[8][4]={{0,2,8,10},{1,3,9,11},{4,6,12,14},{5,7,13,15},{16,18,24,26},{17,19,25,27},{20,22,28,30},{21,23,29,31}};
    static constexpr int block16s[8][4]={{0,2,16,18},{1,3,17,19},{8,10,24,26},{9,11,25,27},{4,6,20,22},{5,7,21,23},{12,14,28,30},{13,15,29,31}};
    static constexpr int block16z[8][4]={{24,26,16,18},{25,27,17,19},{28,30,20,22},{29,31,21,23},{8,10,0,2},{9,11,1,3},{12,14,4,6},{13,15,5,7}};
    static constexpr int block16sz[8][4]={{24,26,8,10},{25,27,9,11},{16,18,0,2},{17,19,1,3},{28,30,12,14},{29,31,13,15},{20,22,4,6},{21,23,5,7}};
    static constexpr int column32[8][8]={{0,1,4,5,8,9,12,13},{2,3,6,7,10,11,14,15},{16,17,20,21,24,25,28,29},{18,19,22,23,26,27,30,31},
        {32,33,36,37,40,41,44,45},{34,35,38,39,42,43,46,47},{48,49,52,53,56,57,60,61},{50,51,54,55,58,59,62,63}};
    static constexpr int column16[8][16]={{0,2,8,10,16,18,24,26,1,3,9,11,17,19,25,27},{4,6,12,14,20,22,28,30,5,7,13,15,21,23,29,31},
        {32,34,40,42,48,50,56,58,33,35,41,43,49,51,57,59},{36,38,44,46,52,54,60,62,37,39,45,47,53,55,61,63},
        {64,66,72,74,80,82,88,90,65,67,73,75,81,83,89,91},{68,70,76,78,84,86,92,94,69,71,77,79,85,87,93,95},
        {96,98,104,106,112,114,120,122,97,99,105,107,113,115,121,123},{100,102,108,110,116,118,124,126,101,103,109,111,117,119,125,127}};
    static constexpr int column8[16][16]={
        {0,4,16,20,32,36,48,52,2,6,18,22,34,38,50,54},{8,12,24,28,40,44,56,60,10,14,26,30,42,46,58,62},
        {33,37,49,53,1,5,17,21,35,39,51,55,3,7,19,23},{41,45,57,61,9,13,25,29,43,47,59,63,11,15,27,31},
        {96,100,112,116,64,68,80,84,98,102,114,118,66,70,82,86},{104,108,120,124,72,76,88,92,106,110,122,126,74,78,90,94},
        {65,69,81,85,97,101,113,117,67,71,83,87,99,103,115,119},{73,77,89,93,105,109,121,125,75,79,91,95,107,111,123,127},
        {128,132,144,148,160,164,176,180,130,134,146,150,162,166,178,182},{136,140,152,156,168,172,184,188,138,142,154,158,170,174,186,190},
        {161,165,177,181,129,133,145,149,163,167,179,183,131,135,147,151},{169,173,185,189,137,141,153,157,171,175,187,191,139,143,155,159},
        {224,228,240,244,192,196,208,212,226,230,242,246,194,198,210,214},{232,236,248,252,200,204,216,220,234,238,250,254,202,206,218,222},
        {193,197,209,213,225,229,241,245,195,199,211,215,227,231,243,247},{201,205,217,221,233,237,249,253,203,207,219,223,235,239,251,255}};
    static constexpr int column4[8][32]={
        {0,8,32,40,64,72,96,104,2,10,34,42,66,74,98,106,4,12,36,44,68,76,100,108,6,14,38,46,70,78,102,110},
        {16,24,48,56,80,88,112,120,18,26,50,58,82,90,114,122,20,28,52,60,84,92,116,124,22,30,54,62,86,94,118,126},
        {65,73,97,105,1,9,33,41,67,75,99,107,3,11,35,43,69,77,101,109,5,13,37,45,71,79,103,111,7,15,39,47},
        {81,89,113,121,17,25,49,57,83,91,115,123,19,27,51,59,85,93,117,125,21,29,53,61,87,95,119,127,23,31,55,63},
        {192,200,224,232,128,136,160,168,194,202,226,234,130,138,162,170,196,204,228,236,132,140,164,172,198,206,230,238,134,142,166,174},
        {208,216,240,248,144,152,176,184,210,218,242,250,146,154,178,186,212,220,244,252,148,156,180,188,214,222,246,254,150,158,182,190},
        {129,137,161,169,193,201,225,233,131,139,163,171,195,203,227,235,133,141,165,173,197,205,229,237,135,143,167,175,199,207,231,239},
        {145,153,177,185,209,217,241,249,147,155,179,187,211,219,243,251,149,157,181,189,213,221,245,253,151,159,183,191,215,223,247,255}};

    enum Psm : uint32_t { CT32=0x00,CT24=0x01,CT16=0x02,CT16S=0x0a,T8=0x13,T4=0x14,T8H=0x1b,T4HL=0x24,T4HH=0x2c,Z32=0x30,Z24=0x31,Z16=0x32,Z16S=0x3a };
    static bool known(uint32_t psm) {
        switch(psm) { case CT32: case CT24: case CT16: case CT16S: case T8: case T4: case T8H: case T4HL: case T4HH: case Z32: case Z24: case Z16: case Z16S: return true; }
        return false;
    }
    // Word address (32-bit formats); bp in 64-word blocks, bw in 64-pixel units.
    static uint32_t addr32(uint32_t bp,uint32_t bw,uint32_t x,uint32_t y,bool z=false) {
        uint32_t page=(y>>5)*std::max(1u,bw)+(x>>6);
        uint32_t block=uint32_t((z?block32z:block32)[(y>>3)&3][(x>>3)&7]);
        return (bp*64+page*2048+block*64+uint32_t(column32[y&7][x&7]))&0xfffff;
    }
    static uint32_t addr16(uint32_t bp,uint32_t bw,uint32_t x,uint32_t y,uint32_t psm) {   // halfword address
        uint32_t page=(y>>6)*std::max(1u,bw)+(x>>6);
        const auto& t=psm==CT16S?block16s:psm==Z16?block16z:psm==Z16S?block16sz:block16;
        uint32_t block=uint32_t(t[(y>>3)&7][(x>>4)&3]);
        return (bp*128+page*4096+block*128+uint32_t(column16[y&7][x&15]))&0x1fffff;
    }
    static uint32_t addr8(uint32_t bp,uint32_t bw,uint32_t x,uint32_t y) {          // byte address
        uint32_t page=(y>>6)*std::max(1u,bw>>1)+(x>>7);
        uint32_t block=uint32_t(block32[(y>>4)&3][(x>>4)&7]);
        return (bp*256+page*8192+block*256+uint32_t(column8[y&15][x&15]))&0x3fffff;
    }
    static uint32_t addr4(uint32_t bp,uint32_t bw,uint32_t x,uint32_t y) {          // nibble address
        uint32_t page=(y>>7)*std::max(1u,bw>>1)+(x>>7);
        uint32_t block=uint32_t(block16[(y>>4)&7][(x>>5)&3]);
        return (bp*512+page*16384+block*512+uint32_t(column4[y&7][x&31])+((y&8)?256u:0u))&0x7fffff;
    }
    uint16_t& half(uint32_t a) { return reinterpret_cast<uint16_t*>(vram.data())[a&0x1fffff]; }
    uint8_t& byte(uint32_t a) { return reinterpret_cast<uint8_t*>(vram.data())[a&0x3fffff]; }
    // Raw pixel value in the format's own width.
    uint32_t readPixel(uint32_t psm,uint32_t bp,uint32_t bw,uint32_t x,uint32_t y) {
        switch(psm) {
            case CT32: case Z32: return vram[addr32(bp,bw,x,y,psm==Z32)];
            case CT24: case Z24: return vram[addr32(bp,bw,x,y,psm==Z24)]&0xffffff;
            case CT16: case CT16S: case Z16: case Z16S: return half(addr16(bp,bw,x,y,psm));
            case T8: return byte(addr8(bp,bw,x,y));
            case T4: { uint32_t a=addr4(bp,bw,x,y); return (byte(a>>1)>>((a&1)*4))&15; }
            case T8H: return vram[addr32(bp,bw,x,y)]>>24;
            case T4HL: return (vram[addr32(bp,bw,x,y)]>>24)&15;
            case T4HH: return vram[addr32(bp,bw,x,y)]>>28;
        }
        return 0;
    }
    void writePixel(uint32_t psm,uint32_t bp,uint32_t bw,uint32_t x,uint32_t y,uint32_t v,uint32_t mask=0) {
        switch(psm) {
            case CT32: case Z32: { auto& w=vram[addr32(bp,bw,x,y,psm==Z32)]; w=(w&mask)|(v&~mask); return; }
            case CT24: case Z24: { auto& w=vram[addr32(bp,bw,x,y,psm==Z24)]; uint32_t m=mask|0xff000000u; w=(w&m)|(v&~m); return; }
            case CT16: case CT16S: case Z16: case Z16S: { auto& h=half(addr16(bp,bw,x,y,psm)); h=uint16_t((h&mask)|(v&~mask&0xffff)); return; }
            case T8: byte(addr8(bp,bw,x,y))=uint8_t(v); return;
            case T4: { uint32_t a=addr4(bp,bw,x,y); auto& b=byte(a>>1); b=uint8_t((a&1)?(b&0x0f)|((v&15)<<4):(b&0xf0)|(v&15)); return; }
            case T8H: { auto& w=vram[addr32(bp,bw,x,y)]; w=(w&0x00ffffffu)|(v<<24); return; }
            case T4HL: { auto& w=vram[addr32(bp,bw,x,y)]; w=(w&0xf0ffffffu)|((v&15)<<24); return; }
            case T4HH: { auto& w=vram[addr32(bp,bw,x,y)]; w=(w&0x0fffffffu)|((v&15)<<28); return; }
        }
    }
    static uint32_t bitsPerPixel(uint32_t psm) {
        switch(psm) { case CT32: case Z32: case T8H: case T4HL: case T4HH: return 32; case CT24: case Z24: return 24;
            case CT16: case CT16S: case Z16: case Z16S: return 16; case T8: return 8; case T4: return 4; }
        return 0;
    }

    // ---------------- register interface (GIF A+D / PACKED expansion / REGLIST)
    void write(uint8_t reg,uint64_t v) {
        if(!stop_reason.empty()) return;
        if(reg>=regs.size()) return;
        switch(reg) {
            case 0x00: regs[0]=v; vcount_=0; return;                                       // PRIM
            case 0x1b: prmode_=v; return;                                                  // PRMODE
            case 0x01: rgbaq_=v; return;
            case 0x02: st_=v; return;
            case 0x03: uv_=v; return;
            case 0x04: case 0x05: case 0x0c: case 0x0d: vertex(reg,v); return;
            case 0x53: regs[reg]=v; startTransfer(); return;                               // TRXDIR
            case 0x54: hwreg(v); return;                                                   // HWREG
            case 0x06: case 0x07: regs[reg]=v; if(((v>>61)&7)!=0) clut_dirty_=true; return; // TEX0 (CLD)
        }
        regs[reg]=v;
    }
    void image(const uint8_t* data,uint32_t bytes) { for(uint32_t i=0;i+8<=bytes;i+=8) { uint64_t v; std::memcpy(&v,data+i,8); hwreg(v); } }

    // ---------------- display: reads the enabled read circuit's frame buffer as RGBA8
    // pmode/dispfb/display: privileged registers (PMODE, DISPFB1/2, DISPLAY1/2).
    bool frame(uint64_t pmode,uint64_t dispfb1,uint64_t display1,uint64_t dispfb2,uint64_t display2,std::vector<uint32_t>& out,uint32_t& w,uint32_t& h) {
        bool en2=(pmode>>1)&1,en1=pmode&1;
        if(!en1 && !en2) return false;
        uint64_t fb=en2?dispfb2:dispfb1,disp=en2?display2:display1;
        uint32_t fbp=uint32_t(fb&0x1ff),fbw=uint32_t((fb>>9)&0x3f),psm=uint32_t((fb>>15)&0x1f),dbx=uint32_t((fb>>32)&0x7ff),dby=uint32_t((fb>>43)&0x7ff);
        uint32_t magh=uint32_t((disp>>23)&15)+1,dw=uint32_t((disp>>32)&0xfff)+1,dh=uint32_t((disp>>44)&0x7ff)+1;
        w=dw/magh; h=dh; if(!w || !h || w>2048 || h>2048) return false;
        out.assign(size_t(w)*h,0);
        for(uint32_t y=0;y<h;++y) for(uint32_t x=0;x<w;++x) out[size_t(y)*w+x]=toRgba(psm,readPixel(psm,fbp*32,fbw,dbx+x,dby+y))|0xff000000u;
        return true;
    }
    static uint32_t toRgba(uint32_t psm,uint32_t v) {       // little-endian R,G,B,A bytes
        if(psm==CT16 || psm==CT16S || psm==Z16 || psm==Z16S)
            return ((v&31)<<3)|(((v>>5)&31)<<11)|(((v>>10)&31)<<19)|((v&0x8000)?0x80000000u:0);
        if(psm==CT24) return v|0x80000000u;
        return v;
    }
private:
    uint64_t rgbaq_{},st_{},uv_{},prmode_{};
    struct Vertex { int32_t x,y; uint32_t z; uint8_t r,g,b,a,f; float s,t,q; uint32_t u,v; };
    Vertex vq_[3]{}; int vcount_{};
    bool clut_dirty_{true};
    // transfer state
    uint32_t tx_x_{},tx_y_{},tx_active_{}; std::vector<uint8_t> tx_pending_;

    // ---------------- transfers
    void startTransfer() {
        uint32_t dir=uint32_t(regs[0x53]&3);
        if(dir==0) { tx_active_=1; tx_x_=0; tx_y_=0; tx_pending_.clear(); return; }
        tx_active_=0;
        if(dir==2) { localCopy(); return; }
        if(dir==1) { stop_reason="gs_local_to_host_transfer"; return; }
    }
    void hwreg(uint64_t v) {
        if(!tx_active_) return;
        uint64_t bb=regs[0x50],pos=regs[0x51],trx=regs[0x52];
        uint32_t dbp=uint32_t((bb>>32)&0x3fff),dbw=uint32_t((bb>>48)&0x3f),dpsm=uint32_t((bb>>56)&0x3f);
        uint32_t dsax=uint32_t((pos>>32)&0x7ff),dsay=uint32_t((pos>>48)&0x7ff),rrw=uint32_t(trx&0xfff),rrh=uint32_t((trx>>32)&0xfff);
        if(!known(dpsm)) { stop_reason="gs_transfer_psm "+std::to_string(dpsm); return; }
        if(!rrw || !rrh) { tx_active_=0; return; }
        uint8_t b[8]; std::memcpy(b,&v,8);
        tx_pending_.insert(tx_pending_.end(),b,b+8);
        uint32_t bpp=bitsPerPixel(dpsm);
        auto next=[&]{ if(++tx_x_>=rrw) { tx_x_=0; if(++tx_y_>=rrh) tx_active_=0; } };
        size_t at=0;
        if(bpp==4) {
            for(;at<tx_pending_.size() && tx_active_;++at) for(int n=0;n<2 && tx_active_;++n) { writePixel(dpsm,dbp,dbw,dsax+tx_x_,dsay+tx_y_,(tx_pending_[at]>>(n*4))&15); next(); }
        } else {
            uint32_t bytes=bpp/8;
            for(;at+bytes<=tx_pending_.size() && tx_active_;at+=bytes) {
                uint32_t p=0; std::memcpy(&p,&tx_pending_[at],bytes);
                writePixel(dpsm,dbp,dbw,dsax+tx_x_,dsay+tx_y_,p); next();
            }
        }
        tx_pending_.erase(tx_pending_.begin(),tx_pending_.begin()+long(at));
        if(!tx_active_) tx_pending_.clear();
    }
    void localCopy() {
        uint64_t bb=regs[0x50],pos=regs[0x51],trx=regs[0x52];
        uint32_t sbp=uint32_t(bb&0x3fff),sbw=uint32_t((bb>>16)&0x3f),spsm=uint32_t((bb>>24)&0x3f);
        uint32_t dbp=uint32_t((bb>>32)&0x3fff),dbw=uint32_t((bb>>48)&0x3f),dpsm=uint32_t((bb>>56)&0x3f);
        uint32_t ssax=uint32_t(pos&0x7ff),ssay=uint32_t((pos>>16)&0x7ff),dsax=uint32_t((pos>>32)&0x7ff),dsay=uint32_t((pos>>48)&0x7ff);
        uint32_t rrw=uint32_t(trx&0xfff),rrh=uint32_t((trx>>32)&0xfff);
        if(!known(spsm) || !known(dpsm) || bitsPerPixel(spsm)!=bitsPerPixel(dpsm)) { stop_reason="gs_local_copy_psm "+std::to_string(spsm)+"->"+std::to_string(dpsm); return; }
        std::vector<uint32_t> tmp(size_t(rrw)*rrh);
        for(uint32_t y=0;y<rrh;++y) for(uint32_t x=0;x<rrw;++x) tmp[size_t(y)*rrw+x]=readPixel(spsm,sbp,sbw,(ssax+x)&2047,(ssay+y)&2047);
        for(uint32_t y=0;y<rrh;++y) for(uint32_t x=0;x<rrw;++x) writePixel(dpsm,dbp,dbw,(dsax+x)&2047,(dsay+y)&2047,tmp[size_t(y)*rrw+x]);
    }

    // ---------------- vertex queue and primitive assembly
    uint64_t prim() const { return ((regs[0x1a]&1)?regs[0]:(regs[0]&7)|(prmode_&~uint64_t(7))); }   // PRMODECONT
    void vertex(uint8_t reg,uint64_t v) {
        Vertex x{};
        uint64_t p=prim(); int ctx=int((p>>9)&1);
        uint64_t off=regs[0x18+ctx];
        x.x=int32_t(v&0xffff)-int32_t(off&0xffff); x.y=int32_t((v>>16)&0xffff)-int32_t((off>>32)&0xffff);
        if(reg==0x04 || reg==0x0c) { x.z=uint32_t((v>>32)&0xffffff); x.f=uint8_t(v>>56); }
        else { x.z=uint32_t(v>>32); x.f=uint8_t(regs[0x0a]>>56); }
        x.r=uint8_t(rgbaq_); x.g=uint8_t(rgbaq_>>8); x.b=uint8_t(rgbaq_>>16); x.a=uint8_t(rgbaq_>>24);
        uint32_t qb=uint32_t(rgbaq_>>32); std::memcpy(&x.q,&qb,4);
        uint32_t sb=uint32_t(st_),tb=uint32_t(st_>>32); std::memcpy(&x.s,&sb,4); std::memcpy(&x.t,&tb,4);
        x.u=uint32_t(uv_&0x3fff); x.v=uint32_t((uv_>>16)&0x3fff);
        bool kick=reg==0x04 || reg==0x05;
        uint32_t type=uint32_t(p&7);
        static constexpr int need[8]={1,2,2,3,3,3,2,0};
        if(type==7) { stop_reason="gs_prim_reserved"; return; }
        if(vcount_<need[type]) vq_[vcount_++]=x;
        else {
            if(type==5) vq_[1]=vq_[2];                                     // fan keeps vertex 0
            else if(need[type]==3) { vq_[0]=vq_[1]; vq_[1]=vq_[2]; }
            else if(need[type]==2) vq_[0]=vq_[1];
            vq_[need[type]-1]=x;
        }
        if(vcount_<need[type]) return;
        if(kick) draw(type,p);
        if(type==0 || type==1 || type==3 || type==6) vcount_=0;          // lists restart
    }
    // ---------------- per-pixel pipeline
    struct Ctx { uint64_t frame,zbuf,test,alpha,tex0,tex1,clamp,scissor,fba; };
    Ctx context(uint64_t p) const {
        int c=int((p>>9)&1);
        return {regs[0x4c+c],regs[0x4e+c],regs[0x47+c],regs[0x42+c],regs[0x06+c],regs[0x14+c],regs[0x08+c],regs[0x40+c],regs[0x4a+c]};
    }
    uint32_t clutEntry(const Ctx& c,uint32_t index) {
        uint64_t t=c.tex0;
        uint32_t cbp=uint32_t((t>>37)&0x3fff),cpsm=uint32_t((t>>51)&15),csm=uint32_t((t>>55)&1),csa=uint32_t((t>>56)&31);
        uint32_t psm=uint32_t((t>>20)&0x3f);
        if(csm) {                                                              // CSM2: linear row at TEXCLUT
            uint64_t tc=regs[0x1c]; uint32_t cbw=uint32_t(tc&0x3f),cou=uint32_t((tc>>6)&0x3f)*16,cov=uint32_t((tc>>12)&0x3ff);
            uint32_t v=readPixel(cpsm?CT16:CT32,cbp,cbw,cou+index,cov);
            return cpsm?toRgba(CT16,v):v;
        }
        uint32_t e=(psm==T4 || psm==T4HL || psm==T4HH)?(cpsm?(csa&31)*16:(csa&15)*16)+index:index;
        uint32_t col=(e&7)|((e&0x10)>>1),row=((e>>5)<<1)|((e>>3)&1);
        if(cpsm==0) return readPixel(CT32,cbp,1,col,row);
        uint32_t v=readPixel(cpsm==2?CT16:CT16S,cbp,1,col,row);
        return toRgba(CT16,v);
    }
    uint32_t texel(const Ctx& c,int32_t u,int32_t v) {                      // RGBA8 after CLUT/TEXA
        uint64_t t=c.tex0;
        uint32_t tbp=uint32_t(t&0x3fff),tbw=uint32_t((t>>14)&0x3f),psm=uint32_t((t>>20)&0x3f);
        int32_t tw=1<<((t>>26)&15),th=1<<((t>>30)&15);
        auto wrap=[&](int32_t x,int32_t size,uint32_t mode,int32_t mn,int32_t mx){
            switch(mode) {
                case 0: return x&(size-1);
                case 1: return std::clamp(x,0,size-1);
                case 2: return std::clamp(x,mn,mx);
                default: return (x&mn)|mx;                                     // region repeat: UMSK/UFIX
            }
        };
        uint64_t cl=c.clamp;
        u=wrap(u,tw,uint32_t(cl&3),int32_t((cl>>4)&0x3ff),int32_t((cl>>14)&0x3ff));
        v=wrap(v,th,uint32_t((cl>>2)&3),int32_t((cl>>24)&0x3ff),int32_t((cl>>34)&0x3ff));
        if(!known(psm)) { stop_reason="gs_texture_psm "+std::to_string(psm); return 0; }
        uint32_t raw=readPixel(psm,tbp,tbw,uint32_t(u),uint32_t(v));
        uint64_t texa=regs[0x3b]; uint32_t ta0=uint32_t(texa&0xff),ta1=uint32_t((texa>>32)&0xff); bool aem=(texa>>15)&1;
        switch(psm) {
            case CT32: return raw;
            case CT24: return raw|((aem && !raw)?0u:ta0<<24);
            case CT16: case CT16S: {
                uint32_t rgb=toRgba(CT16,raw)&0xffffff;
                uint32_t a=(raw&0x8000)?ta1:((aem && !(raw&0x7fff))?0:ta0);
                return rgb|(a<<24);
            }
            default: {                                                           // indexed
                uint32_t e=clutEntry(c,raw);
                uint32_t cpsm=uint32_t((t>>51)&15);
                if(cpsm) { uint32_t a=(e&0x80000000u)?ta1:(((e&0xffffff)==0 && aem)?0:ta0); return (e&0xffffff)|(a<<24); }
                return e;
            }
        }
    }
    void plot(const Ctx& c,uint64_t p,int32_t x,int32_t y,uint32_t z,int r,int g,int b,int a,uint8_t fog,bool tex,float s,float t,float q,float u,float v) {
        uint64_t sc=c.scissor;
        if(x<int32_t(sc&0x7ff) || x>int32_t((sc>>16)&0x7ff) || y<int32_t((sc>>32)&0x7ff) || y>int32_t((sc>>48)&0x7ff)) return;
        ++pixels;
        uint64_t fr=c.frame,zb=c.zbuf,te=c.test;
        uint32_t fbp=uint32_t(fr&0x1ff)*32,fbw=uint32_t((fr>>16)&0x3f),fpsm=uint32_t((fr>>24)&0x3f),fbmsk=uint32_t(fr>>32);
        uint32_t zbp=uint32_t(zb&0x1ff)*32,zpsm=uint32_t((zb>>24)&0xf)|0x30; bool zmsk=(zb>>32)&1;
        if(tex) {
            uint64_t t0=c.tex0;
            int32_t tu,tv;
            if((p>>8)&1) { tu=int32_t(std::floor(u/16.f)); tv=int32_t(std::floor(v/16.f)); }
            else {
                float iq=q!=0?1.f/q:0.f;
                tu=int32_t(std::floor(s*iq*float(1<<((t0>>26)&15)))); tv=int32_t(std::floor(t*iq*float(1<<((t0>>30)&15))));
            }
            uint32_t tc=texel(c,tu,tv); if(!stop_reason.empty()) return;
            int tr=int(tc&0xff),tg=int((tc>>8)&0xff),tb=int((tc>>16)&0xff),ta=int(tc>>24);
            bool tcc=(t0>>34)&1; uint32_t tfx=uint32_t((t0>>35)&3);
            switch(tfx) {
                case 0: r=std::min(255,tr*r>>7); g=std::min(255,tg*g>>7); b=std::min(255,tb*b>>7); if(tcc) a=std::min(255,ta*a>>7); break;
                case 1: r=tr; g=tg; b=tb; if(tcc) a=ta; break;
                case 2: r=std::min(255,(tr*r>>7)+a); g=std::min(255,(tg*g>>7)+a); b=std::min(255,(tb*b>>7)+a); if(tcc) a=std::min(255,ta+a); break;
                case 3: r=std::min(255,(tr*r>>7)+a); g=std::min(255,(tg*g>>7)+a); b=std::min(255,(tb*b>>7)+a); if(tcc) a=ta; break;
            }
        }
        if((p>>5)&1) {                                                          // fog
            uint64_t fc=regs[0x3d];
            r=(fog*r+(255-fog)*int(fc&0xff))>>8; g=(fog*g+(255-fog)*int((fc>>8)&0xff))>>8; b=(fog*b+(255-fog)*int((fc>>16)&0xff))>>8;
        }
        bool write_fb=true,write_z=!zmsk,write_alpha=true;
        if(te&1) {                                                              // alpha test
            uint32_t atst=uint32_t((te>>1)&7),aref=uint32_t((te>>4)&0xff); bool pass=true;
            switch(atst) { case 0: pass=false; break; case 1: pass=true; break; case 2: pass=uint32_t(a)<aref; break; case 3: pass=uint32_t(a)<=aref; break;
                case 4: pass=uint32_t(a)==aref; break; case 5: pass=uint32_t(a)>=aref; break; case 6: pass=uint32_t(a)>aref; break; case 7: pass=uint32_t(a)!=aref; break; }
            if(!pass) {
                switch(uint32_t((te>>12)&3)) { case 0: return; case 1: write_z=false; break; case 2: write_fb=false; break; case 3: write_z=false; write_alpha=false; break; }
            }
        }
        if((te>>16)&1) {                                                        // depth test
            uint32_t ztst=uint32_t((te>>17)&3);
            if(ztst==0) return;
            if(ztst>=2) {
                uint32_t zmax=zpsm==Z32?0xffffffffu:zpsm==Z24?0xffffffu:0xffffu;
                uint32_t zc=std::min(z,zmax),zd=readPixel(zpsm,zbp,fbw,uint32_t(x),uint32_t(y));
                if(ztst==2 ? zc<zd : zc<=zd) return;
                z=zc;
            }
        }
        uint32_t dst=readPixel(fpsm,fbp,fbw,uint32_t(x),uint32_t(y)),drgba=toRgba(fpsm,dst);
        if((te>>14)&1) {                                                        // destination alpha test
            bool bit=(fpsm==CT16 || fpsm==CT16S)?(dst&0x8000)!=0:(dst&0x80000000u)!=0;
            if(bit!=bool((te>>15)&1)) return;
        }
        if(((p>>6)&1) && (!(regs[0x49]&1) || (a&0x80))) {                       // alpha blending (PABE)
            uint64_t al=c.alpha;
            int cs[3]={r,g,b},cd[3]={int(drgba&0xff),int((drgba>>8)&0xff),int((drgba>>16)&0xff)};
            int ad=int(drgba>>24),fix=int((al>>32)&0xff);
            int cc=int((al>>4)&3)==0?a:int((al>>4)&3)==1?ad:fix;
            for(int i=0;i<3;++i) {
                auto pick=[&](int sel){ return sel==0?cs[i]:sel==1?cd[i]:0; };
                int val=((pick(int(al&3))-pick(int((al>>2)&3)))*cc>>7)+pick(int((al>>6)&3));
                cs[i]=(regs[0x46]&1)?std::clamp(val,0,255):(val&0xff);
            }
            r=cs[0]; g=cs[1]; b=cs[2];
        } else if(!(regs[0x46]&1)) { r&=0xff; g&=0xff; b&=0xff; }
        if(c.fba&1) a|=0x80;
        if(write_fb) {
            uint32_t value,mask;
            if(fpsm==CT16 || fpsm==CT16S) {
                value=uint32_t(r>>3)|(uint32_t(g>>3)<<5)|(uint32_t(b>>3)<<10)|((a&0x80)?0x8000u:0);
                mask=((fbmsk>>3)&0x1f)|(((fbmsk>>11)&0x1f)<<5)|(((fbmsk>>19)&0x1f)<<10)|((fbmsk>>31)<<15);
                if(!write_alpha) mask|=0x8000;
                mask|=0xffff0000u;
            } else {
                value=uint32_t(r)|(uint32_t(g)<<8)|(uint32_t(b)<<16)|(uint32_t(a&0xff)<<24);
                mask=fbmsk|(write_alpha?0u:0xff000000u);
            }
            writePixel(fpsm,fbp,fbw,uint32_t(x),uint32_t(y),value,mask);
        }
        if(write_z && ((te>>16)&1)) writePixel(zpsm,zbp,fbw,uint32_t(x),uint32_t(y),z);
    }
    void draw(uint32_t type,uint64_t p) {
        ++primitives;
        Ctx c=context(p);
        if(trace && primitives>=trace_from && trace_left) {
            --trace_left;
            *trace<<std::hex<<"GS prim type="<<type<<" prim=0x"<<p<<" frame=0x"<<c.frame<<" test=0x"<<c.test<<" tex0=0x"<<c.tex0<<" alpha=0x"<<c.alpha<<" scissor=0x"<<c.scissor<<std::dec;
            int n=type==0?1:type<=2||type==6?2:3;
            for(int i=0;i<n;++i) *trace<<" | v"<<i<<" x="<<vq_[i].x/16.0<<" y="<<vq_[i].y/16.0<<" z="<<vq_[i].z<<" rgba="<<int(vq_[i].r)<<","<<int(vq_[i].g)<<","<<int(vq_[i].b)<<","<<int(vq_[i].a)<<" stq="<<vq_[i].s<<","<<vq_[i].t<<","<<vq_[i].q<<" uv="<<vq_[i].u<<","<<vq_[i].v;
            *trace<<'\n';
        }
        uint32_t fpsm=uint32_t((c.frame>>24)&0x3f);
        if(fpsm!=CT32 && fpsm!=CT24 && fpsm!=CT16 && fpsm!=CT16S && fpsm!=Z32 && fpsm!=Z24 && fpsm!=Z16 && fpsm!=Z16S) { stop_reason="gs_frame_psm "+std::to_string(fpsm); return; }
        bool tex=(p>>4)&1,gouraud=(p>>3)&1;
        switch(type) {
            case 0: { const auto& v=vq_[0]; plot(c,p,(v.x+8)>>4,(v.y+8)>>4,v.z,v.r,v.g,v.b,v.a,v.f,tex,v.s,v.t,v.q,float(v.u),float(v.v)); return; }
            case 1: case 2: line(c,p,vq_[0],vq_[1],gouraud,tex); return;
            case 3: case 4: case 5: triangle(c,p,vq_[0],vq_[1],vq_[2],gouraud,tex); return;
            case 6: sprite(c,p,vq_[0],vq_[1],tex); return;
        }
    }
    void line(const Ctx& c,uint64_t p,const Vertex& a,const Vertex& b,bool gouraud,bool tex) {
        int32_t x0=(a.x+8)>>4,y0=(a.y+8)>>4,x1=(b.x+8)>>4,y1=(b.y+8)>>4;
        int n=std::max(std::abs(x1-x0),std::abs(y1-y0)); if(n==0) n=1;
        for(int i=0;i<=n;++i) {
            float f=float(i)/float(n);
            auto L=[&](float p0,float p1){ return p0+(p1-p0)*f; };
            const Vertex& col=gouraud?a:b;
            int r=gouraud?int(L(a.r,b.r)):col.r,g=gouraud?int(L(a.g,b.g)):col.g,bb=gouraud?int(L(a.b,b.b)):col.b,al=gouraud?int(L(a.a,b.a)):col.a;
            plot(c,p,x0+int32_t(std::lround(float(x1-x0)*f)),y0+int32_t(std::lround(float(y1-y0)*f)),uint32_t(L(float(a.z),float(b.z))),r,g,bb,al,
                 uint8_t(L(a.f,b.f)),tex,L(a.s,b.s),L(a.t,b.t),L(a.q,b.q),L(float(a.u),float(b.u)),L(float(a.v),float(b.v)));
        }
    }
    void triangle(const Ctx& c,uint64_t p,const Vertex& a,const Vertex& b,const Vertex& v2,bool gouraud,bool tex) {
        double area=double(b.x-a.x)*double(v2.y-a.y)-double(b.y-a.y)*double(v2.x-a.x);
        if(area==0) return;
        uint64_t sc=c.scissor;
        int32_t minx=std::max<int32_t>(int32_t(sc&0x7ff),(std::min({a.x,b.x,v2.x})+15)>>4),maxx=std::min<int32_t>(int32_t((sc>>16)&0x7ff),(std::max({a.x,b.x,v2.x})-1)>>4);
        int32_t miny=std::max<int32_t>(int32_t((sc>>32)&0x7ff),(std::min({a.y,b.y,v2.y})+15)>>4),maxy=std::min<int32_t>(int32_t((sc>>48)&0x7ff),(std::max({a.y,b.y,v2.y})-1)>>4);
        if(minx>maxx || miny>maxy) return;
        auto edge=[](const Vertex& p0,const Vertex& p1,double px,double py){ return double(p1.x-p0.x)*(py-p0.y)-double(p1.y-p0.y)*(px-p0.x); };
        // top-left rule: an edge owns its pixels when it is a top or left edge for the winding
        auto owns=[&](const Vertex& p0,const Vertex& p1){ int32_t dy=p1.y-p0.y,dx=p1.x-p0.x; return area>0?(dy<0 || (dy==0 && dx>0)):(dy>0 || (dy==0 && dx<0)); };
        bool o0=owns(b,v2),o1=owns(v2,a),o2=owns(a,b);
        for(int32_t y=miny;y<=maxy;++y) for(int32_t x=minx;x<=maxx;++x) {
            double px=double(x)*16,py=double(y)*16;
            double w0=edge(b,v2,px,py)/area,w1=edge(v2,a,px,py)/area,w2=edge(a,b,px,py)/area;
            if(w0<0 || w1<0 || w2<0) continue;
            if((w0==0 && !o0) || (w1==0 && !o1) || (w2==0 && !o2)) continue;
            auto I=[&](double p0,double p1,double p2){ return p0*w0+p1*w1+p2*w2; };
            int r=gouraud?int(I(a.r,b.r,v2.r)):v2.r,g=gouraud?int(I(a.g,b.g,v2.g)):v2.g,bl=gouraud?int(I(a.b,b.b,v2.b)):v2.b,al=gouraud?int(I(a.a,b.a,v2.a)):v2.a;
            plot(c,p,x,y,uint32_t(std::clamp(I(double(a.z),double(b.z),double(v2.z)),0.0,4294967295.0)),r,g,bl,al,uint8_t(I(a.f,b.f,v2.f)),tex,
                 float(I(a.s,b.s,v2.s)),float(I(a.t,b.t,v2.t)),float(I(a.q,b.q,v2.q)),float(I(a.u,b.u,v2.u)),float(I(a.v,b.v,v2.v)));
            if(!stop_reason.empty()) return;
        }
    }
    void sprite(const Ctx& c,uint64_t p,const Vertex& a,const Vertex& b,bool tex) {
        int32_t x0=std::min(a.x,b.x),x1=std::max(a.x,b.x),y0=std::min(a.y,b.y),y1=std::max(a.y,b.y);
        const Vertex& l=a.x<=b.x?a:b; const Vertex& r=a.x<=b.x?b:a;
        const Vertex& t=a.y<=b.y?a:b; const Vertex& d=a.y<=b.y?b:a;
        uint64_t sc=c.scissor;
        int32_t px0=std::max<int32_t>(int32_t(sc&0x7ff),(x0+15)>>4),px1=std::min<int32_t>(int32_t((sc>>16)&0x7ff),(x1-1)>>4);
        int32_t py0=std::max<int32_t>(int32_t((sc>>32)&0x7ff),(y0+15)>>4),py1=std::min<int32_t>(int32_t((sc>>48)&0x7ff),(y1-1)>>4);
        double w=std::max(1,x1-x0),h=std::max(1,y1-y0);
        for(int32_t y=py0;y<=py1;++y) for(int32_t x=px0;x<=px1;++x) {
            double fx=(double(x)*16-x0)/w,fy=(double(y)*16-y0)/h;
            auto X=[&](double p0,double p1){ return p0+(p1-p0)*fx; }; auto Y=[&](double p0,double p1){ return p0+(p1-p0)*fy; };
            plot(c,p,x,y,b.z,b.r,b.g,b.b,b.a,b.f,tex,float(X(l.s,r.s)),float(Y(t.t,d.t)),b.q,float(X(l.u,r.u)),float(Y(t.v,d.v)));
            if(!stop_reason.empty()) return;
        }
    }
};
}
