#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
namespace mp2r {
// VIF command processor (VIF1 layout; VIF0 lacks DIRECT/BASE/OFFSET/MSKPATH3). Consumes 32-bit
// words from DMA or FIFO writes; commands may span qwords and transfers. UNPACK honours the
// CL/WL cycle (skipping when CL>=WL, filling from COL when CL<WL), STMOD offset/difference via
// ROW, STMASK/STCOL write masks, USN and FLG (+TOPS). VIF1 double buffering: OFFSET sets DBF=0
// and TOPS=BASE; each microprogram start sets TOP=TOPS, toggles DBF, TOPS=BASE+DBF*OFST.
class EeVif {
public:
    uint8_t* vu_code{}; uint32_t code_size{};
    uint8_t* vu_data{}; uint32_t data_size{};
    std::function<void(const uint8_t* qwords,uint32_t count)> direct;    // PATH2 GIF data
    std::function<bool(uint32_t address)> start_microprogram;            // false: stop
    std::string stop_reason;
    bool path3_masked{};                                                    // set by MSKPATH3 (VIF1)
    std::function<void()> path3_unmasked;                                  // held PATH3 transfers resume
    uint32_t cl=1,wl=1,mode{},mask{},itop{},itops{},base{},ofst{},tops{},top{},mark{},num{},code{};
    bool dbf{};
    std::array<uint32_t,4> row{},col{};
    uint64_t commands{},unpacked_qwords{},direct_qwords{},mpg_words{};
    bool vif1=true;

