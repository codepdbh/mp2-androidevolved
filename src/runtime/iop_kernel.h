#pragma once
#include "iop_cpu.h"
#include "iop_sif.h"
#include "disc_image.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <chrono>
#include <deque>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace mp2r {
// Virtual IOP kernel: runs the game's IRX modules on IopCpu and provides the IOP
// kernel libraries natively (thread manager, semaphores, event flags, interrupts,
// memory, SIF). Imports bound to another loaded module link to its real code.
// An import with neither implementation stops with "iop_import_missing lib::index".
class IopKernel {
public:
    using Hle=std::function<void(IopKernel&)>;
    enum class State { Dormant,Ready,Waiting };
    enum class Wait { None,Sema,Event,Sleep,Delay,RpcLoop,Vblank };
    struct Thread {
        int id{}; uint32_t attr{},entry{},stack{},stack_size{},gp{},option{}; int priority{},initial_priority{};
        State state=State::Dormant; Wait wait=Wait::None; int wait_id{}; uint64_t order{};
        std::array<uint32_t,32> r{}; uint32_t pc{},hi{},lo{};
        int wakeups{}; std::chrono::steady_clock::time_point wake_at{};
        uint32_t event_bits{},event_mode{},event_result{}; // WaitEventFlag arguments
    };
    struct Sema { int count{},max{}; uint32_t attr{},option{}; };
    struct EventFlag { uint32_t bits{},attr{},option{},initial{}; };
    struct RpcServer { uint32_t sd,sid,func,buf,queue; };
    struct Alarm { std::chrono::steady_clock::time_point due; uint32_t handler,arg; };
    std::vector<Alarm> alarms;
    // MP2R_IOP_TRACE_AT=<hex pc> logs 3000 IOP control transfers from the first time a thread reaches it.
    uint32_t trace_at=[]{ const char* v=std::getenv("MP2R_IOP_TRACE_AT"); return v?uint32_t(std::strtoul(v,nullptr,16)):0u; }();
    unsigned trace_left{},printf_trace{};
    unsigned send_cmd_trace{},user_cmd_trace{},wake_trace{};
    uint32_t mc_command_handler{},mc_devid_handler{},kelf_path_callback{},strtok_next{};
    std::array<bool,64> irq_enabled{};
    // C printf formatting for sysclib sprintf/vsprintf: writes the result at dest, returns its length.
    uint32_t format(uint32_t dest,uint32_t fmt,const std::function<uint32_t()>& next) {
        std::string out=formatString(fmt,next);
        if(!stop_reason.empty()) return 0;
        std::memcpy(sif.ram(dest,out.size()+1),out.c_str(),out.size()+1);
        return uint32_t(out.size());
    }
    std::string formatString(uint32_t fmt,const std::function<uint32_t()>& next) {
        std::string f=sif.readString(fmt,0x10000),out;
        for(size_t i=0;i<f.size();++i) {
            if(f[i]!='%') { out+=f[i]; continue; }
            std::string spec="%"; ++i;
            while(i<f.size() && std::strchr("-+ #0",f[i])) spec+=f[i++];
            while(i<f.size() && (std::isdigit(uint8_t(f[i])) || f[i]=='.' || f[i]=='*')) {
                if(f[i]=='*') { spec+=std::to_string(int32_t(next())); ++i; } else spec+=f[i++];
            }
            while(i<f.size() && (f[i]=='l' || f[i]=='h')) ++i;
            if(i>=f.size()) break;
            char c=f[i]; char buf[512];
            switch(c) {
                case 'd': case 'i': std::snprintf(buf,sizeof buf,(spec+"d").c_str(),int32_t(next())); break;
                case 'u': case 'x': case 'X': case 'o': std::snprintf(buf,sizeof buf,(spec+c).c_str(),next()); break;
                case 'p': std::snprintf(buf,sizeof buf,"%x",next()); break;
                case 'c': std::snprintf(buf,sizeof buf,(spec+"c").c_str(),int(next()&0xff)); break;
                case 's': { uint32_t p=next(); auto s=p?sif.readString(p,0x10000):std::string("(null)"); std::snprintf(buf,sizeof buf,(spec+"s").c_str(),s.c_str()); break; }
                case '%': std::snprintf(buf,sizeof buf,"%%"); break;
                default: stop(std::string("iop_printf_conversion ")+c); return {};
            }
            out+=buf;
        }
        return out;
    }
    // Device models that raise IOP interrupts on their own clock (e.g. SPU2 ADMA) poll here.
    std::vector<std::function<void(IopKernel&)>> ticks;
    struct PendingCall { uint32_t sd,fno,buf,size; uint64_t ticket; };

    IopSif& sif;
    IopCpu cpu;
    std::ostream* log{};
    std::function<uint8_t*(uint32_t)> ee_memory;           // EE RAM for IOP->EE SIF DMA
    std::string stop_reason;
    std::map<std::pair<std::string,int>,Hle> hle;           // native libraries
    std::vector<std::string> loaded;
    DiscImage* disc{};
    uint64_t cd_reads{};
    int cd_stream_flag{},cd_intr_event{};
    unsigned cd_trace{},cd_sc_trace{};
    std::array<uint32_t,2> cd_stm_callback{};
    std::vector<uint32_t> cd_pending_callbacks;
    bool cd_in_callback{};
    std::map<std::string,uint32_t> devices;           // ioman device name -> iop_device_t

    // Sentinel return addresses inside the unused low IOP page.
    static constexpr uint32_t thread_exit=0xf00,rpc_return=0xf10,call_return=0xf20;

    explicit IopKernel(IopSif& s):sif(s) {
        cpu.bus.read=[this](uint32_t a,unsigned n){ return read(a,n); };
        cpu.bus.write=[this](uint32_t a,uint32_t v,unsigned n){ write(a,v,n); };
        cpu.trap=[this](IopCpu&,uint32_t code,bool brk){ stop("iop_guest_"+std::string(brk?"break":"syscall")+"_"+std::to_string(code)); return false; };
        installCore();
    }

    // ---------------- memory
    uint32_t read(uint32_t a,unsigned n) {
        uint32_t p=a&0x1fffffffu,v=0;
        if(p<IopSif::ram_size) { std::memcpy(&v,sif.ram(p,n),n); return v; }
        if(p>=0x1f800000u && p<0x1f800400u) { std::memcpy(&v,scratch_.data()+(p-0x1f800000u),n); return v; }
        stop("iop_mmio_read 0x"+hex(a)+" pc=0x"+hex(cpu.pc)+" ra=0x"+hex(cpu.r[31])); return 0;
    }
    void write(uint32_t a,uint32_t v,unsigned n) {
        uint32_t p=a&0x1fffffffu;
        if(p<IopSif::ram_size) { std::memcpy(sif.ram(p,n),&v,n); return; }
        if(p>=0x1f800000u && p<0x1f800400u) { std::memcpy(scratch_.data()+(p-0x1f800000u),&v,n); return; }
        stop("iop_mmio_write 0x"+hex(a)+" pc=0x"+hex(cpu.pc)+" ra=0x"+hex(cpu.r[31])+" a0-a3=0x"+hex(cpu.r[4])+",0x"+hex(cpu.r[5])+",0x"+hex(cpu.r[6])+",0x"+hex(cpu.r[7])+" s0-s2=0x"+hex(cpu.r[16])+",0x"+hex(cpu.r[17])+",0x"+hex(cpu.r[18])+" thread="+std::to_string(current_));
    }
    uint32_t arg(int i) { return i<4?cpu.r[4+i]:read(cpu.r[29]+16+4*(i-4),4); }
    void ret(uint32_t v) { cpu.r[2]=v; }
    void stop(std::string why) { if(stop_reason.empty()) stop_reason=std::move(why); cpu.stop_reason=stop_reason; }

