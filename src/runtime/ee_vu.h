#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
namespace mp2r {
// Vector unit micro mode interpreter (VU0 4 KB / VU1 16 KB data and code memory).
// Each 64-bit instruction pair holds an upper FMAC instruction (high word: I E M D T bits,
// dest xyzw 24-21, ft 20-16, fs 15-11, fd 10-6, opcode 5-0) and a lower instruction (low word),
// or a float immediate for I when the I bit is set. Both halves read the registers as they were
// before the pair; when both write the same VF register the upper result is kept.
// Floats follow the VU format: no infinities/NaNs (exponent 255 is an ordinary value, clamped
// here to +-FLT_MAX) and denormals read as zero. Q (DIV/SQRT/RSQRT) and P (EFU, VU1) results
// arrive after their latencies, counted at one cycle per pair; WAITQ/WAITP and a new
// instruction of the same unit wait for them. FMAC data hazards stall in hardware, so results
// are visible to the next pair. An E bit ends the program after the following pair.
class EeVu {
public:
    using Vec=std::array<float,4>;
    std::array<Vec,32> vf{};
    std::array<uint16_t,16> vi{};
    Vec acc{};
    float q{},p{},i{};
    uint32_t r{},status{},mac{},clip{};
    uint32_t pc{};                                  // instruction pair index
    uint8_t* code{}; uint32_t code_pairs{};         // 512 (VU0) or 2048 (VU1)
    uint8_t* data{}; uint32_t data_qwords{};        // 256 (VU0) or 1024 (VU1)
    std::function<void(uint32_t qword)> xgkick;     // VU1: PATH1 transfer from data memory
    std::function<uint32_t()> top,itop;             // VIF1 TOP / VIF ITOP
    std::string stop_reason;
    uint64_t cycles{},programs{};

    EeVu() { vf[0]={0,0,0,1}; }
    // Runs from pair `start` (or the current pc for 0xffffffff, MSCNT) until the E bit.
    bool run(uint32_t start,uint64_t budget=50000000) {
        if(start!=0xffffffffu) pc=start;
        ++programs;
        bool ending=false; int64_t branch=-1;
        for(uint64_t n=0;n<budget;++n) {
            uint32_t at=pc%code_pairs;
            uint32_t lower,upper; std::memcpy(&lower,code+at*8,4); std::memcpy(&upper,code+at*8+4,4);
            pc=at+1; ++cycles; settle();
            int64_t pending=branch; branch=-1;
            if(!pair(upper,lower,branch)) return false;
            if(pending>=0) pc=uint32_t(pending);
            if(ending) { finishPending(); return true; }
            if(upper&0x40000000u) ending=true;
        }
        stop_reason="vu_program_budget"; return false;
    }
private:
    float q_next{},p_next{}; uint64_t q_ready{},p_ready{};
    void settle() { if(q_ready && cycles>=q_ready) { q=q_next; q_ready=0; } if(p_ready && cycles>=p_ready) { p=p_next; p_ready=0; } }
    void finishPending() { if(q_ready) { q=q_next; q_ready=0; } if(p_ready) { p=p_next; p_ready=0; } }

    static float clean(float v) {
        uint32_t u; std::memcpy(&u,&v,4);
        if((u&0x7f800000u)==0x7f800000u) u=(u&0x80000000u)|0x7f7fffffu;
        else if((u&0x7f800000u)==0) u&=0x80000000u;
        float o; std::memcpy(&o,&u,4); return o;
    }
    static uint32_t bits(float v) { uint32_t u; std::memcpy(&u,&v,4); return u; }
    static float fromBits(uint32_t u) { float v; std::memcpy(&v,&u,4); return v; }
    Vec reg(unsigned n) const { Vec v=vf[n]; for(auto& c:v) c=clean(c); return v; }
    static int32_t sext(uint32_t v,unsigned width) { return int32_t(v<<(32-width))>>(32-width); }
    uint8_t* qword(uint32_t index) { return data+(index%data_qwords)*16; }

