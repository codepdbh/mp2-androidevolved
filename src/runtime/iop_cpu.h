#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
namespace mp2r {
// R3000A (MIPS I) interpreter for IOP modules shipped on the disc. Memory and
// device access go through callbacks; unsupported instructions stop explicitly.
// Load-delay slots are not modelled: compiler output never reads a loaded register
// in the following instruction.
class IopCpu {
public:
    std::array<uint32_t,32> r{};
    uint32_t pc{},next_pc{},hi{},lo{};
    uint32_t cop0_status{},cop0_cause{},cop0_epc{};
    uint64_t instructions{};
    struct Bus {
        std::function<uint32_t(uint32_t,unsigned)> read;          // address, bytes (1/2/4)
        std::function<void(uint32_t,uint32_t,unsigned)> write;    // address, value, bytes
    } bus;
    // Called for SYSCALL/BREAK; return false to stop with the reason already set.
    std::function<bool(IopCpu&,uint32_t code,bool is_break)> trap;
    std::string stop_reason;

    void jump(uint32_t address) { pc=address; next_pc=address+4; }
    // Executes one instruction (and its effect on the delay slot). Returns false on stop.
    bool step() {
        uint32_t op=bus.read(pc,4);
        uint32_t current=pc; pc=next_pc; next_pc=pc+4;
        ++instructions;
        uint32_t rs=(op>>21)&31,rt=(op>>16)&31,rd=(op>>11)&31,sa=(op>>6)&31;
        int32_t simm=int16_t(op&0xffff); uint32_t imm=op&0xffff;
        auto set=[&](uint32_t reg,uint32_t v){ if(reg) r[reg]=v; };
        auto branch=[&](bool taken){ if(taken) next_pc=pc+(uint32_t(simm)<<2); };
        switch(op>>26) {
        case 0x00:
            switch(op&63) {
            case 0x00: set(rd,r[rt]<<sa); break;
            case 0x02: set(rd,r[rt]>>sa); break;
            case 0x03: set(rd,uint32_t(int32_t(r[rt])>>sa)); break;
            case 0x04: set(rd,r[rt]<<(r[rs]&31)); break;
            case 0x06: set(rd,r[rt]>>(r[rs]&31)); break;
            case 0x07: set(rd,uint32_t(int32_t(r[rt])>>(r[rs]&31))); break;
            case 0x08: next_pc=r[rs]; break;
            case 0x09: { uint32_t t=r[rs]; set(rd,current+8); next_pc=t; break; }
            case 0x0c: case 0x0d:
                if(!trap || !trap(*this,(op>>6)&0xfffff,(op&63)==0x0d)) { pc=current; return fail("iop_trap"); }
                break;
            case 0x10: set(rd,hi); break;
            case 0x11: hi=r[rs]; break;
            case 0x12: set(rd,lo); break;
            case 0x13: lo=r[rs]; break;
            case 0x18: { int64_t p=int64_t(int32_t(r[rs]))*int32_t(r[rt]); lo=uint32_t(p); hi=uint32_t(uint64_t(p)>>32); break; }
            case 0x19: { uint64_t p=uint64_t(r[rs])*r[rt]; lo=uint32_t(p); hi=uint32_t(p>>32); break; }
            case 0x1a: {
                int32_t n=int32_t(r[rs]),d=int32_t(r[rt]);
                if(d==0) { lo=n>=0?0xffffffffu:1u; hi=uint32_t(n); }
                else if(n==INT32_MIN && d==-1) { lo=uint32_t(INT32_MIN); hi=0; }
                else { lo=uint32_t(n/d); hi=uint32_t(n%d); }
                break;
            }
            case 0x1b: if(r[rt]==0) { lo=0xffffffffu; hi=r[rs]; } else { lo=r[rs]/r[rt]; hi=r[rs]%r[rt]; } break;
            case 0x20: case 0x21: set(rd,r[rs]+r[rt]); break;   // ADD overflow traps are not used by IRX code
            case 0x22: case 0x23: set(rd,r[rs]-r[rt]); break;
            case 0x24: set(rd,r[rs]&r[rt]); break;
            case 0x25: set(rd,r[rs]|r[rt]); break;
            case 0x26: set(rd,r[rs]^r[rt]); break;
            case 0x27: set(rd,~(r[rs]|r[rt])); break;
            case 0x2a: set(rd,int32_t(r[rs])<int32_t(r[rt])); break;
            case 0x2b: set(rd,r[rs]<r[rt]); break;
            default: pc=current; return fail("iop_unsupported_special");
            }
            break;
        case 0x01: {
            bool link=(rt&0x1e)==0x10,taken=(rt&1)?int32_t(r[rs])>=0:int32_t(r[rs])<0;
            if(link) r[31]=current+8;
            branch(taken); break;
        }
        case 0x02: next_pc=(pc&0xf0000000u)|((op&0x3ffffff)<<2); break;
        case 0x03: r[31]=current+8; next_pc=(pc&0xf0000000u)|((op&0x3ffffff)<<2); break;
        case 0x04: branch(r[rs]==r[rt]); break;
        case 0x05: branch(r[rs]!=r[rt]); break;
        case 0x06: branch(int32_t(r[rs])<=0); break;
        case 0x07: branch(int32_t(r[rs])>0); break;
        case 0x08: case 0x09: set(rt,r[rs]+uint32_t(simm)); break;
        case 0x0a: set(rt,int32_t(r[rs])<simm); break;
        case 0x0b: set(rt,r[rs]<uint32_t(simm)); break;
        case 0x0c: set(rt,r[rs]&imm); break;
        case 0x0d: set(rt,r[rs]|imm); break;
        case 0x0e: set(rt,r[rs]^imm); break;
        case 0x0f: set(rt,imm<<16); break;
        case 0x10:
            if(rs==0) set(rt,rd==12?cop0_status:rd==13?cop0_cause:rd==14?cop0_epc:0);
            else if(rs==4) { if(rd==12) cop0_status=r[rt]; else if(rd==13) cop0_cause=r[rt]; else if(rd==14) cop0_epc=r[rt]; }
            else if(rs==16 && (op&63)==0x10) cop0_status=(cop0_status&~0xfu)|((cop0_status>>2)&0xfu); // RFE
            else { pc=current; return fail("iop_unsupported_cop0"); }
            break;
        case 0x20: set(rt,uint32_t(int8_t(bus.read(r[rs]+simm,1)))); break;
        case 0x21: set(rt,uint32_t(int16_t(bus.read(r[rs]+simm,2)))); break;
        case 0x23: set(rt,bus.read(r[rs]+simm,4)); break;
        case 0x24: set(rt,bus.read(r[rs]+simm,1)&0xff); break;
        case 0x25: set(rt,bus.read(r[rs]+simm,2)&0xffff); break;
        case 0x22: case 0x26: { // LWL / LWR
            uint32_t a=r[rs]+simm,w=bus.read(a&~3u,4),s=(a&3)*8;
            if((op>>26)==0x22) set(rt,(r[rt]&(0x00ffffffu>>s))|(w<<(24-s)));
            else set(rt,(r[rt]&~(0xffffffffu>>s))|(w>>s));
            break;
        }
        case 0x28: bus.write(r[rs]+simm,r[rt]&0xff,1); break;
        case 0x29: bus.write(r[rs]+simm,r[rt]&0xffff,2); break;
        case 0x2b: bus.write(r[rs]+simm,r[rt],4); break;
        case 0x2a: case 0x2e: { // SWL / SWR
            uint32_t a=r[rs]+simm,w=bus.read(a&~3u,4),s=(a&3)*8;
            if((op>>26)==0x2a) w=(w&~(0xffffffffu>>(24-s)))|(r[rt]>>(24-s));
            else w=(w&~(0xffffffffu<<s))|(r[rt]<<s);
            bus.write(a&~3u,w,4); break;
        }
        default: pc=current; return fail("iop_unsupported_opcode");
        }
        return true;
    }
private:
    bool fail(const char* why) { if(stop_reason.empty()) stop_reason=why; return false; }
};
}