    // ---------------- module loading (IRX: ELF type 0xFF80 with .iopmod and REL sections)
    bool loadModule(const std::vector<uint8_t>& irx,const std::string& name,const std::string& args) {
        auto u16=[&](size_t o){ uint16_t v; std::memcpy(&v,irx.data()+o,2); return v; };
        auto u32=[&](size_t o){ uint32_t v; std::memcpy(&v,irx.data()+o,4); return v; };
        if(irx.size()<52 || u32(0)!=0x464c457fu || u16(16)!=0xff80) { stop("iop_module_not_irx "+name); return false; }
        uint32_t phoff=u32(28),shoff=u32(32),shnum=u16(48);
        uint32_t info_off=u32(phoff+4),load_off=u32(phoff+32+4),filesz=u32(phoff+32+16),memsz=u32(phoff+32+20);
        uint32_t entry=u32(info_off+4),gp=u32(info_off+8),bss=u32(info_off+20);
        uint32_t base=sif.allocate(memsz+bss+64);
        std::memcpy(sif.ram(base,filesz),irx.data()+load_off,filesz);
        std::memset(sif.ram(base+filesz,memsz-filesz+bss),0,memsz-filesz+bss);
        for(uint32_t s=0;s<shnum;++s) {
            size_t sh=shoff+s*40;
            if(u32(sh+4)!=9) continue; // SHT_REL
            uint32_t off=u32(sh+16),size=u32(sh+20);
            std::vector<std::pair<uint32_t,uint32_t>> pending_hi;
            for(uint32_t r=off;r<off+size;r+=8) {
                uint32_t where=base+u32(r),type=u32(r+4)&0xff,w=read(where,4);
                switch(type) {
                    case 2: write(where,w+base,4); break;                                         // R_MIPS_32
                    case 4: write(where,(w&0xfc000000u)|(((w&0x3ffffffu)+(base>>2))&0x3ffffffu),4); break; // R_MIPS_26
                    case 5: pending_hi.push_back({where,w}); break;                                // R_MIPS_HI16
                    case 6: {                                                                      // R_MIPS_LO16
                        uint32_t lo=uint32_t(int32_t(int16_t(w&0xffff)));
                        for(auto [hw,hv]:pending_hi) {
                            uint32_t value=((hv&0xffff)<<16)+lo+base;
                            write(hw,(hv&0xffff0000u)|(((value+0x8000u)>>16)&0xffff),4);
                        }
                        pending_hi.clear();
                        write(where,(w&0xffff0000u)|((lo+base)&0xffff),4);
                        break;
                    }
                    case 0: break;
                    default: stop("iop_relocation_unsupported "+std::to_string(type)); return false;
                }
            }
        }
        linkImports(base,memsz);
        loaded.push_back(name);
        // Module entry: _start(argc, argv) runs to completion on a loader context.
        uint32_t argv=sif.allocate(0x200),strings=argv+0x40,argc=0;
        std::vector<std::string> list{name};
        for(size_t p=0;p<args.size();) { auto e=args.find('\0',p); list.push_back(args.substr(p,e-p)); if(e==std::string::npos) break; p=e+1; }
        for(auto& s:list) { if(argc>=15) break; write(argv+4*argc++,strings,4); std::memcpy(sif.ram(strings,s.size()+1),s.c_str(),s.size()+1); strings+=uint32_t(s.size()+1); }
        write(argv+4*argc,0,4);
        uint32_t result=callGuest(base+entry,{argc,argv},base+gp);
        if(log) *log<<"IOP module "<<name<<" base=0x"<<std::hex<<base<<" entry=0x"<<base+entry<<" result="<<result<<std::dec<<'\n';
        return stop_reason.empty() && (result&3)!=1; // NO_RESIDENT_END keeps nothing loaded
    }
    // Runs guest code to its return on a private stack; it must not block.
    uint32_t callGuest(uint32_t pc,std::vector<uint32_t> args,std::optional<uint32_t> gp={}) {
        auto saved=snapshot();
        if(!call_stack_) call_stack_=sif.allocate(0x4000)+0x4000;
        for(size_t i=0;i<args.size() && i<4;++i) cpu.r[4+i]=args[i];
        if(gp) cpu.r[28]=*gp;
        cpu.r[29]=call_stack_-0x80; cpu.r[31]=call_return; cpu.jump(pc);
        for(size_t i=4;i<args.size();++i) write(cpu.r[29]+16+uint32_t(i-4)*4,args[i],4);   // o32 stack arguments
        ++nested_;
        uint64_t budget=50000000;
        while(cpu.pc!=call_return && stop_reason.empty() && budget--) {
            if(dispatchImport()) continue;
            if(!cpu.step()) { stop(cpu.stop_reason); break; }
        }
        --nested_;
        if(budget==uint64_t(-1)) stop("iop_guest_call_budget 0x"+hex(pc));
        uint32_t v=cpu.r[2]; restore(saved); return v;
    }