    // FMAC result with flags: fields in dest are written to target (nullptr: ACC).
    struct Result { Vec v{}; unsigned dest{}; int fd{-1}; bool acc{}; bool flags{}; uint32_t mac{}; bool clip{}; uint32_t clip_value{}; };
    static float flagged(double x,unsigned field,uint32_t& m) {
        float f=float(x);
        uint32_t u=bits(f),e=u&0x7f800000u;
        unsigned b=3-field;
        if(e==0x7f800000u || std::fabs(x)>3.4028234663852886e38) { m|=1u<<(12+b); u=(u&0x80000000u)|0x7f7fffffu; if(x<0) u|=0x80000000u; }
        else if(e==0 && (u&0x7fffffu)) { m|=1u<<(8+b); u&=0x80000000u; }
        if(!(u&0x7fffffffu)) m|=1u<<b;
        if(u&0x80000000u) m|=1u<<(4+b);
        return fromBits(u);
    }
    void commit(const Result& res) {
        if(res.clip) clip=res.clip_value;
        if(res.flags) {
            mac=res.mac;
            uint32_t s=0;
            if(mac&0x000f) s|=1; if(mac&0x00f0) s|=2; if(mac&0x0f00) s|=4; if(mac&0xf000) s|=8;
            status=(status&0xff0u)|s|(s<<6);                        // Z S U O, sticky copies in bits 6-9
        }
        if(!res.dest) return;
        if(res.acc) { for(unsigned c=0;c<4;++c) if(res.dest&(8>>c)) acc[c]=res.v[c]; return; }
        if(res.fd<=0) return;
        for(unsigned c=0;c<4;++c) if(res.dest&(8>>c)) vf[res.fd][c]=res.v[c];
    }
    bool upperOp(uint32_t u,Result& res) {
        unsigned dest=(u>>21)&15,ft=(u>>16)&31,fs=(u>>11)&31,fd=(u>>6)&31,op=u&63;
        Vec s=reg(fs),t=reg(ft),a=acc;
        for(auto& c:a) c=clean(c);
        res.dest=dest;
        auto arith=[&](auto fn,bool to_acc,int out) {
            res.acc=to_acc; res.fd=out; res.flags=true;
            for(unsigned c=0;c<4;++c) {
                double v=fn(c);
                if(dest&(8>>c)) res.v[c]=flagged(v,c,res.mac);
            }
        };
        auto bc=[&](unsigned b){ return Vec{t[b],t[b],t[b],t[b]}; };
        auto scalar=[](float x){ return Vec{x,x,x,x}; };
        Vec qv=scalar(clean(q)),iv=scalar(clean(i));
        auto add=[&](const Vec& y,bool to_acc,int out){ arith([&](unsigned c){ return double(s[c])+y[c]; },to_acc,out); };
        auto sub=[&](const Vec& y,bool to_acc,int out){ arith([&](unsigned c){ return double(s[c])-y[c]; },to_acc,out); };
        auto mul=[&](const Vec& y,bool to_acc,int out){ arith([&](unsigned c){ return double(s[c])*y[c]; },to_acc,out); };
        auto madd=[&](const Vec& y,bool to_acc,int out){ arith([&](unsigned c){ return double(a[c])+double(clean(float(double(s[c])*y[c]))); },to_acc,out); };
        auto msub=[&](const Vec& y,bool to_acc,int out){ arith([&](unsigned c){ return double(a[c])-double(clean(float(double(s[c])*y[c]))); },to_acc,out); };
        auto minmax=[&](const Vec& y,bool is_max){
            res.fd=int(fd);
            for(unsigned c=0;c<4;++c) res.v[c]=is_max?std::max(s[c],y[c]):std::min(s[c],y[c]);
        };
        if(op<0x3c) {
            switch(op) {
                case 0x00: case 0x01: case 0x02: case 0x03: add(bc(op&3),false,int(fd)); return true;
                case 0x04: case 0x05: case 0x06: case 0x07: sub(bc(op&3),false,int(fd)); return true;
                case 0x08: case 0x09: case 0x0a: case 0x0b: madd(bc(op&3),false,int(fd)); return true;
                case 0x0c: case 0x0d: case 0x0e: case 0x0f: msub(bc(op&3),false,int(fd)); return true;
                case 0x10: case 0x11: case 0x12: case 0x13: minmax(bc(op&3),true); return true;
                case 0x14: case 0x15: case 0x16: case 0x17: minmax(bc(op&3),false); return true;
                case 0x18: case 0x19: case 0x1a: case 0x1b: mul(bc(op&3),false,int(fd)); return true;
                case 0x1c: mul(qv,false,int(fd)); return true;
                case 0x1d: minmax(iv,true); return true;
                case 0x1e: mul(iv,false,int(fd)); return true;
                case 0x1f: minmax(iv,false); return true;
                case 0x20: add(qv,false,int(fd)); return true;
                case 0x21: madd(qv,false,int(fd)); return true;
                case 0x22: add(iv,false,int(fd)); return true;
                case 0x23: madd(iv,false,int(fd)); return true;
                case 0x24: sub(qv,false,int(fd)); return true;
                case 0x25: msub(qv,false,int(fd)); return true;
                case 0x26: sub(iv,false,int(fd)); return true;
                case 0x27: msub(iv,false,int(fd)); return true;
                case 0x28: add(t,false,int(fd)); return true;
                case 0x29: madd(t,false,int(fd)); return true;
                case 0x2a: mul(t,false,int(fd)); return true;
                case 0x2b: minmax(t,true); return true;
                case 0x2c: sub(t,false,int(fd)); return true;
                case 0x2d: msub(t,false,int(fd)); return true;
                case 0x2e: {                                                // OPMSUB
                    Vec y{t[2],t[0],t[1],0},x{s[1],s[2],s[0],0};
                    res.dest&=0xe;
                    arith([&](unsigned c){ return double(a[c])-double(clean(float(double(x[c])*y[c]))); },false,int(fd));
                    return true;
                }
                case 0x2f: minmax(t,false); return true;
            }
            stop_reason="vu_upper_opcode "+std::to_string(op); return false;
        }
        unsigned sp=(((u>>6)&31)<<2)|(u&3);
        switch(sp) {
            case 0x00: case 0x01: case 0x02: case 0x03: add(bc(sp&3),true,-1); return true;
            case 0x04: case 0x05: case 0x06: case 0x07: sub(bc(sp&3),true,-1); return true;
            case 0x08: case 0x09: case 0x0a: case 0x0b: madd(bc(sp&3),true,-1); return true;
            case 0x0c: case 0x0d: case 0x0e: case 0x0f: msub(bc(sp&3),true,-1); return true;
            case 0x10: case 0x11: case 0x12: case 0x13: {                  // ITOF0/4/12/15
                static constexpr float scale[4]={1.f,1.f/16,1.f/4096,1.f/32768};
                res.fd=int(ft);
                for(unsigned c=0;c<4;++c) { int32_t v; std::memcpy(&v,&vf[fs][c],4); res.v[c]=float(v)*scale[sp&3]; }
                return true;
            }
            case 0x14: case 0x15: case 0x16: case 0x17: {                  // FTOI0/4/12/15
                static constexpr double scale[4]={1.,16.,4096.,32768.};
                res.fd=int(ft);
                for(unsigned c=0;c<4;++c) {
                    double v=std::trunc(double(s[c])*scale[sp&3]);
                    int32_t n=v>=2147483647.?INT32_MAX:v<=-2147483648.?INT32_MIN:int32_t(v);
                    std::memcpy(&res.v[c],&n,4);
                }
                return true;
            }
            case 0x18: case 0x19: case 0x1a: case 0x1b: mul(bc(sp&3),true,-1); return true;
            case 0x1c: mul(qv,true,-1); return true;
            case 0x1d: res.fd=int(ft); for(unsigned c=0;c<4;++c) res.v[c]=std::fabs(s[c]); return true;   // ABS
            case 0x1e: mul(iv,true,-1); return true;
            case 0x1f: {                                                    // CLIP fs.xyz, ft.w
                float w=std::fabs(t[3]); uint32_t f=0;
                if(s[0]>w) f|=1; if(s[0]<-w) f|=2; if(s[1]>w) f|=4; if(s[1]<-w) f|=8; if(s[2]>w) f|=16; if(s[2]<-w) f|=32;
                res.dest=0; res.clip=true; res.clip_value=((clip<<6)|f)&0xffffffu; return true;
            }
            case 0x20: add(qv,true,-1); return true;
            case 0x21: madd(qv,true,-1); return true;
            case 0x22: add(iv,true,-1); return true;
            case 0x23: madd(iv,true,-1); return true;
            case 0x24: sub(qv,true,-1); return true;
            case 0x25: msub(qv,true,-1); return true;
            case 0x26: sub(iv,true,-1); return true;
            case 0x27: msub(iv,true,-1); return true;
            case 0x28: add(t,true,-1); return true;
            case 0x29: madd(t,true,-1); return true;
            case 0x2a: mul(t,true,-1); return true;
            case 0x2c: sub(t,true,-1); return true;
            case 0x2d: msub(t,true,-1); return true;
            case 0x2e: {                                                    // OPMULA
                Vec y{t[2],t[0],t[1],0},x{s[1],s[2],s[0],0};
                res.dest&=0xe;
                arith([&](unsigned c){ return double(x[c])*y[c]; },true,-1);
                return true;
            }
            case 0x2f: res.dest=0; return true;                             // NOP
        }
        stop_reason="vu_upper_special "+std::to_string(sp); return false;
    }
    void writeVf(unsigned n,unsigned dest,const Vec& v) { if(!n) return; for(unsigned c=0;c<4;++c) if(dest&(8>>c)) vf[n][c]=v[c]; }
    void setVi(unsigned n,uint32_t v) { if(n&15) vi[n&15]=uint16_t(v); }
    void loadQ(uint32_t index,unsigned ft,unsigned dest) { Vec v; std::memcpy(v.data(),qword(index),16); writeVf(ft,dest,v); }
    void storeQ(uint32_t index,unsigned fs,unsigned dest) {
        uint8_t* m=qword(index); for(unsigned c=0;c<4;++c) if(dest&(8>>c)) std::memcpy(m+c*4,&vf[fs][c],4);
    }
    void startQ(double v,unsigned latency) { finishQ(); q_next=float(v); q_ready=cycles+latency; }
    void finishQ() { if(q_ready) { q=q_next; q_ready=0; } }
    void startP(double v,unsigned latency) { if(p_ready) { p=p_next; p_ready=0; } p_next=float(v); p_ready=cycles+latency; }
    float divide(float a,float b,bool rsqrt_den) {
        status&=~0x30u;
        if(b==0) {
            if(a==0) status|=0x10|0x400; else status|=0x20|0x800;
            return fromBits(((bits(a)^bits(b))&0x80000000u)|0x7f7fffffu);
        }
        (void)rsqrt_den; return float(double(a)/b);
    }
    bool lowerOp(uint32_t l,int64_t& branch) {
        unsigned dest=(l>>21)&15,ft=(l>>16)&31,fs=(l>>11)&31;
        unsigned it=ft&15,is=fs&15;
        uint32_t vis=vi[is],vit=vi[it];
        int32_t imm11=sext(l&0x7ff,11);
        if(l>>31) {
            unsigned op=l&63;
            if(op<0x3c) {
                unsigned id=(l>>6)&15;
                switch(op) {
                    case 0x30: setVi(id,vis+vit); return true;                 // IADD
                    case 0x31: setVi(id,vis-vit); return true;                 // ISUB
                    case 0x32: setVi(it,vis+uint32_t(sext((l>>6)&31,5))); return true;   // IADDI
                    case 0x34: setVi(id,vis&vit); return true;                 // IAND
                    case 0x35: setVi(id,vis|vit); return true;                 // IOR
                }
                stop_reason="vu_lower_opcode "+std::to_string(op); return false;
            }
            unsigned sp=(((l>>6)&31)<<2)|(l&3);
            Vec s=reg(fs);
            unsigned fsf=(l>>21)&3,ftf=(l>>23)&3;
            switch(sp) {
                case 0x30: writeVf(ft,dest,vf[fs]); return true;               // MOVE
                case 0x31: writeVf(ft,dest,Vec{vf[fs][1],vf[fs][2],vf[fs][3],vf[fs][0]}); return true;   // MR32
                case 0x34: loadQ(vis,ft,dest); setVi(is,vis+1); return true;   // LQI ft,(is++)
                case 0x35: storeQ(vit,fs,dest); setVi(it,vit+1); return true;  // SQI fs,(it++)
                case 0x36: setVi(is,vis-1); loadQ(uint16_t(vis-1),ft,dest); return true;    // LQD
                case 0x37: setVi(it,vit-1); storeQ(uint16_t(vit-1),fs,dest); return true;   // SQD
                case 0x38: startQ(divide(s[fsf],reg(ft)[ftf],false),7); return true;        // DIV
                case 0x39: { float t=reg(ft)[ftf]; status&=~0x30u; if(t<0) status|=0x10|0x400; startQ(std::sqrt(std::fabs(t)),7); return true; }
                case 0x3a: {                                                   // RSQRT
                    float t=reg(ft)[ftf]; status&=~0x30u;
                    if(t<0) status|=0x10|0x400;
                    float d=std::sqrt(std::fabs(t));
                    startQ(d==0?divide(s[fsf],0,true):double(s[fsf])/d,13); return true;
                }
                case 0x3b: finishQ(); return true;                             // WAITQ
                case 0x3c: setVi(it,bits(vf[fs][fsf])&0xffff); return true;    // MTIR
                case 0x3d: { Vec v; int32_t x=int16_t(vis); for(auto& c:v) std::memcpy(&c,&x,4); writeVf(ft,dest,v); return true; }   // MFIR
                case 0x3e: { uint8_t* m=qword(vis); for(unsigned c=0;c<4;++c) if(dest&(8>>c)) { uint32_t w; std::memcpy(&w,m+c*4,4); setVi(it,w&0xffff); } return true; }   // ILWR
                case 0x3f: { uint8_t* m=qword(vis); uint32_t w=vit; for(unsigned c=0;c<4;++c) if(dest&(8>>c)) std::memcpy(m+c*4,&w,4); return true; }   // ISWR
                case 0x40: case 0x41: {                                        // RNEXT / RGET
                    if(sp==0x40) { uint32_t x=(r>>4)&1,y=(r>>22)&1; r=(((r<<1)^x^y)&0x7fffffu)|0x3f800000u; }
                    float v=fromBits(r); writeVf(ft,dest,Vec{v,v,v,v}); return true;
                }
                case 0x42: r=0x3f800000u|(bits(vf[fs][fsf])&0x7fffffu); return true;   // RINIT
                case 0x43: r=0x3f800000u|((r^bits(vf[fs][fsf]))&0x7fffffu); return true;   // RXOR
                case 0x64: { float v=clean(p); writeVf(ft,dest,Vec{v,v,v,v}); return true; }   // MFP
                case 0x68: setVi(it,top?top():0); return true;                 // XTOP
                case 0x69: setVi(it,itop?itop():0); return true;               // XITOP
                case 0x6c: if(!xgkick) { stop_reason="vu_xgkick_without_path1"; return false; } xgkick(vis); return true;
                case 0x70: startP(double(s[0])*s[0]+double(s[1])*s[1]+double(s[2])*s[2],11); return true;            // ESADD
                case 0x71: startP(1.0/(double(s[0])*s[0]+double(s[1])*s[1]+double(s[2])*s[2]),18); return true;      // ERSADD
                case 0x72: startP(std::sqrt(double(s[0])*s[0]+double(s[1])*s[1]+double(s[2])*s[2]),18); return true; // ELENG
                case 0x73: startP(1.0/std::sqrt(double(s[0])*s[0]+double(s[1])*s[1]+double(s[2])*s[2]),24); return true;   // ERLENG
                case 0x74: startP(std::atan2(double(s[1]),double(s[0])),54); return true;   // EATANxy
                case 0x75: startP(std::atan2(double(s[2]),double(s[0])),54); return true;   // EATANxz
                case 0x76: startP(double(s[0])+s[1]+s[2]+s[3],12); return true;            // ESUM
                case 0x78: startP(std::sqrt(std::fabs(double(s[fsf]))),12); return true;   // ESQRT
                case 0x79: startP(1.0/std::sqrt(std::fabs(double(s[fsf]))),18); return true;   // ERSQRT
                case 0x7a: startP(1.0/double(s[fsf]),12); return true;                     // ERCPR
                case 0x7b: if(p_ready) { p=p_next; p_ready=0; } return true;               // WAITP
                case 0x7c: startP(std::sin(double(s[fsf])),29); return true;               // ESIN
                case 0x7d: startP(std::atan(double(s[fsf])),54); return true;              // EATAN
                case 0x7e: startP(std::exp(-double(s[fsf])),44); return true;              // EEXP
            }
            stop_reason="vu_lower_special "+std::to_string(sp); return false;
        }
        unsigned op=l>>25;
        uint32_t imm12=((l>>10)&0x800u)|(l&0x7ffu),imm24=l&0xffffffu;
        switch(op) {
            case 0x00: loadQ(uint32_t(int32_t(vis)+imm11),ft,dest); return true;    // LQ ft,imm(is)
            case 0x01: storeQ(uint32_t(int32_t(vit)+imm11),fs,dest); return true;   // SQ fs,imm(it)
            case 0x04: { uint8_t* m=qword(uint32_t(int32_t(vis)+imm11)); for(unsigned c=0;c<4;++c) if(dest&(8>>c)) { uint32_t w; std::memcpy(&w,m+c*4,4); setVi(it,w&0xffff); } return true; }   // ILW
            case 0x05: { uint8_t* m=qword(uint32_t(int32_t(vis)+imm11)); uint32_t w=vit; for(unsigned c=0;c<4;++c) if(dest&(8>>c)) std::memcpy(m+c*4,&w,4); return true; }   // ISW
            case 0x08: setVi(it,vis+((((l>>21)&15)<<11)|(l&0x7ff))); return true;  // IADDIU
            case 0x09: setVi(it,vis-((((l>>21)&15)<<11)|(l&0x7ff))); return true;  // ISUBIU
            case 0x10: setVi(1,(clip&0xffffffu)==imm24); return true;             // FCEQ
            case 0x11: clip=imm24; return true;                                   // FCSET
            case 0x12: setVi(1,(clip&imm24)!=0); return true;                     // FCAND
            case 0x13: setVi(1,((clip|imm24)&0xffffffu)==0xffffffu); return true; // FCOR
            case 0x14: setVi(it,(status&0xfffu)==imm12); return true;             // FSEQ
            case 0x15: status=(status&0x3fu)|(imm12&0xfc0u); return true;          // FSSET
            case 0x16: setVi(it,status&imm12); return true;                       // FSAND
            case 0x17: setVi(it,status|imm12); return true;                       // FSOR
            case 0x18: setVi(it,(mac&0xffffu)==vis); return true;                 // FMEQ
            case 0x1a: setVi(it,mac&vis); return true;                            // FMAND
            case 0x1b: setVi(it,(mac|vis)&0xffffu); return true;                  // FMOR
            case 0x1c: setVi(it,clip&0xfffu); return true;                        // FCGET
            case 0x20: branch=int64_t((pc+uint32_t(imm11))%code_pairs); return true;    // B
            case 0x21: setVi(it,pc+1); branch=int64_t((pc+uint32_t(imm11))%code_pairs); return true;   // BAL
            case 0x24: branch=int64_t(vis%code_pairs); return true;               // JR
            case 0x25: setVi(it,pc+1); branch=int64_t(vis%code_pairs); return true;   // JALR
            case 0x28: if(vit==vis) branch=int64_t((pc+uint32_t(imm11))%code_pairs); return true;   // IBEQ
            case 0x29: if(vit!=vis) branch=int64_t((pc+uint32_t(imm11))%code_pairs); return true;   // IBNE
            case 0x2c: if(int16_t(vis)<0) branch=int64_t((pc+uint32_t(imm11))%code_pairs); return true;    // IBLTZ
            case 0x2d: if(int16_t(vis)>0) branch=int64_t((pc+uint32_t(imm11))%code_pairs); return true;    // IBGTZ
            case 0x2e: if(int16_t(vis)<=0) branch=int64_t((pc+uint32_t(imm11))%code_pairs); return true;   // IBLEZ
            case 0x2f: if(int16_t(vis)>=0) branch=int64_t((pc+uint32_t(imm11))%code_pairs); return true;   // IBGEZ
        }
        stop_reason="vu_lower_opcode2 "+std::to_string(op); return false;
    }
    bool pair(uint32_t upper,uint32_t lower,int64_t& branch) {
        Result res;
        if(!upperOp(upper,res)) return false;
        if(upper&0x80000000u) i=fromBits(lower);                   // I bit: lower word is the I immediate
        else if(!lowerOp(lower,branch)) return false;
        commit(res);
        vf[0]={0,0,0,1};
        return true;
    }
};
}
