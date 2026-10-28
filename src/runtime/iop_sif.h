#pragma once
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <ostream>
#include <string>
#include <vector>
namespace mp2r {
// High-level IOP endpoint of the SIF. The EE side runs the game's own sifcmd/sifrpc
// library code unchanged; MP2R answers it like a booted IOP: registers, SIF1 transfers
// into IOP RAM, system commands, RPC bind/call/rdata, and SIF0 replies delivered one at a
// time through DMAC channel 5. RPC servers are native implementations keyed by server ID.
// Protocol reference: PS2SDK ee/iop sifcmd+sifrpc (protocol-compatible open implementation).
class IopSif {
public:
    static constexpr uint32_t ram_size=0x200000;
    static constexpr uint32_t SIFINIT=0x10000,CMDINIT=0x20000,BOOTEND=0x40000;
    // Returns the IOP address of the reply data (copied to the EE receive buffer), or 0.
    using Handler=std::function<uint32_t(IopSif&,int function,uint32_t buffer,uint32_t size)>;
    // Servers implemented by IOP module code reply later through completeCall(ticket, reply).
    using AsyncHandler=std::function<void(uint32_t function,uint32_t buffer,uint32_t size,uint64_t ticket)>;
    struct Server { uint32_t sid,token,buffer,size; Handler handler; std::string module; AsyncHandler async; };
private:
    struct CallState { uint32_t rec,pkt,rpc,cd,recvbuf,rsize,rmode; };
    std::map<uint64_t,CallState> pending_calls_;
    uint64_t next_ticket_=1;
    uint8_t* ee_{};
    std::vector<uint8_t> ram_=std::vector<uint8_t>(ram_size);
    uint32_t mscom_{},smcom_{},msflag_{},smflag_{};
    std::array<uint32_t,64> system_regs_{};
    uint32_t ee_packet_buffer_{};
    uint32_t heap_=0x00020000;
    std::map<uint32_t,Server> servers_;
    struct Outgoing { std::vector<uint8_t> packet; uint32_t data_dest{}; std::vector<uint8_t> data; };
    std::deque<Outgoing> sif0_;
    bool sif0_busy_{},reboot_pending_{};
    uint32_t next_transfer_=1;
public:
    static constexpr uint32_t command_buffer=0x0001e000; // IOP-side SIFCMD receive buffer (MP2R layout)
    std::ostream* log{};
    std::string stop_reason;      // set when the guest needs an IOP service MP2R does not provide
    std::vector<std::string> modules;
    std::string reboot_image;
    std::function<void(IopSif&)> on_reboot; // re-registers resident servers after an IOP reset
    unsigned rpc_calls{};

    IopSif() { boot(); }
    uint8_t* ram(uint32_t address,uint32_t bytes) {
        address&=0x1fffff;
        if(uint64_t(address)+bytes>ram_size) throw std::runtime_error("IOP RAM access out of range");
        return ram_.data()+address;
    }
    uint32_t read32(uint32_t address) { uint32_t v; std::memcpy(&v,ram(address,4),4); return v; }
    void write32(uint32_t address,uint32_t v) { std::memcpy(ram(address,4),&v,4); }
    std::string readString(uint32_t address,uint32_t max) {
        std::string s; for(uint32_t i=0;i<max;++i) { char c=char(*ram(address+i,1)); if(!c) break; s+=c; } return s;
    }
    uint32_t allocate(uint32_t bytes) {
        uint32_t address=(heap_+63)&~63u; heap_=address+((bytes+63)&~63u);
        if(heap_>ram_size) throw std::runtime_error("IOP HLE heap exhausted");
        return address;
    }
    void addServer(uint32_t sid,uint32_t size,Handler handler,std::string module) {
        uint32_t token=allocate(0x40);            // stands in for the IOP SifRpcServerData_t
        servers_[sid]={sid,token,allocate(size),size,std::move(handler),std::move(module)};
    }
    bool hasServer(uint32_t sid) const { return servers_.count(sid)!=0; }
    void addLleServer(uint32_t sid,uint32_t token,uint32_t buffer,AsyncHandler handler,std::string module) {
        servers_[sid]={sid,token,buffer,0,{},std::move(module),std::move(handler)};
    }
    // User-defined SIF commands (cid without bit 31) handled by loaded IOP modules.
    std::function<bool(uint32_t cid,uint32_t packet)> user_command;
    void completeCall(uint64_t ticket,uint32_t reply) {
        auto it=pending_calls_.find(ticket);
        if(it==pending_calls_.end()) throw std::runtime_error("Unknown IOP RPC completion");
        auto state=it->second; pending_calls_.erase(it);
        finishCall(state,reply);
    }
    // IOP module -> EE command (sceSifSendCmd on the IOP): extra data then the packet.
    void sendFromIop(uint32_t cid,uint32_t packet,uint32_t psize,uint32_t ee_dest,uint32_t iop_src,uint32_t size) {
        Outgoing out; out.packet=copyOut(packet,(psize+3)&~3u);
        uint32_t header[3]={(psize&0xff)|(size<<8),ee_dest,cid};
        std::memcpy(out.packet.data(),header,12);
        out.data_dest=ee_dest; out.data=copyOut(iop_src,(size+3)&~3u);
        sif0_.push_back(std::move(out));
    }
    void pumpPending(uint32_t& dmac_stat) { if(ee_) pump(ee_,dmac_stat); }
    // Header-only system command with opt field; extra data lands at ee_dest before the packet.
    void sendSystemCommand(uint32_t cid,uint32_t opt,uint32_t ee_dest,std::vector<uint8_t> data) {
        Outgoing out; out.packet.resize(16);
        uint32_t header[4]={16u|uint32_t(data.size()<<8),ee_dest,cid,opt};
        std::memcpy(out.packet.data(),header,16);
        out.data_dest=ee_dest; out.data=std::move(data);
        sif0_.push_back(std::move(out));
    }
    // EE RAM as seen by IOP-side DMA (known once the EE has issued its first SIF transfer).
    uint8_t* eeMemory() const { if(!ee_) throw std::runtime_error("IOP access to EE RAM before any SIF transfer"); return ee_; }