    // ---------------- scheduler: runs IOP threads until all wait or the budget expires
    void run(uint64_t budget) {
        while(budget && stop_reason.empty()) {
            wakeDelayed();
            preempt();
            if(!current_ && !schedule()) return;
            auto& t=threads_.at(current_);
            if(cpu.pc==thread_exit) { t.state=State::Dormant; current_=0; continue; }
            if(cpu.pc==rpc_return) { finishRpc(); continue; }
            if(cpu.pc==trace_at && trace_at) { trace_at=0; trace_left=3000; if(log) *log<<"IOP trace start thread "<<current_<<std::endl; }
            uint32_t before=cpu.pc;
            if(dispatchImport()) {
                if(trace_left && log) { --trace_left; *log<<"IT import@0x"<<std::hex<<before<<" ra=0x"<<cpu.r[31]<<" v0=0x"<<cpu.r[2]<<" a0=0x"<<cpu.r[4]<<" a1=0x"<<cpu.r[5]<<" t="<<std::dec<<current_<<std::endl; }
                --budget; continue;
            }
            if(!cpu.step()) { stop(cpu.stop_reason); return; }
            if(trace_left && cpu.pc!=before+4 && log) { --trace_left; *log<<"IT 0x"<<std::hex<<before<<"->0x"<<cpu.pc<<std::dec<<std::endl; }
            --budget;
        }
    }
    // Diagnostics: threads with state/wait/pc, kernel objects and alarms.
    void dumpState(std::ostream& out) const {
        static const char* states[]={"dormant","ready","waiting"};
        static const char* waits[]={"-","sema","event","sleep","delay","rpcloop","vblank"};
        out<<"IOP threads (current "<<current_<<"):\n";
        for(const auto& [id,t]:threads_)
            out<<"  #"<<id<<" prio="<<t.priority<<' '<<states[int(t.state)]<<" wait="<<waits[int(t.wait)]<<" id="<<t.wait_id
               <<" pc=0x"<<std::hex<<t.pc<<" entry=0x"<<t.entry<<std::dec<<'\n';
        for(const auto& [id,e]:events_) out<<"  event #"<<id<<" bits=0x"<<std::hex<<e.bits<<std::dec<<'\n';
        for(const auto& [id,s]:semas_) out<<"  sema #"<<id<<" count="<<s.count<<'\n';
        out<<"  alarms="<<alarms.size()<<" pending rpc calls="<<calls_.size()<<(active_call_?" (one active)":"")<<'\n';
    }
    // Drive command completion: with streaming active, cdvdman's interrupt handler calls the
    // streaming module's callback (flag 1 -> stm0, 2 -> stm1). Callbacks that start a new command
    // are queued so completions do not nest.
    void cdCommandComplete() {
        uint32_t cb=cd_stream_flag==1?cd_stm_callback[0]:cd_stream_flag==2?cd_stm_callback[1]:0;
        if(!cb) return;
        cd_pending_callbacks.push_back(cb);
        if(cd_in_callback) return;
        cd_in_callback=true;
        uint32_t saved_v0=cpu.r[2];
        while(!cd_pending_callbacks.empty() && stop_reason.empty()) {
            uint32_t next=cd_pending_callbacks.front(); cd_pending_callbacks.erase(cd_pending_callbacks.begin());
            callGuest(next,{1});
        }
        cd_in_callback=false; cpu.r[2]=saved_v0;
    }
    // Video timing shared with the EE: wakes threads in WaitVblankStart (id 0) / WaitVblankEnd (id 1).
    void signalVblank(bool start) {
        for(auto& [id,t]:threads_)
            if(t.state==State::Waiting && t.wait==Wait::Vblank && t.wait_id==(start?0:1)) ready(t,0);
        in_vblank=start;
    }
    bool in_vblank{};
    // iomanX devctl on a device registered by a loaded module (ops entry 23):
    // devctl(iop_file_t*, name, cmd, arg, arglen, buf, buflen). Returns nullopt without that device.
    std::optional<int32_t> deviceControl(const std::string& device,uint32_t cmd,const std::vector<uint8_t>& in,std::vector<uint8_t>& out) {
        auto it=devices.find(device); if(it==devices.end()) return std::nullopt;
        uint32_t ops=read(it->second+16,4),devctl=read(ops+23*4,4);
        if(!devctl) return std::nullopt;
        if(in.size()+out.size()>0x3c0) { stop("iop_devctl_buffer_too_large"); return 0; }
        if(!devctl_scratch_) devctl_scratch_=sif.allocate(0x400);                  // reused across calls
        uint32_t scratch=devctl_scratch_;
        uint32_t file=scratch,name=scratch+0x10,arg=scratch+0x40,buf=arg+uint32_t(in.size());
        write(file,0,4); write(file+4,0,4); write(file+8,it->second,4); write(file+12,0,4);   // mode, unit 0, device, privdata
        write(name,0,1);
        if(!in.empty()) std::memcpy(sif.ram(arg,uint32_t(in.size())),in.data(),in.size());
        auto result=int32_t(callGuest(devctl,{file,name,cmd,arg,uint32_t(in.size()),buf,uint32_t(out.size())}));
        if(!out.empty()) std::memcpy(out.data(),sif.ram(buf,uint32_t(out.size())),out.size());
        return result;
    }
    // IOP reboot: every module, thread and kernel object disappears.
    void reset() {
        stubs_.clear(); libraries_.clear(); threads_.clear(); semas_.clear(); events_.clear();
        queue_thread_.clear(); servers_.clear(); calls_.clear(); cmd_handlers_.clear(); devices.clear(); alarms.clear(); cd_intr_event=0; cd_stm_callback={}; cd_pending_callbacks.clear();
        active_call_.reset(); rpc_saved_.reset(); current_=0; call_stack_=0; devctl_scratch_=0; loaded.clear();
        sif.user_command=nullptr;
    }
    bool idle() const {
        for(auto& [id,t]:threads_) if(t.state==State::Ready) return false;
        return current_==0;
    }
    uint64_t instructions() const { return cpu.instructions; }

private:
    std::array<uint8_t,0x400> scratch_{};
    std::map<uint32_t,std::pair<std::string,int>> stubs_;
    std::map<std::string,std::vector<uint32_t>> libraries_;   // LLE exports
    std::map<int,Thread> threads_;
    std::map<int,Sema> semas_;
    std::map<int,EventFlag> events_;
    std::map<uint32_t,int> queue_thread_;                       // RPC queue -> serving thread
    std::map<uint32_t,RpcServer> servers_;                      // by sd
    std::deque<PendingCall> calls_;
    std::map<uint32_t,std::pair<uint32_t,uint32_t>> cmd_handlers_; // cid -> handler, arg
    std::optional<PendingCall> active_call_;
    std::optional<Thread> rpc_saved_;
    int next_id_=1,current_{}; uint64_t order_{}; int nested_{};
    uint32_t call_stack_{},devctl_scratch_{};
    struct Snapshot { std::array<uint32_t,32> r; uint32_t pc,next_pc,hi,lo; };
    Snapshot snapshot() const { return {cpu.r,cpu.pc,cpu.next_pc,cpu.hi,cpu.lo}; }
    void restore(const Snapshot& s) { cpu.r=s.r; cpu.pc=s.pc; cpu.next_pc=s.next_pc; cpu.hi=s.hi; cpu.lo=s.lo; }
    static std::string hex(uint32_t v) { char b[16]; std::snprintf(b,sizeof b,"%x",v); return b; }