    void feed(const uint8_t* bytes,uint32_t count_words) {
        for(uint32_t i=0;i<count_words && stop_reason.empty();++i) { uint32_t w; std::memcpy(&w,bytes+i*4,4); word(w); }
    }
private:
    enum class State { Command,Strow,Stcol,Stmask,Mpg,Direct,Unpack } state_=State::Command;
    uint32_t remaining_{},index_{},addr_{};
    std::vector<uint8_t> pending_;
    uint32_t unpack_cmd_{},cycle_{},written_{},total_{},vector_bytes_{};
    void word(uint32_t w) {
        switch(state_) {
        case State::Command: command(w); return;
        case State::Strow: row[index_++]=w; if(index_==4) state_=State::Command; return;
        case State::Stcol: col[index_++]=w; if(index_==4) state_=State::Command; return;
        case State::Stmask: mask=w; state_=State::Command; return;
        case State::Mpg:
            if(addr_+4>code_size) { stop_reason="vif_mpg_overflow"; return; }
            std::memcpy(vu_code+addr_,&w,4); addr_+=4; ++mpg_words;
            if(--remaining_==0) state_=State::Command; return;
        case State::Direct:
            append(w);
            if(pending_.size()==16) { if(direct) direct(pending_.data(),1); pending_.clear(); ++direct_qwords; if(--remaining_==0) state_=State::Command; }
            return;
        case State::Unpack:
            append(w); drain();
            if(--remaining_==0) { pending_.clear(); while(written_<total_ && filling()) store(nullptr); state_=State::Command; }
            return;
        }
    }
    void append(uint32_t w) { auto n=pending_.size(); pending_.resize(n+4); std::memcpy(pending_.data()+n,&w,4); }
    void command(uint32_t w) {
        code=w; ++commands;
        uint32_t cmd=(w>>24)&0x7f,imm=w&0xffff,n8=(w>>16)&0xff;
        if(cmd>=0x60) { beginUnpack(w); return; }
        switch(cmd) {
            case 0x00: return;                                                     // NOP
            case 0x01: cl=imm&0xff; wl=(imm>>8)&0xff; return;                      // STCYCL
            case 0x02: if(vif1) { ofst=imm&0x3ff; dbf=false; tops=base; } return;  // OFFSET
            case 0x03: if(vif1) base=imm&0x3ff; return;                            // BASE
            case 0x04: itops=imm&0x3ff; return;                                    // ITOP
            case 0x05: mode=imm&3; return;                                         // STMOD
            case 0x06:                                                             // MSKPATH3: GIF_STAT.M3P
                if(vif1) { bool was=path3_masked; path3_masked=(imm>>15)&1; if(was && !path3_masked && path3_unmasked) path3_unmasked(); }
                return;
            case 0x07: mark=imm; return;                                           // MARK
            case 0x10: case 0x11: case 0x13: return;                               // FLUSHE/FLUSH/FLUSHA: synchronous
            case 0x14: case 0x15: case 0x17: {                                     // MSCAL / MSCALF / MSCNT
                itop=itops; top=tops;
                if(vif1) { dbf=!dbf; tops=base+(dbf?ofst:0); }
                if(!start_microprogram || !start_microprogram(cmd==0x17?0xffffffffu:imm*8))
                    if(stop_reason.empty()) stop_reason="vu_microprogram_required";
                return;
            }
            case 0x20: state_=State::Stmask; return;
            case 0x30: state_=State::Strow; index_=0; return;
            case 0x31: state_=State::Stcol; index_=0; return;
            case 0x4a: remaining_=(n8?n8:256)*2; addr_=imm*8; state_=State::Mpg; return;
            case 0x50: case 0x51:                                                  // DIRECT / DIRECTHL
                if(!vif1) break;
                remaining_=imm?imm:65536; pending_.clear(); state_=State::Direct; return;
        }
        stop_reason="vif_command_unsupported "+std::to_string(cmd);
    }
    bool filling() const { return cl<wl && (cycle_%wl)>=cl; }
    void beginUnpack(uint32_t w) {
        unpack_cmd_=w; total_=(w>>16)&0xff; if(!total_) total_=256;
        num=total_; written_=0; cycle_=0;
        addr_=w&0x3ff; if((w>>15)&1) addr_+=tops;                                // FLG: relative to TOPS
        pending_.clear();
        uint32_t vn=(w>>26)&3,vl=(w>>24)&3;
        uint32_t bits=(vn==3 && vl==3)?16:(vn+1)*(32>>vl);
        vector_bytes_=bits/8;
        uint32_t inputs=cl>=wl?total_:(total_/wl)*cl+std::min(total_%wl,cl);
        remaining_=(inputs*bits+31)/32;
        if(remaining_) { state_=State::Unpack; drain(); }
        else { while(written_<total_) store(nullptr); state_=State::Command; }
    }
    void drain() {
        while(written_<total_) {
            if(filling()) { store(nullptr); continue; }
            if(pending_.size()<vector_bytes_) return;
            store(pending_.data());
            pending_.erase(pending_.begin(),pending_.begin()+vector_bytes_);
        }
    }
    void store(const uint8_t* src) {
        uint32_t cycle=cycle_%std::max(wl,1u),row_index=std::min(cycle,3u);
        std::array<uint32_t,4> v{};
        if(src) decode(src,v);
        uint32_t dest=(addr_*16)%data_size;
        uint32_t m=(mask>>(row_index*8))&0xff;
        for(int i=0;i<4;++i) {
            uint32_t sel=src?(m>>(i*2))&3:2,value,current;
            std::memcpy(&current,vu_data+dest+i*4,4);
            switch(sel) {
                case 0: value=mode==1?v[i]+row[i]:mode==2?(row[i]+=v[i]):v[i]; break;
                case 1: value=row[i]; break;
                case 2: value=col[row_index]; break;
                default: value=current; break;
            }
            std::memcpy(vu_data+dest+i*4,&value,4);
        }
        ++written_; ++unpacked_qwords; ++cycle_; ++addr_;
        if(cl>=wl && wl && (cycle_%wl)==0) addr_+=cl-wl;                          // skipping write
    }
    void decode(const uint8_t* s,std::array<uint32_t,4>& v) const {
        uint32_t vn=(unpack_cmd_>>26)&3,vl=(unpack_cmd_>>24)&3; bool usn=(unpack_cmd_>>14)&1;
        if(vn==3 && vl==3) {                                                       // V4-5 (RGBA5551)
            uint16_t c; std::memcpy(&c,s,2);
            v={uint32_t((c&0x1f)<<3),uint32_t(((c>>5)&0x1f)<<3),uint32_t(((c>>10)&0x1f)<<3),uint32_t((c>>15)<<7)}; return;
        }
        auto get=[&](uint32_t i)->uint32_t{
            if(vl==0) { uint32_t x; std::memcpy(&x,s+i*4,4); return x; }
            if(vl==1) { uint16_t x; std::memcpy(&x,s+i*2,2); return usn?x:uint32_t(int32_t(int16_t(x))); }
            uint8_t x=s[i]; return usn?x:uint32_t(int32_t(int8_t(x)));
        };
        uint32_t n=vn+1;
        if(n==1) { uint32_t x=get(0); v={x,x,x,x}; return; }                       // S-xx broadcast
        for(uint32_t i=0;i<4;++i) v[i]=i<n?get(i):0;
    }
};
}
