#pragma once
// Native EE platform services shared by the selected-function probe and the
// whole-program build. Generated translation units see it through PS2Runtime.
#include "mp2_guest_runtime.h"
#include "bootstrap_runtime.h"
#include "ee_semaphores.h"
#include "ee_timer3.h"
#include "ee_intc.h"
#include "ee_threads.h"
#include "ee_osd_config.h"
#include "ee_tlb.h"
#include "ee_special_memory.h"
#include "ee_vblank.h"
#include "iop_modules.h"
#include "iop_kernel.h"
#include "iop_libsd.h"
#include "disc_image.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include "kernel_syscall_table.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <unordered_map>
#include <vector>
class MP2InitializationRuntime : public MP2GuestRuntime, public mp2r::BootstrapRuntime {
public:
    using mp2r::BootstrapRuntime::GuestBranchKind;
    using mp2r::BootstrapRuntime::isSpecialAddress;
    struct GuestTransfer {};
    struct MemoryStop {};
    struct SyscallFrame { uint32_t resume,saved_ra; };
    std::vector<SyscallFrame> syscall_frames;
    mp2r::KernelSyscallTable syscall_table;
    using Fn=MP2GuestFunction;
    std::unordered_map<uint32_t,Fn> entries;
    mp2r::EeSemaphores semaphores;
    mp2r::EeTimer3Registers timer3;
    mp2r::EeTimerRegisters timer2{0x10001000};
    mp2r::EeIntc intc;
    mp2r::EeThreads threads;
    mp2r::EeOsdConfig osd;
    unsigned osd_reads{},osd_writes{};
    mp2r::EeTlb tlb;
    unsigned tlb_writes{};
    static mp2r::EeTlbRegisters tlbRegisters(const R5900Context* c) {
        return {c->cop0_index,c->cop0_random,c->cop0_wired,c->cop0_pagemask,c->cop0_entryhi,c->cop0_entrylo0,c->cop0_entrylo1};
    }
    static void setTlbRegisters(R5900Context* c,const mp2r::EeTlbRegisters& r) {
        c->cop0_index=r.index; c->cop0_random=r.random; c->cop0_wired=r.wired; c->cop0_pagemask=r.pagemask;
        c->cop0_entryhi=r.entryhi; c->cop0_entrylo0=r.entrylo0; c->cop0_entrylo1=r.entrylo1;
    }
    void stopTlb(R5900Context* cpu) { reason="tlb_index_out_of_range"; blocked_source=cpu->pc; throw MemoryStop{}; }
    void logTlbWrite(const mp2r::EeTlbRegisters& r,unsigned slot) {
        if(++tlb_writes<=64) log<<"TLB write slot="<<slot<<" pagemask=0x"<<std::hex<<r.pagemask<<" entryhi=0x"<<r.entryhi
            <<" lo0=0x"<<r.entrylo0<<" lo1=0x"<<r.entrylo1<<std::dec<<" wired="<<r.wired<<'\n';
    }
    void handleTLBWI(uint8_t*,R5900Context* cpu) {
        auto r=tlbRegisters(cpu);
        if(!tlb.writeIndexed(r)) stopTlb(cpu);
        logTlbWrite(r,r.index&0x3f);
    }
    void handleTLBWR(uint8_t*,R5900Context* cpu) {
        auto r=tlbRegisters(cpu); auto slot=r.random;
        if(!tlb.writeRandom(r)) stopTlb(cpu);
        if(slot<r.wired || slot>=mp2r::EeTlb::size) slot=mp2r::EeTlb::size-1;
        setTlbRegisters(cpu,r); logTlbWrite(r,slot);
    }
    void handleTLBR(uint8_t*,R5900Context* cpu) {
        auto r=tlbRegisters(cpu);
        if(!tlb.read(r)) stopTlb(cpu);
        setTlbRegisters(cpu,r);
    }
    void handleTLBP(uint8_t*,R5900Context* cpu) {
        auto r=tlbRegisters(cpu); tlb.probe(r); setTlbRegisters(cpu,r);
    }
    struct ThreadContext { R5900Context cpu; std::vector<SyscallFrame> syscall_frames; };
    std::unordered_map<int,ThreadContext> thread_contexts;
    unsigned context_switches{},semaphore_waits{};
    // With every thread waiting the kernel runs its idle loop until an interrupt readies one.
    bool idle{};
    uint64_t idle_entries{};
    std::chrono::steady_clock::time_point idle_since{};
    static constexpr uint32_t idle_pc=0x00000900u;
    void reschedule(R5900Context* cpu) {
        int previous=threads.current(),next=threads.schedule();
        if(!next) {
            if(idle) return;
            cpu->llbit=0;
            thread_contexts[previous]={*cpu,std::move(syscall_frames)}; syscall_frames.clear();
            auto global_tlb=tlbRegisters(cpu);
            *cpu=R5900Context{}; setTlbRegisters(cpu,global_tlb);
            cpu->pc=idle_pc; cpu->cop0_status=0x10001u;
            idle=true; ++idle_entries;
            throw GuestTransfer{};
        }
        if(!idle && previous==next) return;
        cpu->llbit=0;
        if(!idle) thread_contexts[previous]={*cpu,std::move(syscall_frames)};
        idle=false;
        auto& selected=thread_contexts.at(next);
        auto global_tlb=tlbRegisters(cpu); // COP0 TLB state belongs to the CPU, not to a thread
        *cpu=selected.cpu; syscall_frames=std::move(selected.syscall_frames);
        setTlbRegisters(cpu,global_tlb);
        cpu->llbit=0;
        ++context_switches;
        if(context_switches<=64) log<<"thread switch "<<previous<<" -> "<<next<<" pc=0x"<<std::hex<<cpu->pc<<std::dec<<'\n';
        throw GuestTransfer{}; // leave the generated caller before changing guest stacks
    }
    bool kernel_copy_verified{};
    std::chrono::steady_clock::time_point last_clock=std::chrono::steady_clock::now();
    uint64_t clock_fraction{};
    void clockTimers() {
        auto now=std::chrono::steady_clock::now();
        auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(now-last_clock).count();
        last_clock=now;
        if(ns<0 || ns>300000000000LL) throw std::runtime_error("Invalid native clock interval");
        uint64_t scaled=uint64_t(ns%1000000000LL)*147456000u+clock_fraction;
        auto cycles=uint64_t(ns/1000000000LL)*147456000u+scaled/1000000000u;
        clock_fraction=scaled%1000000000u;
        if(timer2.advanceBusCycles(cycles)) intc.raise(11);
        if(timer3.advanceBusCycles(cycles)) intc.raise(12);
        auto blank=vblank.advance(uint64_t(ns));
        if(blank&mp2r::EeVblank::Start) { intc.raise(2); gs_csr|=8u; if(guest_ram) signalVsync(guest_ram); }
        if(blank&mp2r::EeVblank::End) intc.raise(3);
    }
    // ---- Interrupt delivery (INT0 = INTC, INT1 = DMAC), modelled on the EE kernel dispatcher:
    // causes are acknowledged first, then each registered handler runs with a0=cause/channel,
    // a1=arg and the gp saved at registration; a negative v0 ends that cause's chain.
    mp2r::EeVblank vblank;
    uint32_t gs_csr{};
    std::array<uint32_t,3> gs_crt{};
    uint64_t gs_imr=0xff00;
    uint32_t vsync_flag{},vsync_csr{}; // SetVSyncFlag targets, written at VBLANK start
    uint8_t* guest_ram{};
    void signalVsync(uint8_t* ram) {
        if(!vsync_flag || !vsync_csr) return;
        uint32_t one=1; uint64_t csr=gsRead(0x12001000u);
        if(vsync_flag<=PS2_RAM_SIZE-4) std::memcpy(ram+vsync_flag,&one,4);
        if(vsync_csr<=PS2_RAM_SIZE-8) std::memcpy(ram+vsync_csr,&csr,8);
    }
    mp2r::EeIntc dmac_handlers;
    uint32_t dmac_stat{}; // D_STAT: channel status bits 0-9, channel mask bits 16-25
    struct HandlerCall { uint32_t entry,a0,a1,gp; };
    struct InterruptFrame { R5900Context cpu; std::vector<SyscallFrame> syscall_frames; std::vector<HandlerCall> calls; std::size_t next{}; };
    std::optional<InterruptFrame> interrupt;
    uint64_t interrupts_delivered{};
    static constexpr uint32_t interrupt_return=0x00000800u,interrupt_stack_top=0x00074000u;
    uint32_t dmacPending() const { return dmac_stat&(dmac_stat>>16)&0x3ffu; }
    bool interruptPending() const { return !interrupt && (intc.pending() || dmacPending()); }
    bool interruptDeliverable(const R5900Context* cpu) const {
        return interruptPending() && (cpu->cop0_status&0x10001u)==0x10001u && !(cpu->cop0_status&6u);
    }
    void collectHandlers(std::vector<HandlerCall>& calls,const mp2r::EeIntc& chains,unsigned cause) {
        for(int id=chains.head(int(cause));id;id=chains.get(id)->next) {
            const auto* h=chains.get(id);
            calls.push_back({h->entry,cause,h->argument,h->gp});
        }
    }
    // Called by the executor between steps. Returns true when guest handler code was entered.
    bool beginInterrupt(R5900Context* cpu) {
        if(!interruptDeliverable(cpu)) return false;
        std::vector<HandlerCall> calls;
        if(auto pending=intc.pending()) {
            for(unsigned cause=0;cause<15;++cause) {
                if(!(pending&(1u<<cause))) continue;
                intc.acknowledge(1u<<cause);
                if(cause==12 && syscall_table.timer3InterruptHandler()) {
                    reason="timer3_alarm_dispatch_required"; blocked_source=cpu->pc; throw MemoryStop{};
                }
                collectHandlers(calls,intc,cause);
            }
        } else {
            auto channels=dmacPending();
            for(unsigned channel=0;channel<10;++channel) {
                if(!(channels&(1u<<channel))) continue;
                dmac_stat&=~(1u<<channel);
                collectHandlers(calls,dmac_handlers,channel);
            }
        }
        if(calls.empty()) return false;
        interrupt=InterruptFrame{*cpu,std::move(syscall_frames),std::move(calls),0};
        syscall_frames.clear();
        ++interrupts_delivered;
        enterHandler(cpu);
        return true;
    }
    void enterHandler(R5900Context* cpu) {
        const auto& call=interrupt->calls[interrupt->next];
        cpu->cop0_status&=~1u; // IE off while handlers run
        cpu->pc=call.entry; cpu->llbit=0;
        SET_GPR_U32(cpu,4,call.a0); SET_GPR_U32(cpu,5,call.a1); SET_GPR_U32(cpu,28,call.gp);
        SET_GPR_U32(cpu,29,interrupt_stack_top-0x20u); SET_GPR_U32(cpu,31,interrupt_return);
    }
    // Executor reached interrupt_return: run the next handler or restore the interrupted context.
    void finishHandler(R5900Context* cpu) {
        if(!interrupt) throw std::runtime_error("Interrupt return without interrupt frame");
        auto& frame=*interrupt;
        auto finished=frame.calls[frame.next++];
        if(GPR_S32(cpu,2)<0)
            while(frame.next<frame.calls.size() && frame.calls[frame.next].a0==finished.a0) ++frame.next;
        if(frame.next<frame.calls.size()) { enterHandler(cpu); return; }
        auto global_tlb=tlbRegisters(cpu);
        *cpu=frame.cpu; syscall_frames=std::move(frame.syscall_frames);
        setTlbRegisters(cpu,global_tlb);
        cpu->llbit=0;
        interrupt.reset();
        // The kernel's interrupt epilogue reschedules if a handler readied a better thread.
        try { reschedule(cpu); } catch(const GuestTransfer&) {}
    }
    void clearLLBit(R5900Context* cpu) { cpu->llbit=0; }
    void verifyKernelCopy(const uint8_t* ram) {
        // The tail contains mutable alarm state; only the first check covers it.
        auto bytes=kernel_copy_verified?0x6e8u:0x740u;
        if(std::memcmp(ram+0x76000,ram+0x54d6a0,bytes) ||
           std::memcmp(ram+0x82000,ram+0x54dde0,40))
            throw std::runtime_error("Original alarm kernel copy mismatch");
        if(!kernel_copy_verified) log<<"original alarm kernel and vector copies verified\n";
        kernel_copy_verified=true;
    }
    bool tlb_copy_verified{};
    void verifyTlbCopy(const uint8_t* ram) {
        // Code plus the constant service table copied by the original InitTLBFunctions path.
        if(std::memcmp(ram+0x75000,ram+0x54c868,0x330)) throw std::runtime_error("Original TLB service copy mismatch");
        if(!tlb_copy_verified) log<<"original TLB service copy verified\n";
        tlb_copy_verified=true;
    }
    std::string boot_path;
    // SetupThread copies LoadExecPS2 arguments into the caller's {argc, argv[16], payload[256]}.
    // A disc boot passes the SYSTEM.CNF BOOT2 path as the only argument.
    void writeBootArguments(uint8_t* ram) {
        auto args=thread.args;
        if(!args) return;
        if((args&3) || args>PS2_RAM_SIZE-0x144 || boot_path.empty() || boot_path.size()>=256 ||
           boot_path.find('\0')!=std::string::npos)
            throw std::runtime_error("Unsupported boot argument area");
        uint32_t argc=1,argv0=args+0x44;
        std::memcpy(ram+args,&argc,4); std::memcpy(ram+args+4,&argv0,4);
        std::memcpy(ram+argv0,boot_path.c_str(),boot_path.size()+1);
        log<<"SetupThread args=0x"<<std::hex<<args<<std::dec<<" argc=1 argv0="<<boot_path<<'\n';
    }
    unsigned dispatches{};
    uint64_t executions{};
    std::array<std::pair<uint32_t,uint32_t>,64> recent_branches{};
    uint32_t source{};
    uint32_t memory_address{};
    std::string reason;
    std::ostream& log;
    unsigned checkpoint_limit;
    explicit MP2InitializationRuntime(std::ostream& output,uint32_t table_base,unsigned limit)
        :syscall_table(table_base),log(output),checkpoint_limit(limit) {
        iop.log=&log; iop_kernel.log=&log; libsd.install(iop_kernel);
        iop_kernel.ee_memory=[this](uint32_t address){ return guest_ram+(address&0x1ffffff); };
        auto loader=[this](mp2r::IopSif&,const std::string& path,const std::string& name,const std::string& args){ return loadIopModule(path,name,args); };
        iop.on_reboot=[this,loader](mp2r::IopSif& sif){ iop_kernel.reset(); mp2r::registerResidentIopServices(sif,loader); };
        mp2r::registerResidentIopServices(iop,loader);
    }
    mp2r::IopSif iop;
    mp2r::IopKernel iop_kernel{iop};
    mp2r::IopLibsd libsd;
    std::optional<mp2r::DiscImage> disc;
    std::filesystem::path disc_root;
    // Hardware drivers MP2R provides as native libraries instead of running their IRX.
    static bool nativeIopModule(const std::string& name) { return name=="LIBSD.IRX" || name=="SIO2MAN.IRX"; }
    int loadIopModule(const std::string& path,const std::string& name,const std::string& args) {
        if(nativeIopModule(name)) return int(iop.modules.size()+1);
        if(disc_root.empty()) { iop.stop_reason="iop_module_without_disc "+name; return -1; }
        // "cdrom0:\DIR\FILE;1" -> <disc root>/DIR/FILE
        auto relative=path.substr(path.find(':')+1);
        if(auto semi=relative.find(';');semi!=std::string::npos) relative.resize(semi);
        std::replace(relative.begin(),relative.end(),'\\','/');
        while(!relative.empty() && relative.front()=='/') relative.erase(0,1);
        std::ifstream file(disc_root/relative,std::ios::binary);
        if(!file) { log<<"IOP module not found on disc: "<<path<<'\n'; return -203; }      // KE_NOFILE
        std::vector<uint8_t> irx((std::istreambuf_iterator<char>(file)),{});
        if(!iop_kernel.loadModule(irx,name,args)) { iop.stop_reason=iop_kernel.stop_reason.empty()?"iop_module_no_resident "+name:iop_kernel.stop_reason; return -1; }
        return int(iop.modules.size()+1);
    }
    // Lets IOP threads run; replies they queue for the EE are pumped through SIF0.
    void runIop(uint64_t budget) {
        iop_kernel.run(budget);
        if(!iop_kernel.stop_reason.empty() && iop.stop_reason.empty()) iop.stop_reason=iop_kernel.stop_reason;
        iop.pumpPending(dmac_stat);
    }
    void checkIop(R5900Context* cpu) {
        if(iop.stop_reason.empty()) return;
        reason=iop.stop_reason; blocked_source=cpu->pc; throw MemoryStop{};
    }
    bool eeCheckpointDue(uint32_t=32) {
        clockTimers();
        ++checkpoints;
        // Yield to the executor so pending interrupts are delivered even inside tight guest loops.
        return checkpoints>=checkpoint_limit || checkpoints%4096==0 || interruptPending();
    }
    void registerGuestFunctions() {
        entries.reserve(mp2rGuestEntryCount);
        for(std::size_t i=0;i<mp2rGuestEntryCount;++i) bind(mp2rGuestEntries[i].pc,mp2rGuestEntries[i].fn);
    }
    void bind(uint32_t pc,Fn fn) {
        if(!entries.emplace(pc,fn).second) throw std::runtime_error("Duplicate original-code entry");
    }
    void stopMissing(R5900Context* cpu,uint32_t target,uint32_t from) {
        blocked=true; blocked_target=target; blocked_source=from; cpu->pc=target;
        reason="missing_function";
    }
    bool dispatchGuestBranch(uint8_t*,R5900Context* cpu,uint32_t target,uint32_t from,
                             uint32_t,GuestBranchKind,const char*) {
        recent_branches[dispatches%recent_branches.size()]={from,target};
        ++dispatches; source=from;
        if(dispatches<=64) log<<"branch 0x"<<std::hex<<from<<" -> 0x"<<target<<std::dec<<'\n';
        cpu->pc=target;
        if(!entries.contains(target)) stopMissing(cpu,target,from);
        return false; // unwind host code; next executor step enters the guest target
    }
    static uint32_t readWord(const uint8_t* ram,uint32_t address) {
        if(address>PS2_RAM_SIZE-4 || (address&3)) throw std::runtime_error("Invalid guest descriptor address");
        uint32_t value; std::memcpy(&value,ram+address,4); return value;
    }
    void handleSyscall(uint8_t* ram,R5900Context* cpu,uint32_t encoded) {
        auto number=encoded?encoded:GPR_U32(cpu,3);
        // libkernel encodes interrupt-context entry points as negative numbers.
        if(int32_t(number)<0) number=uint32_t(-int32_t(number));
        // iEnableIntc/iDisableIntc/iEnableDmac/iDisableDmac share the non-interrupt behaviour.
        if(number>=0x1a && number<=0x1d) number-=6;
        if(number==0x3c || number==0x3d) {
            mp2r::BootstrapRuntime::handleSyscall(ram,cpu,encoded);
            if(number==0x3c) { threads.bindMain({0,0x100008,thread.stack,thread.size,thread.gp,0,0}); writeBootArguments(ram); }
            return;
        }
        if(syscall_count<syscalls.size()) syscalls[syscall_count]=number;
        ++syscall_count;
        if(number==0x4a || number==0x4b) {
            auto p=GPR_U32(cpu,4); std::span<uint8_t> memory(ram,PS2_RAM_SIZE);
            bool ok=number==0x4a?osd.set(memory,p):osd.get(memory,p);
            if(ok) { if(number==0x4a) ++osd_writes; else ++osd_reads; }
            SET_GPR_S32(cpu,2,ok?0:-1);
            log<<(number==0x4a?"SetOsdConfigParam":"GetOsdConfigParam")<<" status="<<(ok?0:-1)
               <<" raw=0x"<<std::hex<<osd.raw()<<std::dec<<'\n';
            return;
        }
        if(number==0x6e || number==0x6f) {
            bool ok=osd.transferExtended({ram,PS2_RAM_SIZE},GPR_U32(cpu,4),GPR_U32(cpu,5),GPR_U32(cpu,6),number==0x6e);
            SET_GPR_S32(cpu,2,ok?0:-1); return;
        }
        if(number==0x20) {
            uint32_t p=GPR_U32(cpu,4); int id=-1;
            if(p && !(p&3) && p<=PS2_RAM_SIZE-28)
                id=threads.create({readWord(ram,p),readWord(ram,p+4),readWord(ram,p+8),readWord(ram,p+12),
                                   readWord(ram,p+16),static_cast<int32_t>(readWord(ram,p+20)),readWord(ram,p+24)});
            SET_GPR_S32(cpu,2,id);
            log<<"CreateThread id="<<id;
            if(auto t=threads.get(id)) log<<" entry=0x"<<std::hex<<t->descriptor.entry<<" stack=0x"<<t->descriptor.stack
                <<std::dec<<" priority="<<t->descriptor.priority;
            log<<'\n'; return;
        }
        if(number==0x21) {
            int id=GPR_S32(cpu,4),status=threads.erase(id);
            SET_GPR_S32(cpu,2,status<0?-1:id); return;
        }
        if(number==0x22) {
            int id=GPR_S32(cpu,4); uint32_t arg=GPR_U32(cpu,5);
            if(threads.start(id)<0) { SET_GPR_S32(cpu,2,-1); return; }
            const auto d=threads.get(id)->descriptor;
            ThreadContext initial{}; auto* child=&initial.cpu;
            child->pc=d.entry; child->cop0_status=cpu->cop0_status&~6u;
            SET_GPR_U32(child,4,arg); SET_GPR_U32(child,28,d.gp);
            SET_GPR_U32(child,29,((d.stack+d.size)&~15u)-16u);
            SET_GPR_U32(child,31,thread.root);
            thread_contexts[id]=std::move(initial);
            SET_GPR_S32(cpu,2,id);
            log<<"StartThread id="<<id<<" arg=0x"<<std::hex<<arg<<std::dec<<'\n';
            reschedule(cpu); return;
        }
        if(number==0x23) { threads.exitCurrent(); reschedule(cpu); return; }
        if(number==0x29) {
            auto previous=threads.changePriority(GPR_S32(cpu,4),GPR_S32(cpu,5));
            SET_GPR_S32(cpu,2,previous);
            if(previous>=0) reschedule(cpu);
            return;
        }
        if(number==0x2f) { SET_GPR_S32(cpu,2,threads.current()); return; }
        if(number==0x44 || number==0x45 || number==0x46) {
            int id=GPR_S32(cpu,4);
            if(!semaphores.find(id)) { SET_GPR_S32(cpu,2,-1); return; }
            if(semaphores.poll(id)>=0) { SET_GPR_S32(cpu,2,id); return; }
            if(number!=0x44) { SET_GPR_S32(cpu,2,-1); return; }
            SET_GPR_S32(cpu,2,id); ++semaphore_waits;
            if(semaphore_waits<=64) log<<"WaitSema thread="<<threads.current()<<" id="<<id<<'\n';
            threads.block(mp2r::EeThreads::Wait::Semaphore,id); reschedule(cpu); return;
        }
        if(number==0x42 || number==0x43) {
            int id=GPR_S32(cpu,4); const auto* semaphore=semaphores.find(id);
            if(!semaphore) { SET_GPR_S32(cpu,2,-1); return; }
            int woken=threads.wakeSemaphore(id,(semaphore->attributes&1)!=0);
            SET_GPR_S32(cpu,2,woken?(number==0x43?-2:id):semaphores.signal(id));
            if(woken && number==0x42) reschedule(cpu);
            return;
        }
        if(number==0x10 || number==0x12) {
            auto entry=GPR_U32(cpu,5);
            auto& chains=number==0x10?intc:dmac_handlers;
            int cause=GPR_S32(cpu,4);
            auto id=number==0x12 && cause>=10?-1:chains.add(cause,entry,GPR_S32(cpu,6),GPR_U32(cpu,7),GPR_U32(cpu,28));
            SET_GPR_S32(cpu,2,id);
            log<<(number==0x10?"AddIntcHandler cause=":"AddDmacHandler channel=")<<cause<<" entry=0x"<<std::hex<<entry<<std::dec<<" id="<<id<<'\n';
            return;
        }
        if(number==0x11 || number==0x13) {
            auto& chains=number==0x11?intc:dmac_handlers;
            SET_GPR_S32(cpu,2,chains.remove(GPR_S32(cpu,4),GPR_S32(cpu,5))<0?-1:0);
            return;
        }
        if(number==0x32) {
            SET_GPR_S32(cpu,2,threads.current());
            if(threads.sleepCurrent()) reschedule(cpu);
            return;
        }
        if(number==0x33 || number==0x34) {
            int id=GPR_S32(cpu,4),woken=id==threads.current()?-1:threads.wakeup(id);
            SET_GPR_S32(cpu,2,woken<0?-1:id);
            if(woken>0 && number==0x33) reschedule(cpu);
            return;
        }
        if(number==0x35 || number==0x36) { SET_GPR_S32(cpu,2,threads.cancelWakeups(GPR_S32(cpu,4))); return; }
        if(number==0x02) { // SetGsCrt(interlace, mode, field): recorded; display output is the renderer's job
            gs_crt={GPR_U32(cpu,4),GPR_U32(cpu,5),GPR_U32(cpu,6)};
            log<<"SetGsCrt interlace="<<gs_crt[0]<<" mode=0x"<<std::hex<<gs_crt[1]<<std::dec<<" field="<<gs_crt[2]<<'\n';
            return;
        }
        if(number==0x70) { SET_GPR_U64(cpu,2,gs_imr); return; }
        if(number==0x71) { gs_imr=GPR_U32(cpu,4); SET_GPR_U64(cpu,2,gs_imr); return; }
        if(number==0x73) { vsync_flag=GPR_U32(cpu,4); vsync_csr=GPR_U32(cpu,5); return; }
        if(number==0x7f) { SET_GPR_U32(cpu,2,PS2_RAM_SIZE); return; }
        if(number==0x79) { iop.setReg(GPR_U32(cpu,4),GPR_U32(cpu,5)); SET_GPR_U32(cpu,2,0); return; }
        if(number==0x7a) { SET_GPR_U32(cpu,2,iop.getReg(GPR_U32(cpu,4))); return; }
        if(number==0x77) {
            auto id=iop.setDma(ram,GPR_U32(cpu,4),GPR_U32(cpu,5));
            checkIop(cpu); iop.pump(ram,dmac_stat);
            SET_GPR_U32(cpu,2,id); return;
        }
        if(number==0x78) { iop.setDChain(ram,dmac_stat); SET_GPR_U32(cpu,2,0); return; }
        if(number==0x76) { SET_GPR_S32(cpu,2,-1); return; } // SifDmaStat: transfers complete synchronously
        if(number==0x16 || number==0x17) {
            auto channel=GPR_U32(cpu,4);
            if(channel>=10) { SET_GPR_S32(cpu,2,-1); return; }
            auto bit=1u<<(16+channel);
            bool previous=(dmac_stat&bit)!=0,enabled=number==0x16;
            if(previous!=enabled) dmac_stat^=bit;
            SET_GPR_S32(cpu,2,previous!=enabled?1:0);
            return;
        }
        if(number==0x14 || number==0x15) {
            auto cause=GPR_U32(cpu,4);
            if(cause>=15) { SET_GPR_S32(cpu,2,-1); return; }
            auto bit=1u<<cause;
            bool previous=(intc.mask()&bit)!=0,enabled=number==0x14;
            if(previous!=enabled) intc.toggleMask(bit);
            SET_GPR_S32(cpu,2,previous!=enabled?1:0);
            return;
        }
        if(number==0x64) {
            auto mode=GPR_U32(cpu,4);
            if(mode!=0 && mode!=2) throw std::runtime_error("Unsupported FlushCache mode");
            verifyKernelCopy(ram);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            return;
        }
        if(number==0x74) {
            uint32_t index=GPR_U32(cpu,4),handler=GPR_U32(cpu,5);
            if(handler && !entries.contains(handler)) { stopMissing(cpu,handler,cpu->pc-4); throw GuestTransfer{}; }
            syscall_table.install(ram,index,handler);
            SET_GPR_S32(cpu,2,0);
            log<<"SetSyscall index=0x"<<std::hex<<index<<" handler=0x"<<handler<<std::dec<<'\n';
            return;
        }
        if(auto handler=syscall_table.handler(number)) {
            if(syscall_frames.size()==8) throw std::runtime_error("Nested syscall frame limit");
            syscall_frames.push_back({cpu->pc,GPR_U32(cpu,31)});
            SET_GPR_U32(cpu,31,cpu->pc); cpu->pc=handler;
            throw GuestTransfer{};
        }
        if(number!=0x40) throw std::runtime_error("Unimplemented syscall 0x"+hex(number));
        uint32_t param=GPR_U32(cpu,4);
        int id=-1;
        if(param!=0 && param<=PS2_RAM_SIZE-24 && !(param&3))
            id=semaphores.create(static_cast<int32_t>(readWord(ram,param+8)),
                static_cast<int32_t>(readWord(ram,param+4)),readWord(ram,param+16),readWord(ram,param+20));
        SET_GPR_S32(cpu,2,id);
        log<<"CreateSema param=0x"<<std::hex<<param<<std::dec<<" id="<<id<<'\n';
    }
    mp2r::EeSpecialMemory special;
    [[noreturn]] void stopAccess(R5900Context* cpu,const char* why,uint32_t address) {
        reason=why; memory_address=address; blocked_source=cpu->pc; throw MemoryStop{};
    }
    template<class T> T loadSpecial(R5900Context* cpu,uint32_t address,const char* why) {
        T value{}; if(!special.read(address,&value,sizeof(T))) stopAccess(cpu,why,address); return value;
    }
    template<class T> void storeSpecial(R5900Context* cpu,uint32_t address,T value,const char* why) {
        if(!special.write(address,&value,sizeof(T))) stopAccess(cpu,why,address);
    }
    // GS privileged registers (0x12000000-0x12001FFF): 64-bit, 16-byte stride. CSR reports
    // VSINT (bit3, latched at VBLANK start, write-1-clear) and FIELD (bit13); others hold writes.
    std::unordered_map<uint32_t,uint64_t> gs_registers;
    static bool gsPrivileged(uint32_t address) { return (address&0x1fffe000u)==0x12000000u; }
    uint64_t gsRead(uint32_t address) {
        auto p=address&0x1fffffffu;
        if(p==0x12001000u) return gs_csr|(vblank.oddField()?0x2000u:0u)|(0x1bull<<16)|(0x55ull<<24);
        auto it=gs_registers.find(p); return it==gs_registers.end()?0:it->second;
    }
    void gsWrite(uint32_t address,uint64_t value) {
        auto p=address&0x1fffffffu;
        if(p==0x12001000u) { gs_csr&=~uint32_t(value&0x1fu); if(value&0x200u) gs_csr=0; return; }
        gs_registers[p]=value;
    }
    uint64_t Load64(uint8_t*,R5900Context* cpu,uint32_t a) {
        if(gsPrivileged(a)) return gsRead(a);
        return loadSpecial<uint64_t>(cpu,a,"unsupported_mmio_read64");
    }
    uint16_t Load16(uint8_t*,R5900Context* cpu,uint32_t a) { return loadSpecial<uint16_t>(cpu,a,"unsupported_mmio_read16"); }
    uint8_t Load8(uint8_t*,R5900Context* cpu,uint32_t a) { return loadSpecial<uint8_t>(cpu,a,"unsupported_mmio_read8"); }
    __m128i Load128(uint8_t*,R5900Context* cpu,uint32_t a) { return loadSpecial<__m128i>(cpu,a&~15u,"unsupported_mmio_read128"); }
    void Store8(uint8_t*,R5900Context* cpu,uint32_t a,uint8_t v) { storeSpecial(cpu,a,v,"unsupported_mmio_write8"); }
    void Store16(uint8_t*,R5900Context* cpu,uint32_t a,uint16_t v) { storeSpecial(cpu,a,v,"unsupported_mmio_write16"); }
    void Store64(uint8_t*,R5900Context* cpu,uint32_t a,uint64_t v) {
        if(gsPrivileged(a)) { gsWrite(a,v); return; }
        storeSpecial(cpu,a,v,"unsupported_mmio_write64");
    }
    void Store128(uint8_t*,R5900Context* cpu,uint32_t a,__m128i v) { storeSpecial(cpu,a&~15u,v,"unsupported_mmio_write128"); }
    // Guest faults stop precisely; MP2R has no BIOS exception vectors to deliver them to.
    void handleBreak(uint8_t*,R5900Context* cpu) { stopAccess(cpu,"guest_break",cpu->pc); }
    void SignalException(R5900Context* cpu,PS2Exception exception) {
        reason="guest_exception_"+std::to_string(int(exception)); blocked_source=cpu->pc; throw MemoryStop{};
    }
    void executeVU0Microprogram(uint8_t*,R5900Context* cpu,uint32_t address) { stopAccess(cpu,"vu0_microprogram_required",address); }
    static std::string hex(uint32_t value) {
        std::ostringstream text; text<<std::hex<<value; return text.str();
    }
    uint32_t Load32(uint8_t*,R5900Context* cpu,uint32_t address) {
        clockTimers();
        if(uint32_t value; special.read(address,&value,4)) return value;
        if((address&0x1fffffff)==0x1000e010) return dmac_stat;
        if(auto p=address&0x1fffffffu; p>=0x1000f200u && p<=0x1000f230u && !(p&0xf)) return iop.getReg(((p-0x1000f200u)>>4)+1);
        if(gsPrivileged(address)) return uint32_t(gsRead(address&~7u)>>((address&4)*8));
        if((address&0x1fffffff)==0x1000f000) return intc.status();
        if((address&0x1fffffff)==0x1000f010) return intc.mask();
        if(timer3.owns(address&0x1fffffff)) return timer3.read(address&0x1fffffff);
        if(timer2.owns(address&0x1fffffff)) return timer2.read(address&0x1fffffff);
        reason="unsupported_mmio_read32"; memory_address=address; blocked_source=cpu->pc;
        throw MemoryStop{};
    }
    void Store32(uint8_t*,R5900Context* cpu,uint32_t address,uint32_t value) {
        clockTimers();
        if(special.write(address,&value,4)) return;
        if(auto p=address&0x1fffffffu; p>=0x1000f200u && p<=0x1000f230u && !(p&0xf)) { iop.setReg(((p-0x1000f200u)>>4)+1,value); return; }
        if((address&0x1fffffff)==0x1000e010) { dmac_stat=(dmac_stat&~(value&0x3ffu))^(value&0x3ff0000u); return; }
        if(gsPrivileged(address) && !(address&4)) { gsWrite(address,value); return; }
        if((address&0x1fffffff)==0x1000f000) { intc.acknowledge(value); return; }
        if((address&0x1fffffff)==0x1000f010) { intc.toggleMask(value); return; }
        if(timer2.owns(address&0x1fffffff)) {
            timer2.write(address&0x1fffffff,value);
            if(!timer2.needsVideoClock()) return;
            reason="timer2_video_clock_required"; memory_address=address; blocked_source=cpu->pc; throw MemoryStop{};
        }
        if(timer3.owns(address&0x1fffffff)) {
            timer3.write(address&0x1fffffff,value);
            if(!timer3.needsVideoClock()) return;
            reason="timer3_video_clock_required";
        } else reason="unsupported_mmio_write32";
        memory_address=address; blocked_source=cpu->pc; throw MemoryStop{};
    }
};
using MP2Runtime=MP2InitializationRuntime;