    void linkImports(uint32_t base,uint32_t size) {
        for(uint32_t a=base;a+20<=base+size;a+=4) {
            uint32_t magic=read(a,4);
            if(magic==0x41c00000u) {                                   // export table: registered at RegisterLibraryEntries
                continue;
            }
            if(magic!=0x41e00000u) continue;
            std::string lib; for(int i=0;i<8;++i) { char c=char(read(a+12+i,1)); if(!c) break; lib+=c; }
            for(uint32_t s=a+20;read(s,4)==0x03e00008u && (read(s+4,4)>>16)==0x2400;s+=8)
                stubs_[s]={lib,int(read(s+4,4)&0xffff)};
        }
    }
    // Executes an import stub: LLE export if the library is loaded, else native code.
    bool dispatchImport() {
        auto it=stubs_.find(cpu.pc);
        if(it==stubs_.end()) return false;
        const auto& [lib,index]=it->second;
        if(auto l=libraries_.find(lib);l!=libraries_.end() && index<int(l->second.size()) && l->second[index]) {
            cpu.jump(l->second[index]); return true;
        }
        auto h=hle.find({lib,index});
        if(h==hle.end()) { stop("iop_import_missing "+lib+"::"+std::to_string(index)); return true; }
        uint32_t ra=cpu.r[31]; int caller=current_;
        h->second(*this);
        // A blocking call switched threads; the caller resumes at ra when woken.
        if(current_==caller && cpu.pc==it->first) cpu.jump(ra);
        else if(caller && threads_.count(caller)) threads_[caller].pc=ra;
        return true;
    }
    void save(Thread& t) { t.r=cpu.r; t.pc=cpu.pc; t.hi=cpu.hi; t.lo=cpu.lo; }
    void load(const Thread& t) { cpu.r=t.r; cpu.jump(t.pc); cpu.hi=t.hi; cpu.lo=t.lo; }
    bool schedule() {
        Thread* best=nullptr;
        for(auto& [id,t]:threads_) if(t.state==State::Ready && (!best || t.priority<best->priority || (t.priority==best->priority && t.order<best->order))) best=&t;
        if(!best) return false;
        current_=best->id; load(*best); return true;
    }
    // Blocks the current thread; pc stays at the stub so dispatchImport resumes at ra.
    void block(Wait w,int id) {
        auto& t=threads_.at(current_);
        save(t); t.state=State::Waiting; t.wait=w; t.wait_id=id; t.order=++order_; current_=0;
    }
    void ready(Thread& t,uint32_t result) { t.state=State::Ready; t.wait=Wait::None; t.r[2]=result; t.order=++order_; }
    // Preempt if a woken thread outranks the running one (called from non-interrupt services).
    // A thread readied in interrupt context (alarm, device tick, SIF command) with a higher
    // priority preempts the running one between instructions (never inside a delay slot);
    // the preempted thread keeps its place at the head of its priority's ready queue.
    void preempt() {
        if(!current_ || nested_ || cpu.next_pc!=cpu.pc+4) return;
        auto& cur=threads_.at(current_);
        for(auto& [id,t]:threads_) if(t.state==State::Ready && t.priority<cur.priority) {
            cur.state=State::Ready; save(cur); current_=0; return;
        }
    }
    void yieldTo() {
        if(!current_ || nested_) return;
        auto& cur=threads_.at(current_);
        for(auto& [id,t]:threads_) if(t.state==State::Ready && t.priority<cur.priority) {
            cur.state=State::Ready; cur.order=++order_; save(cur); cur.pc=cpu.r[31]; current_=0; return;
        }
    }
    // Fires due alarms in interrupt context; a non-zero return re-arms with that many ticks.
    void fireAlarms() {
        if(nested_ || alarms.empty()) return;
        auto now=std::chrono::steady_clock::now();
        for(size_t i=0;i<alarms.size() && stop_reason.empty();) {
            if(alarms[i].due>now) { ++i; continue; }
            auto a=alarms[i]; alarms.erase(alarms.begin()+long(i));
            uint32_t next=callGuest(a.handler,{a.arg});
            if(next) alarms.push_back({now+std::chrono::nanoseconds(uint64_t(next)*1000000/36864),a.handler,a.arg});
        }
    }
    void wakeDelayed() {
        fireAlarms();
        if(!nested_) for(auto& t:ticks) { t(*this); if(!stop_reason.empty()) return; }
        auto now=std::chrono::steady_clock::now();
        for(auto& [id,t]:threads_) if(t.state==State::Waiting && t.wait==Wait::Delay && now>=t.wake_at) ready(t,0);
    }
    void finishRpc() {
        uint32_t reply=cpu.r[2]; auto call=*active_call_; active_call_.reset();
        auto& t=threads_.at(current_); load(*rpc_saved_); t=*rpc_saved_; rpc_saved_.reset();
        save(t); t.state=State::Waiting; t.wait=Wait::RpcLoop; current_=0;
        sif.completeCall(call.ticket,reply);
        startRpc();
    }
    void startRpc() {
        if(active_call_ || calls_.empty()) return;
        auto call=calls_.front();
        auto& server=servers_.at(call.sd);
        auto q=queue_thread_.find(server.queue);
        if(q==queue_thread_.end()) { stop("iop_rpc_queue_without_thread"); return; }
        auto& t=threads_.at(q->second);
        if(t.state!=State::Waiting || t.wait!=Wait::RpcLoop) return; // serving thread busy; retried later
        calls_.pop_front(); active_call_=call; rpc_saved_=t;
        t.r[4]=call.fno; t.r[5]=call.buf; t.r[6]=call.size; t.r[31]=rpc_return; t.r[29]-=0x80; t.pc=server.func;
        ready(t,t.r[2]);
    }
    void installCore();
};