    // ---- EE kernel SIF register syscalls (SifSetReg 0x79 / SifGetReg 0x7A)
    void setReg(uint32_t reg,uint32_t value) {
        if(reg&0x80000000u) { system_regs_[reg&63u]=value; return; }
        switch(reg) {
            case 1: mscom_=value; break;
            case 3: msflag_|=value; break;          // EE sets MSFLAG bits
            case 4: smflag_&=~value; break;         // EE clears SMFLAG bits
            default: break;
        }
    }
    uint32_t getReg(uint32_t reg) {
        if(reg&0x80000000u) return system_regs_[reg&63u];
        if(reboot_pending_ && reg==4) completeReboot();
        switch(reg) { case 1: return mscom_; case 2: return smcom_; case 3: return msflag_; case 4: return smflag_; default: return 0; }
    }

    // ---- SifSetDma (0x77): descriptors {src, dest, size, attr} in EE RAM.
    uint32_t setDma(uint8_t* ee,uint32_t descriptors,uint32_t count) {
        ee_=ee;
        for(uint32_t i=0;i<count;++i) {
            uint32_t d[4]; std::memcpy(d,ee+((descriptors+i*16)&0x1ffffff),16);
            uint32_t src=d[0]&0x1ffffff,dest=d[1]&0x1fffff,size=(d[2]+3)&~3u;
            std::memcpy(ram(dest,size),ee+src,size);
            if(dest==command_buffer) command(ee);
        }
        return next_transfer_++;
    }
    // SifSetDChain (0x78): the EE handler consumed the previous SIF0 packet.
    void setDChain(uint8_t* ee,uint32_t& dmac_stat) { sif0_busy_=false; pump(ee,dmac_stat); }
    void pump(uint8_t* ee,uint32_t& dmac_stat) {
        if(sif0_busy_ || sif0_.empty() || !ee_packet_buffer_) return;
        auto out=std::move(sif0_.front()); sif0_.pop_front();
        if(!out.data.empty()) std::memcpy(ee+(out.data_dest&0x1ffffff),out.data.data(),out.data.size());
        std::memcpy(ee+(ee_packet_buffer_&0x1ffffff),out.packet.data(),out.packet.size());
        dmac_stat|=1u<<5; sif0_busy_=true;
    }
private:
    void boot() {
        servers_.clear(); sif0_.clear(); sif0_busy_=false; heap_=0x00020000; pending_calls_.clear();
        smcom_=command_buffer; smflag_|=SIFINIT|CMDINIT|BOOTEND;
        if(on_reboot) on_reboot(*this);
    }
    void completeReboot() {
        reboot_pending_=false; ee_packet_buffer_=0; boot();
        if(log) *log<<"IOP reboot complete image="<<reboot_image<<'\n';
    }
    void send(uint32_t cid,std::vector<uint32_t> words,uint32_t data_dest=0,std::vector<uint8_t> data={}) {
        Outgoing out; out.packet.resize(16+words.size()*4);
        uint32_t header[4]={uint32_t(out.packet.size())|uint32_t(data.size()<<8),data_dest,cid,0};
        std::memcpy(out.packet.data(),header,16);
        std::memcpy(out.packet.data()+16,words.data(),words.size()*4);
        out.data_dest=data_dest; out.data=std::move(data);
        sif0_.push_back(std::move(out));
    }
    std::vector<uint8_t> copyOut(uint32_t address,uint32_t size) {
        std::vector<uint8_t> v(size); if(size) std::memcpy(v.data(),ram(address,size),size); return v;
    }
    void command(uint8_t* ee) {
        uint32_t p=command_buffer,cid=read32(p+8),opt=read32(p+12);
        switch(cid) {
            case 0x80000000: ee_packet_buffer_=read32(p+16); return;               // CHANGE_SADDR
            case 0x80000002:                                                        // INIT_CMD
                if(opt==0) ee_packet_buffer_=read32(p+16);
                else send(0x80000001,{0,1});                                        // SET_SREG RPCINIT=1
                return;
            case 0x80000003:                                                        // RESET_CMD
                reboot_image=readString(p+24,std::min<uint32_t>(read32(p+16),80));
                if(log) *log<<"IOP reset requested arg="<<reboot_image<<'\n';
                reboot_pending_=true; return;
            case 0x80000009: {                                                      // RPC_BIND
                uint32_t rec=read32(p+16),pkt=read32(p+20),rpc=read32(p+24),cd=read32(p+28),sid=read32(p+32);
                auto it=servers_.find(sid);
                if(it==servers_.end()) { stop_reason="iop_rpc_server_missing sid=0x"+hex(sid); return; }
                if(log) *log<<"SIF bind sid=0x"<<std::hex<<sid<<std::dec<<" ("<<it->second.module<<")\n";
                send(0x80000008,{rec,pkt,rpc,cd,0x80000009,it->second.token,it->second.buffer,0});
                return;
            }
            case 0x8000000a: {                                                      // RPC_CALL
                uint32_t rec=read32(p+16),pkt=read32(p+20),rpc=read32(p+24),cd=read32(p+28),fno=read32(p+32),
                         ssize=read32(p+36),recvbuf=read32(p+40),rsize=read32(p+44),rmode=read32(p+48),sd=read32(p+52);
                const Server* server=nullptr;
                for(auto& [sid,s]:servers_) if(s.token==sd) server=&s;
                if(!server) { stop_reason="iop_rpc_call_unbound sd=0x"+hex(sd); return; }
                ++rpc_calls;
                CallState state{rec,pkt,rpc,cd,recvbuf,rsize,rmode};
                if(server->async) {
                    auto ticket=next_ticket_++; pending_calls_[ticket]=state;
                    server->async(fno,server->buffer,ssize,ticket); return;
                }
                uint32_t reply=server->handler(*this,int(fno),server->buffer,ssize);
                if(!stop_reason.empty()) return;
                finishCall(state,reply);
                return;
            }
            case 0x8000000c: {                                                      // RPC_RDATA
                uint32_t rec=read32(p+16),pkt=read32(p+20),rpc=read32(p+24),cd=read32(p+28),
                         src=read32(p+32),dest=read32(p+36),size=read32(p+40);
                send(0x80000008,{rec,pkt,rpc,cd,0x8000000c,0,0,0},dest,copyOut(src,(size+3)&~3u));
                return;
            }
            default:
                if(!(cid&0x80000000u) && user_command && user_command(cid,p)) return;
                stop_reason="iop_sif_command_unsupported cid=0x"+hex(cid);
        }
    }
    void finishCall(const CallState& s,uint32_t reply) {
        auto data=reply?copyOut(reply,(s.rsize+3)&~3u):std::vector<uint8_t>{};
        if(s.rmode) { send(0x80000008,{s.rec,s.pkt,s.rpc,s.cd,0x8000000a,0,0,0},s.recvbuf,std::move(data)); return; }
        if(!ee_) throw std::runtime_error("IOP RPC completion before any EE transfer");
        if(!data.empty()) std::memcpy(ee_+(s.recvbuf&0x1ffffff),data.data(),data.size());
        uint32_t done[12]={64,0,0x80000008,0,0,s.pkt,0,s.cd,0x8000000a,0,0,0}; // rec_id/rpc_id 0: completion
        std::memcpy(ee_+(s.pkt&0x1ffffff),done,sizeof done);
    }
    static std::string hex(uint32_t v) { char b[16]; std::snprintf(b,sizeof b,"%x",v); return b; }
};
}
