#include "initialization_probe.h"
#include "bootstrap_runtime.h"
#include "ee_semaphores.h"
#include "ee_timer3.h"
#include "ee_intc.h"
#include "ee_threads.h"
#include "ee_osd_config.h"
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

class MP2InitializationRuntime : public mp2r::BootstrapRuntime {
public:
    struct GuestTransfer {};
    struct MemoryStop {};
    struct SyscallFrame { uint32_t resume,saved_ra; };
    std::vector<SyscallFrame> syscall_frames;
    mp2r::KernelSyscallTable syscall_table;
    using Fn=void(*)(uint8_t*,R5900Context*,MP2InitializationRuntime*);
    std::unordered_map<uint32_t,Fn> entries;
    mp2r::EeSemaphores semaphores;
    mp2r::EeTimer3Registers timer3;
    mp2r::EeTimerRegisters timer2{0x10001000};
    mp2r::EeIntc intc;
    mp2r::EeThreads threads;
    mp2r::EeOsdConfig osd;
    unsigned osd_reads{},osd_writes{};
    struct ThreadContext { R5900Context cpu; std::vector<SyscallFrame> syscall_frames; };
    std::unordered_map<int,ThreadContext> thread_contexts;
    unsigned context_switches{},semaphore_waits{};
    void reschedule(R5900Context* cpu) {
        int previous=threads.current(),next=threads.schedule();
        if(!next) { reason="no_runnable_threads"; blocked_source=cpu->pc; throw MemoryStop{}; }
        if(previous==next) return;
        cpu->llbit=0;
        thread_contexts[previous]={*cpu,std::move(syscall_frames)};
        auto& selected=thread_contexts.at(next);
        *cpu=selected.cpu; syscall_frames=std::move(selected.syscall_frames);
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
        if(intc.pending()) { reason="interrupt_delivery_required"; throw MemoryStop{}; }
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
    unsigned dispatches{},executions{};
    uint32_t source{};
    uint32_t memory_address{};
    std::string reason;
    std::ostream& log;
    unsigned checkpoint_limit;
    explicit MP2InitializationRuntime(std::ostream& output,uint32_t table_base,unsigned limit)
        :syscall_table(table_base),log(output),checkpoint_limit(limit) {}
    bool eeCheckpointDue(uint32_t=32) {
        clockTimers();
        ++checkpoints;
        return checkpoints>=checkpoint_limit || checkpoints%4096==0;
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
        if(number==uint32_t(-0x43)) number=0x43;
        if(number==uint32_t(-0x46)) number=0x46;
        if(number==0x3c || number==0x3d) {
            mp2r::BootstrapRuntime::handleSyscall(ram,cpu,encoded);
            if(number==0x3c) threads.bindMain({0,0x100008,thread.stack,thread.size,thread.gp,0,0});
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
        if(number==0x10) {
            auto entry=GPR_U32(cpu,5);
            auto id=intc.add(GPR_S32(cpu,4),entry,GPR_S32(cpu,6),GPR_U32(cpu,7));
            SET_GPR_S32(cpu,2,id);
            log<<"AddIntcHandler cause="<<GPR_S32(cpu,4)<<" entry=0x"<<std::hex<<entry<<std::dec<<" id="<<id<<'\n';
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
    uint64_t Load64(uint8_t*,R5900Context*,uint32_t) { throw std::runtime_error("Unimplemented special Load64"); }
    uint16_t Load16(uint8_t*,R5900Context*,uint32_t) { throw std::runtime_error("Unimplemented special Load16"); }
    uint8_t Load8(uint8_t*,R5900Context*,uint32_t) { throw std::runtime_error("Unimplemented special Load8"); }
    void Store16(uint8_t*,R5900Context*,uint32_t,uint16_t) { throw std::runtime_error("Unimplemented special Store16"); }
    static std::string hex(uint32_t value) {
        std::ostringstream text; text<<std::hex<<value; return text.str();
    }
    uint32_t Load32(uint8_t*,R5900Context* cpu,uint32_t address) {
        clockTimers();
        if((address&0x1fffffff)==0x1000f000) return intc.status();
        if((address&0x1fffffff)==0x1000f010) return intc.mask();
        if(timer3.owns(address&0x1fffffff)) return timer3.read(address&0x1fffffff);
        if(timer2.owns(address&0x1fffffff)) return timer2.read(address&0x1fffffff);
        reason="unsupported_mmio_read32"; memory_address=address; blocked_source=cpu->pc;
        throw MemoryStop{};
    }
    void Store32(uint8_t*,R5900Context* cpu,uint32_t address,uint32_t value) {
        clockTimers();
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
    void Store64(uint8_t*,R5900Context*,uint32_t,uint64_t) { throw std::runtime_error("Unimplemented special Store64"); }
};
#define PS2Runtime MP2InitializationRuntime
#include "selected_functions.inc"
#undef PS2Runtime

namespace mp2r {
namespace {
uint32_t word(const uint8_t* bytes,std::size_t size,std::size_t offset) {
    if(offset>size || size-offset<4) throw std::runtime_error("Truncated ELF field");
    uint32_t value; std::memcpy(&value,bytes+offset,4); return value;
}
uint16_t half(const uint8_t* bytes,std::size_t size,std::size_t offset) {
    if(offset>size || size-offset<2) throw std::runtime_error("Truncated ELF field");
    return uint16_t(bytes[offset])|uint16_t(bytes[offset+1])<<8;
}
}
InitializationResult runInitialization(const uint8_t* elf,std::size_t bytes,std::ostream& log,
                                     uint32_t kernel_table_base,unsigned max_checkpoints) {
    if(bytes!=4984624 || word(elf,bytes,0)!=0x464c457fu || elf[4]!=1 || elf[5]!=1 || half(elf,bytes,18)!=8 || word(elf,bytes,24)!=0x100008)
        throw std::runtime_error("Unexpected PAL ELF layout; caller must verify SHA-256");
    std::vector<uint8_t> ram(PS2_RAM_SIZE,0);
    auto phoff=word(elf,bytes,28); auto stride=half(elf,bytes,42); auto count=half(elf,bytes,44);
    if(stride!=32 || count!=2) throw std::runtime_error("Unexpected program headers");
    for(unsigned i=0;i<count;++i) {
        auto p=std::size_t(phoff)+i*stride;
        if(word(elf,bytes,p)!=1) continue;
        auto offset=word(elf,bytes,p+4),va=word(elf,bytes,p+8),filesz=word(elf,bytes,p+16),memsz=word(elf,bytes,p+20);
        if(filesz>memsz || offset>bytes || filesz>bytes-offset || va>PS2_RAM_SIZE || memsz>PS2_RAM_SIZE-va)
            throw std::runtime_error("Invalid ELF load range");
        std::copy(elf+offset,elf+offset+filesz,ram.begin()+va);
        // Poison BSS to verify original startup clearing, not just loader zero-fill.
        std::fill(ram.begin()+va+filesz,ram.begin()+va+memsz,0xa5);
    }
    R5900Context cpu{}; cpu.pc=0x100008;
    if(max_checkpoints==0) throw std::runtime_error("Zero checkpoint budget");
    MP2InitializationRuntime host(log,kernel_table_base,max_checkpoints); registerSelected(host);
    try {
        while(!host.blocked && host.executions<200000) {
            host.clockTimers();
            if(host.checkpoints>=max_checkpoints) { host.reason="checkpoint_budget"; break; }
            auto it=host.entries.find(cpu.pc);
            if(it==host.entries.end()) { host.stopMissing(&cpu,cpu.pc,host.source); break; }
            if(!host.syscall_frames.empty() && cpu.pc==host.syscall_frames.back().resume) {
                auto frame=host.syscall_frames.back(); host.syscall_frames.pop_back();
                auto* context=&cpu; SET_GPR_U32(context,31,frame.saved_ra);
            }
            ++host.executions;
            if(cpu.pc>=0x80076000 && cpu.pc<0x80076740) host.verifyKernelCopy(ram.data());
            try { it->second(ram.data(),&cpu,&host); }
            catch(const MP2InitializationRuntime::GuestTransfer&) { continue; }
        }
        if(!host.blocked && host.reason.empty()) host.reason="execution_budget";
    } catch(const MP2InitializationRuntime::MemoryStop&) {
        // Precise stop at an unsupported hardware access; no fabricated register value.
    } catch(const std::exception& error) { host.reason=error.what(); }
    log<<"stop reason="<<host.reason<<" pc=0x"<<std::hex<<cpu.pc<<" source=0x"<<host.blocked_source
       <<" address=0x"<<host.memory_address<<std::dec<<" steps="<<host.executions<<" dispatches="<<host.dispatches<<" semaphores="<<host.semaphores.size()<<'\n';
    log<<"threads="<<host.threads.size()<<" switches="<<host.context_switches<<" semaphore_waits="<<host.semaphore_waits<<'\n';
    if(host.context_switches==2 && host.semaphore_waits==1 && host.threads.current()==1) {
        const auto* helper=host.threads.get(2);
        const auto& saved=host.thread_contexts.at(2).cpu; const auto* context=&saved;
        if(host.threads.current()!=1 || !helper || helper->state!=EeThreads::State::Waiting ||
           helper->wait!=EeThreads::Wait::Semaphore || helper->wait_id!=3 ||
           MP2InitializationRuntime::readWord(ram.data(),0x54c798)!=2 ||
           MP2InitializationRuntime::readWord(ram.data(),0x5c2080)!=3 ||
           GPR_U32(context,28)!=0x5c8870 || GPR_U32(context,29)<0x5c1c80 || GPR_U32(context,29)>=0x5c2080 ||
           saved.pc!=0x10d628 || !host.thread_contexts.at(2).syscall_frames.empty())
            throw std::runtime_error("Original helper thread wait/context mismatch");
        log<<"original helper thread waiting on semaphore 3; main context restored\n";
    }
    if(host.semaphores.size()>=2) {
        auto a=MP2InitializationRuntime::readWord(ram.data(),0x54ced0),b=MP2InitializationRuntime::readWord(ram.data(),0x54ced4);
        if(a==b || !host.semaphores.find(a) || !host.semaphores.find(b)) throw std::runtime_error("Original semaphore ID stores mismatch");
        log<<"original semaphore stores verified: "<<a<<", "<<b<<'\n';
    }
    bool alarm_ready=host.syscall_table.handler(8)==0x800766c0;
    if(alarm_ready) {
        if(!host.kernel_copy_verified || MP2InitializationRuntime::readWord(ram.data(),0x54ceb8)!=host.syscall_table.virtualBase())
            throw std::runtime_error("Original syscall-table discovery/return mismatch");
        log<<"original syscall-table discovery verified: 0x"<<std::hex<<host.syscall_table.virtualBase()<<std::dec<<'\n';
        if(host.syscall_table.handler(0xfc)!=0x80076440 || host.syscall_table.handler(0xfe)!=0x80076440 ||
           host.syscall_table.handler(0xfd)!=0x800762a0 || host.syscall_table.handler(0xff)!=0x800762a0 ||
           host.syscall_table.handler(8)!=0x800766c0 || host.syscall_table.timer3InterruptHandler()!=0x80076488)
            throw std::runtime_error("Original alarm patch installation mismatch");
        log<<"original alarm patch installation verified: 4 services, INTC12, return trampoline\n";
    }
    return {host.reason,cpu.pc,host.blocked_source,host.memory_address,host.executions,host.dispatches,host.semaphores.size(),host.syscall_count,alarm_ready,
            host.threads.size(),host.context_switches,host.semaphore_waits};
}
}