inline void IopKernel::installCore() {
    auto& k=*this;
    auto add=[&](const char* lib,int index,Hle fn){ hle[{lib,index}]=std::move(fn); };
    // sysmem
    add("sysmem",4,[](IopKernel& k){ k.ret(k.sif.allocateMode(int(k.arg(0)),k.arg(1),k.arg(2))); });   // AllocSysMemory(mode,size,addr)
    add("sysmem",5,[](IopKernel& k){ k.ret(uint32_t(k.sif.freeBlock(k.arg(0)))); });                    // FreeSysMemory
    add("sysmem",6,[](IopKernel& k){ k.ret(IopSif::ram_size); });                                        // QueryMemSize
    add("sysmem",7,[](IopKernel& k){ k.ret(k.sif.maxFree()); });                                         // QueryMaxFreeMemSize
    add("sysmem",8,[](IopKernel& k){ k.ret(k.sif.totalFree()); });                                       // QueryTotalFreeMemSize
    add("sysmem",9,[](IopKernel& k){ k.ret(k.sif.blockTop(k.arg(0))); });                                // QueryBlockTopAddress
    add("sysmem",10,[](IopKernel& k){ k.ret(k.sif.blockSize(k.arg(0))); });                              // QueryBlockSize
    add("sysmem",14,[](IopKernel& k){ k.ret(0); });
    // loadcore
    add("loadcore",6,[](IopKernel& k){
        uint32_t t=k.arg(0); if(k.read(t,4)!=0x41c00000u) { k.ret(-1u); return; }
        std::string lib; for(int i=0;i<8;++i) { char c=char(k.read(t+12+i,1)); if(!c) break; lib+=c; }
        auto& f=k.libraries_[lib]; f.clear();
        for(uint32_t a=t+20;k.read(a,4);a+=4) f.push_back(k.read(a,4));
        if(k.log) *k.log<<"IOP library "<<lib<<" exports="<<f.size()<<'\n';
        k.ret(0);
    });
    add("loadcore",7,[](IopKernel& k){ k.ret(0); });
    // modload 13 (IOPRP280 MODLOAD: stores the KELF path check callback MCMAN uses for mc0: modules).
    // MP2R loads modules only from the disc, so the callback is kept and never invoked.
    add("modload",13,[](IopKernel& k){ k.kelf_path_callback=k.arg(0); });
    // intrman: interrupts are delivered synchronously by MP2R, so the critical sections are no-ops.
    add("intrman",17,[](IopKernel& k){ if(k.arg(0)) k.write(k.arg(0),0,4); k.ret(0); });
    add("intrman",18,[](IopKernel& k){ k.ret(0); });
    // EnableIntr/DisableIntr(irq, &old): device interrupts reach handlers through the native
    // device models (libsd, cdvdman, SIO2), so only the enable state is kept.
    add("intrman",6,[](IopKernel& k){ k.irq_enabled[k.arg(0)&63]=true; k.ret(0); });
    add("intrman",7,[](IopKernel& k){
        uint32_t irq=k.arg(0)&63; if(k.arg(1)) k.write(k.arg(1),k.irq_enabled[irq]?irq:uint32_t(-1),4);
        k.irq_enabled[irq]=false; k.ret(0);
    });
    add("intrman",23,[](IopKernel& k){ k.ret(k.nested_?1:0); });
    // stdio / Kprintf
    add("stdio",4,[](IopKernel& k){                                    // printf: module diagnostics go to the log
        int next=1; auto text=k.formatString(k.arg(0),[&]{ return k.arg(next++); });
        if(k.log && ++k.printf_trace<=500) *k.log<<"IOP printf: "<<text<<(text.empty() || text.back()!='\n'?"\n":"");
        k.ret(uint32_t(text.size()));
    });
    // thbase
    add("thbase",4,[](IopKernel& k){
        uint32_t p=k.arg(0); Thread t; t.id=k.next_id_++;
        t.attr=k.read(p,4); t.option=k.read(p+4,4); t.entry=k.read(p+8,4); t.stack_size=k.read(p+12,4);
        t.priority=t.initial_priority=int(k.read(p+16,4)); t.stack=k.sif.allocate(t.stack_size); t.gp=k.cpu.r[28];
        k.threads_[t.id]=t; k.ret(uint32_t(t.id));
    });
    add("thbase",6,[](IopKernel& k){
        auto it=k.threads_.find(int(k.arg(0))); if(it==k.threads_.end()) { k.ret(uint32_t(-407)); return; }
        auto& t=it->second; t.r={}; t.r[4]=k.arg(1); t.r[28]=t.gp; t.r[29]=(t.stack+t.stack_size-0x10)&~15u; t.r[31]=thread_exit;
        t.pc=t.entry; t.priority=t.initial_priority; k.ready(t,0); k.ret(0); k.yieldTo();
    });
    add("thbase",8,[](IopKernel& k){ k.threads_.at(k.current_).state=State::Dormant; k.current_=0; });
    add("thbase",14,[](IopKernel& k){
        int id=int(k.arg(0)); if(!id) id=k.current_;
        auto it=k.threads_.find(id); if(it==k.threads_.end()) { k.ret(uint32_t(-407)); return; }
        it->second.priority=int(k.arg(1)); k.ret(0); k.yieldTo();
    });
    add("thbase",20,[](IopKernel& k){ k.ret(uint32_t(k.current_)); });
    add("thbase",24,[](IopKernel& k){
        auto& t=k.threads_.at(k.current_);
        if(t.wakeups) { --t.wakeups; k.ret(0); return; }
        k.ret(0); k.block(Wait::Sleep,0);
    });
    auto wake=[](IopKernel& k){
        auto it=k.threads_.find(int(k.arg(0))); if(it==k.threads_.end()) { k.ret(uint32_t(-407)); return; }
        auto& t=it->second;
        if(k.log && ++k.wake_trace<=60) *k.log<<"IOP Wakeup "<<it->first<<" from thread "<<k.current_<<" ra=0x"<<std::hex<<k.cpu.r[31]<<std::dec
                                              <<(t.state==State::Waiting && t.wait==Wait::Sleep?" (sleeping)":" (count)")<<'\n';
        if(t.state==State::Waiting && t.wait==Wait::Sleep) k.ready(t,0); else ++t.wakeups;
        k.ret(0);
    };
    add("thbase",25,[wake](IopKernel& k){ wake(k); k.yieldTo(); });
    add("thbase",26,wake);
    add("thbase",33,[](IopKernel& k){
        auto& t=k.threads_.at(k.current_);
        t.wake_at=std::chrono::steady_clock::now()+std::chrono::microseconds(k.arg(0)); k.ret(0); k.block(Wait::Delay,0);
    });
    auto now_clock=[]{ return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())*36864/1000000; };
    add("thbase",34,[now_clock](IopKernel& k){ uint64_t c=now_clock(); k.write(k.arg(0),uint32_t(c),4); k.write(k.arg(0)+4,uint32_t(c>>32),4); k.ret(0); });
    add("thbase",43,[now_clock](IopKernel& k){ k.ret(uint32_t(now_clock())); });
    add("thbase",39,[](IopKernel& k){ uint64_t c=uint64_t(k.arg(0))*36864/1000; k.write(k.arg(1),uint32_t(c),4); k.write(k.arg(1)+4,uint32_t(c>>32),4); k.ret(0); });
    add("thbase",40,[](IopKernel& k){
        uint64_t c=k.read(k.arg(0),4)|uint64_t(k.read(k.arg(0)+4,4))<<32,us=c*1000/36864;
        k.write(k.arg(1),uint32_t(us/1000000),4); k.write(k.arg(2),uint32_t(us%1000000),4); k.ret(0);
    });
    // thsemap
    add("thsemap",4,[](IopKernel& k){
        uint32_t p=k.arg(0); int id=k.next_id_++;
        // iop_sema_t { attr, option, initial, max }
        k.semas_[id]={int(k.read(p+8,4)),int(k.read(p+12,4)),k.read(p,4),k.read(p+4,4)}; k.ret(uint32_t(id));
    });
    add("thsemap",5,[](IopKernel& k){ k.semas_.erase(int(k.arg(0))); k.ret(0); });
    auto signal=[](IopKernel& k){
        int id=int(k.arg(0)); auto it=k.semas_.find(id); if(it==k.semas_.end()) { k.ret(uint32_t(-408)); return; }
        Thread* first=nullptr;
        for(auto& [tid,t]:k.threads_) if(t.state==State::Waiting && t.wait==Wait::Sema && t.wait_id==id && (!first || t.order<first->order)) first=&t;
        if(first) k.ready(*first,0); else if(it->second.count<it->second.max) ++it->second.count;
        k.ret(0);
    };
    add("thsemap",6,[signal](IopKernel& k){ signal(k); k.yieldTo(); });
    add("thsemap",7,signal);
    add("thsemap",8,[](IopKernel& k){
        auto it=k.semas_.find(int(k.arg(0))); if(it==k.semas_.end()) { k.ret(uint32_t(-408)); return; }
        if(it->second.count>0) { --it->second.count; k.ret(0); return; }
        k.ret(0); k.block(Wait::Sema,it->first);
    });
    add("thsemap",9,[](IopKernel& k){
        auto it=k.semas_.find(int(k.arg(0))); if(it==k.semas_.end()) { k.ret(uint32_t(-408)); return; }
        if(it->second.count>0) { --it->second.count; k.ret(0); } else k.ret(uint32_t(-419));
    });
    // thevent: WaitEventFlag modes AND=0/OR=1, clear bit 0x10, clear-pattern 0x20.
    add("thevent",4,[](IopKernel& k){ uint32_t p=k.arg(0); int id=k.next_id_++; k.events_[id]={k.read(p+8,4),k.read(p,4),k.read(p+4,4),k.read(p+8,4)}; k.ret(uint32_t(id)); });
    // ReferEventFlagStatus / iReferEventFlagStatus -> iop_event_info_t {attr, option, initBits, currBits, numThreads}
    auto refer_event=[](IopKernel& k){
        int id=int(k.arg(0)); auto it=k.events_.find(id); uint32_t info=k.arg(1);
        if(it==k.events_.end()) { k.ret(uint32_t(-409)); return; }
        uint32_t waiting=0;
        for(auto& [tid,t]:k.threads_) if(t.state==State::Waiting && t.wait==Wait::Event && t.wait_id==id) ++waiting;
        const auto& e=it->second;
        uint32_t v[5]={e.attr,e.option,e.initial,e.bits,waiting};
        for(int i=0;i<5;++i) k.write(info+i*4,v[i],4);
        k.ret(0);
    };
    add("thevent",13,refer_event); add("thevent",14,refer_event);
    add("thevent",5,[](IopKernel& k){ k.events_.erase(int(k.arg(0))); k.ret(0); });
    auto set_event=[](IopKernel& k){
        int id=int(k.arg(0)); auto it=k.events_.find(id); if(it==k.events_.end()) { k.ret(uint32_t(-409)); return; }
        auto& e=it->second; e.bits|=k.arg(1);
        for(auto& [tid,t]:k.threads_) {
            if(t.state!=State::Waiting || t.wait!=Wait::Event || t.wait_id!=id) continue;
            bool ok=(t.event_mode&1)?(e.bits&t.event_bits)!=0:(e.bits&t.event_bits)==t.event_bits;
            if(!ok) continue;
            if(t.event_result) k.write(t.event_result,e.bits,4);
            if(t.event_mode&0x10) e.bits=0; else if(t.event_mode&0x20) e.bits&=~t.event_bits;
            k.ready(t,0);
        }
        k.ret(0);
    };
    add("thevent",6,[set_event](IopKernel& k){ set_event(k); k.yieldTo(); });
    add("thevent",7,set_event);
    auto clear_event=[](IopKernel& k){ auto it=k.events_.find(int(k.arg(0))); if(it!=k.events_.end()) it->second.bits&=k.arg(1); k.ret(0); };
    add("thevent",8,clear_event); add("thevent",9,clear_event);
    auto wait_event=[](IopKernel& k,bool poll){
        int id=int(k.arg(0)); auto it=k.events_.find(id); if(it==k.events_.end()) { k.ret(uint32_t(-409)); return; }
        auto& e=it->second; uint32_t bits=k.arg(1),mode=k.arg(2),result=k.arg(3);
        bool ok=(mode&1)?(e.bits&bits)!=0:(e.bits&bits)==bits;
        if(ok) {
            if(result) k.write(result,e.bits,4);
            if(mode&0x10) e.bits=0; else if(mode&0x20) e.bits&=~bits;
            k.ret(0); return;
        }
        if(poll) { k.ret(uint32_t(-421)); return; }
        auto& t=k.threads_.at(k.current_); t.event_bits=bits; t.event_mode=mode; t.event_result=result;
        k.ret(0); k.block(Wait::Event,id);
    };
    add("thevent",10,[wait_event](IopKernel& k){ wait_event(k,false); });
    add("thevent",11,[wait_event](IopKernel& k){ wait_event(k,true); });
    // sysclib (string/memory helpers executed natively on IOP RAM)
    add("sysclib",11,[](IopKernel& k){ k.ret(uint32_t(std::memcmp(k.sif.ram(k.arg(0),k.arg(2)),k.sif.ram(k.arg(1),k.arg(2)),k.arg(2)))); });
    add("sysclib",12,[](IopKernel& k){ std::memmove(k.sif.ram(k.arg(0),k.arg(2)),k.sif.ram(k.arg(1),k.arg(2)),k.arg(2)); k.ret(k.arg(0)); });
    add("sysclib",14,[](IopKernel& k){ std::memset(k.sif.ram(k.arg(0),k.arg(2)),int(k.arg(1)&0xff),k.arg(2)); k.ret(k.arg(0)); });
    add("sysclib",27,[](IopKernel& k){ k.ret(uint32_t(k.sif.readString(k.arg(0),0x100000).size())); });
    add("sysclib",23,[](IopKernel& k){ auto s=k.sif.readString(k.arg(1),0x100000); std::memcpy(k.sif.ram(k.arg(0),s.size()+1),s.c_str(),s.size()+1); k.ret(k.arg(0)); });
    add("sysclib",22,[](IopKernel& k){ auto a=k.sif.readString(k.arg(0),0x100000),b=k.sif.readString(k.arg(1),0x100000); k.ret(uint32_t(a<b?-1:a>b?1:0)); });
    add("sysclib",13,[](IopKernel& k){ uint32_t n=k.arg(2); if(n) std::memmove(k.sif.ram(k.arg(0),n),k.sif.ram(k.arg(1),n),n); k.ret(k.arg(0)); });   // memmove
    add("sysclib",17,[](IopKernel& k){ uint32_t n=k.arg(1); if(n) std::memset(k.sif.ram(k.arg(0),n),0,n); k.ret(0); });                             // bzero
    add("sysclib",20,[](IopKernel& k){                                                                                                             // strcat
        auto a=k.sif.readString(k.arg(0),0x100000),b=k.sif.readString(k.arg(1),0x100000);
        std::memcpy(k.sif.ram(k.arg(0)+uint32_t(a.size()),b.size()+1),b.c_str(),b.size()+1); k.ret(k.arg(0));
    });
    auto find_char=[](IopKernel& k,bool last){
        auto s=k.sif.readString(k.arg(0),0x100000); char c=char(k.arg(1));
        size_t at=c==0?s.size():last?s.rfind(c):s.find(c);
        k.ret(at==std::string::npos?0:k.arg(0)+uint32_t(at));
    };
    add("sysclib",21,[find_char](IopKernel& k){ find_char(k,false); });                                                                          // strchr
    add("sysclib",32,[find_char](IopKernel& k){ find_char(k,true); });                                                                           // strrchr
    add("sysclib",29,[](IopKernel& k){                                                                                                             // strncmp
        uint32_t a=k.arg(0),b=k.arg(1),n=k.arg(2); int r=0;
        for(uint32_t i=0;i<n;++i) { uint8_t x=uint8_t(k.read(a+i,1)),y=uint8_t(k.read(b+i,1)); if(x!=y) { r=x<y?-1:1; break; } if(!x) break; }
        k.ret(uint32_t(r));
    });
    add("sysclib",30,[](IopKernel& k){                                                                                                             // strncpy
        uint32_t d=k.arg(0),s=k.arg(1),n=k.arg(2); bool end=false;
        for(uint32_t i=0;i<n;++i) { uint8_t c=end?0:uint8_t(k.read(s+i,1)); if(!c) end=true; k.write(d+i,c,1); }
        k.ret(d);
    });
    auto parse_long=[](IopKernel& k,bool is_signed){                                                                                        // strtol / strtoul
        auto s=k.sif.readString(k.arg(0),0x1000); char* end=nullptr;
        uint32_t v=is_signed?uint32_t(int32_t(std::strtol(s.c_str(),&end,int(k.arg(2))))):uint32_t(std::strtoul(s.c_str(),&end,int(k.arg(2))));
        if(k.arg(1)) k.write(k.arg(1),k.arg(0)+uint32_t(end-s.c_str()),4);
        k.ret(v);
    };
    add("sysclib",36,[parse_long](IopKernel& k){ parse_long(k,true); });
    add("sysclib",38,[parse_long](IopKernel& k){ parse_long(k,false); });
    add("sysclib",7,[](IopKernel& k){ uint32_t c=k.arg(0)&0xff; k.ret(c>='A' && c<='Z'?c+32:c); });                                              // tolower
    add("sysclib",6,[](IopKernel& k){ uint32_t c=k.arg(0)&0xff; k.ret(c>='a' && c<='z'?c-32:c); });                                              // toupper
    add("sysclib",28,[](IopKernel& k){                                                                                                             // strncat
        auto a=k.sif.readString(k.arg(0),0x100000); uint32_t d=k.arg(0)+uint32_t(a.size()),s=k.arg(1),n=k.arg(2),i=0;
        for(;i<n;++i) { uint8_t c=uint8_t(k.read(s+i,1)); if(!c) break; k.write(d+i,c,1); }
        k.write(d+i,0,1); k.ret(k.arg(0));
    });
    add("sysclib",35,[](IopKernel& k){                                                                                                             // strtok
        uint32_t s=k.arg(0)?k.arg(0):k.strtok_next; auto delim=k.sif.readString(k.arg(1),0x1000);
        if(!s) { k.ret(0); return; }
        auto is_delim=[&](uint8_t c){ return c && delim.find(char(c))!=std::string::npos; };
        while(is_delim(uint8_t(k.read(s,1)))) ++s;
        if(!k.read(s,1)) { k.strtok_next=0; k.ret(0); return; }
        uint32_t e=s; while(k.read(e,1) && !is_delim(uint8_t(k.read(e,1)))) ++e;
        if(k.read(e,1)) { k.write(e,0,1); k.strtok_next=e+1; } else k.strtok_next=0;
        k.ret(s);
    });
    add("sysclib",19,[](IopKernel& k){ int next=2; k.ret(k.format(k.arg(0),k.arg(1),[&]{ return k.arg(next++); })); });                       // sprintf
    add("sysclib",42,[](IopKernel& k){ uint32_t ap=k.arg(2); k.ret(k.format(k.arg(0),k.arg(1),[&]{ uint32_t v=k.read(ap,4); ap+=4; return v; })); });   // vsprintf
    add("sysclib",34,[](IopKernel& k){                                                                                                             // strstr
        auto h=k.sif.readString(k.arg(0),0x100000),n=k.sif.readString(k.arg(1),0x100000);
        size_t at=h.find(n); k.ret(at==std::string::npos?0:k.arg(0)+uint32_t(at));
    });
    // sifman: IOP->EE DMA copies directly into EE RAM.
    auto set_dma=[](IopKernel& k){
        uint32_t list=k.arg(0),count=k.arg(1);
        for(uint32_t i=0;i<count;++i) {
            uint32_t src=k.read(list+i*16,4),dest=k.read(list+i*16+4,4),size=(k.read(list+i*16+8,4)+3)&~3u;
            std::memcpy(k.ee_memory(dest&0x1ffffff),k.sif.ram(src,size),size);
        }
        k.ret(1);
    };
    add("sifman",7,set_dma); add("sifman",32,set_dma);
    add("sifman",5,[](IopKernel& k){ k.ret(0); });
    add("sifman",8,[](IopKernel& k){ k.ret(uint32_t(-1)); });
    add("sifman",29,[](IopKernel& k){ k.ret(1); });
    // sifcmd: the module-side SIF library bridges into IopSif.
    add("sifcmd",4,[](IopKernel& k){ k.ret(0); });
    add("sifcmd",14,[](IopKernel& k){ k.ret(0); });
    add("sifcmd",8,[](IopKernel& k){ k.ret(0); });
    add("sifcmd",10,[](IopKernel& k){
        uint32_t cid=k.arg(0); k.cmd_handlers_[cid]={k.arg(1),k.arg(2)};
        k.sif.user_command=[&k](uint32_t c,uint32_t packet){
            auto it=k.cmd_handlers_.find(c);
            if(it==k.cmd_handlers_.end()) return false;
            if(k.log && ++k.user_cmd_trace<=400) *k.log<<"IOP user cmd cid=0x"<<std::hex<<c<<" handler=0x"<<it->second.first<<" words="<<k.read(packet+16,4)<<","<<k.read(packet+20,4)<<","<<k.read(packet+24,4)<<","<<k.read(packet+28,4)<<std::dec<<'\n';
            k.callGuest(it->second.first,{packet,it->second.second});
            return true;
        };
        k.ret(0);
    });
    add("sifcmd",11,[](IopKernel& k){ k.cmd_handlers_.erase(k.arg(0)); k.ret(0); });
    auto send_cmd=[](IopKernel& k){
        uint32_t cid=k.arg(0),packet=k.arg(1),psize=k.arg(2),src=k.arg(3),dest=k.arg(4),size=k.arg(5);
        if(k.log && ++k.send_cmd_trace<=400) *k.log<<"IOP SendCmd cid=0x"<<std::hex<<cid<<" words="<<k.read(packet+16,4)<<","<<k.read(packet+20,4)<<","<<k.read(packet+24,4)<<","<<k.read(packet+28,4)<<std::dec<<" size="<<size<<'\n';
        k.sif.sendFromIop(cid,packet,psize,dest,src,size);
        k.ret(1);
    };
    add("sifcmd",12,send_cmd); add("sifcmd",13,send_cmd);
    add("sifcmd",19,[](IopKernel& k){ k.queue_thread_[k.arg(0)]=int(k.arg(1)); k.ret(0); });
    add("sifcmd",17,[](IopKernel& k){
        uint32_t sd=k.arg(0),sid=k.arg(1),func=k.arg(2),buf=k.arg(3),queue=k.arg(6);
        k.servers_[sd]={sd,sid,func,buf,queue};
        k.sif.addLleServer(sid,sd,buf,[&k,sd](uint32_t fno,uint32_t b,uint32_t size,uint64_t ticket){
            k.calls_.push_back({sd,fno,b,size,ticket}); k.startRpc();
        },k.loaded.empty()?"":k.loaded.back());
        if(k.log) *k.log<<"IOP RPC server sid=0x"<<std::hex<<sid<<" func=0x"<<func<<std::dec<<'\n';
        k.ret(0);
    });
    add("sifcmd",22,[](IopKernel& k){ k.ret(0); k.block(Wait::RpcLoop,int(k.arg(0))); k.startRpc(); });
    // ioman AddDrv(20)/DelDrv(21): device drivers registered by modules (iop_device_t {name, type, version, desc, ops}).
    add("ioman",20,[](IopKernel& k){
        uint32_t device=k.arg(0); auto name=k.sif.readString(k.read(device,4),32);
        k.devices[name]=device;
        if(k.log) *k.log<<"IOP AddDrv "<<name<<" ops=0x"<<std::hex<<k.read(device+16,4)<<std::dec<<'\n';
        k.ret(0);
    });
    add("ioman",21,[](IopKernel& k){ k.ret(k.devices.erase(k.sif.readString(k.arg(0),32))?0:uint32_t(-1)); });
    // Alarms: SetAlarm(35)/iSetAlarm(36)(iop_sys_clock_t* delay, handler(arg) -> next delay or 0, arg),
    // CancelAlarm(37)/iCancelAlarm(38)(handler, arg). Clock: 36.864 MHz system ticks.
    auto set_alarm=[](IopKernel& k){
        uint64_t ticks=k.read(k.arg(0),4)|uint64_t(k.read(k.arg(0)+4,4))<<32;
        k.alarms.push_back({std::chrono::steady_clock::now()+std::chrono::nanoseconds(ticks*1000000/36864),k.arg(1),k.arg(2)});
        k.ret(0);
    };
    add("thbase",35,set_alarm); add("thbase",36,set_alarm);
    auto cancel_alarm=[](IopKernel& k){
        auto before=k.alarms.size();
        k.alarms.erase(std::remove_if(k.alarms.begin(),k.alarms.end(),[&](const Alarm& a){ return a.handler==k.arg(0) && a.arg==k.arg(1); }),k.alarms.end());
        k.ret(k.alarms.size()<before?0:uint32_t(-105));   // KE_NOEXIST_ALARM
    };
    add("thbase",37,cancel_alarm); add("thbase",38,cancel_alarm);
    // vblank: WaitVblankStart(4), WaitVblankEnd(5), WaitVblank(6: in blank or next start), WaitNonVblank(7).
    add("vblank",4,[](IopKernel& k){ k.ret(0); k.block(Wait::Vblank,0); });
    add("vblank",5,[](IopKernel& k){ k.ret(0); k.block(Wait::Vblank,1); });
    add("vblank",6,[](IopKernel& k){ k.ret(0); if(!k.in_vblank) k.block(Wait::Vblank,0); });
    add("vblank",7,[](IopKernel& k){ k.ret(0); if(k.in_vblank) k.block(Wait::Vblank,1); });
    // cdvdman: sector reads complete synchronously from the disc image (PS2 DVD media).
    add("cdvdman",6,[](IopKernel& k){
        if(!k.disc) { k.stop("iop_cd_without_disc"); return; }
        uint32_t lbn=k.arg(0),count=k.arg(1),buf=k.arg(2);
        if(k.log && ++k.cd_trace<=40) *k.log<<"IOP sceCdRead lbn="<<lbn<<" count="<<count<<" buf=0x"<<std::hex<<buf<<" mode=0x"<<k.read(k.arg(3),4)<<std::dec<<" flag="<<k.cd_stream_flag<<'\n';
        for(uint32_t i=0;i<count;++i) k.disc->read(lbn+i,k.sif.ram(buf+i*2048,2048));
        k.cd_reads+=count; k.ret(1);
        k.cdCommandComplete();
    });
    add("cdvdman",48,[](IopKernel& k){ k.cd_stm_callback[0]=k.arg(0); k.ret(0); });   // sceCdstm0Cb
    add("cdvdman",49,[](IopKernel& k){ k.cd_stm_callback[1]=k.arg(0); k.ret(0); });   // sceCdstm1Cb
    add("cdvdman",39,[](IopKernel& k){ k.ret(1); });                                  // sceCdBreak: nothing pending
    add("cdvdman",46,[](IopKernel& k){ k.ret(1); k.cdCommandComplete(); });          // sceCdNop: drive NOP command
    add("cdvdman",61,[](IopKernel& k){ k.ret(1); });                                  // sceCdStStop
    // Export 114 of the IOPRP280 cdvdman 2.35 is a sector read with sceCdRead's signature (used by CDVDSTM).
    hle[{"cdvdman",114}]=hle[{"cdvdman",6}];
    add("cdvdman",8,[](IopKernel& k){ k.ret(0); });      // sceCdGetError: SCECdErNO
    // secrman (BIOS ROM module, not in IOPRP280): MCMAN registers its SIO2 command handlers here.
    // MagicGate card authentication is not emulated, so SecrAuthCard reports failure.
    add("secrman",4,[](IopKernel& k){ k.mc_command_handler=k.arg(0); k.ret(0); });   // SecrSetMcCommandHandler
    add("secrman",5,[](IopKernel& k){ k.mc_devid_handler=k.arg(0); k.ret(0); });     // SecrSetMcDevIDHandler
    add("secrman",6,[](IopKernel& k){ k.ret(0); });                                  // SecrAuthCard: not authenticated
    add("secrman",7,[](IopKernel& k){ k.ret(0); });                                  // SecrResetAuthCard
    add("cdvdman",11,[](IopKernel& k){ k.ret(0); });     // sceCdSync: no command in progress
    add("cdvdman",12,[](IopKernel& k){ k.ret(0x14); });  // sceCdGetDiskType: SCECdPS2DVD
    add("cdvdman",13,[](IopKernel& k){ k.ret(2); });     // sceCdDiskReady: SCECdComplete
    add("cdvdman",50,[](IopKernel& k){                    // sceCdSC (system control), cdvdman 2.35 semantics
        int32_t code=int32_t(k.arg(0)); uint32_t param=k.arg(1);
        if(k.log && ++k.cd_sc_trace<=10) *k.log<<"IOP sceCdSC "<<code<<'\n';
        if(code>=-1 && code<=2) {                          // stream flag; *param = last error
            if(param) k.write(param,0,4);
            if(code!=-1) k.cd_stream_flag=code;
            k.ret(uint32_t(k.cd_stream_flag)); return;
        }
        if(code==-9) { k.ret(0x0223); return; }           // 0xFFFFFFF7: cdvdman version
        if(code==-11) {                                    // 0xFFFFFFF5: cdvdman interrupt event flag
            // cdvdman leaves it at 0x29 once idle; MP2R reads complete synchronously, so it stays idle.
            if(!k.cd_intr_event) { k.cd_intr_event=k.next_id_++; k.events_[k.cd_intr_event]={0x29,2,0,0}; }
            k.ret(uint32_t(k.cd_intr_event)); return;
        }
        if(code==-8) { k.ret(1); return; }                // 0xFFFFFFF8: spin control
        // 0xFFFFFFFE: cdvdman's read-request word, cleared at init and when a read completes;
        // IOP-side reads here complete within the call, so none is outstanding.
        if(code==-2) { k.ret(0); return; }
        if(code==-17) { k.ret(5000); return; }            // 0xFFFFFFEF: stream timeout (cdvdman default, ms)
        if(code==-15) { k.ret(15000); return; }           // 0xFFFFFFF1: sync timeout (cdvdman default, ms)
        if(code==-18) { k.ret(0); return; }               // 0xFFFFFFEE: last-read timeout set (reads never time out here)
        k.stop("iop_cdsc_unsupported "+std::to_string(code));
    });
    (void)k;
}
}
