#include "initialization_probe.h"
#include "mp2_runtime.h"
#include <thread>
#include <cstdlib>
#include <fstream>

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
InitializationResult runInitialization(const uint8_t* elf,std::size_t bytes,std::ostream& log,std::string_view boot_path,
                                     uint32_t kernel_table_base,unsigned max_checkpoints,uint64_t max_executions,
                                     std::string_view disc_root) {
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
    MP2InitializationRuntime host(log,kernel_table_base,max_checkpoints); host.registerGuestFunctions();
    host.boot_path=boot_path; host.guest_ram=ram.data();
    if(!disc_root.empty()) { host.disc_root=std::filesystem::path(disc_root); host.disc.emplace(host.disc_root); host.iop_kernel.disc=&*host.disc; host.registerIopServices(); }
    try {
        while(!host.blocked && host.executions<max_executions) {
            host.clockTimers();
            if(host.checkpoints>=max_checkpoints) { host.reason="checkpoint_budget"; break; }
            if(cpu.pc==MP2InitializationRuntime::interrupt_return && host.interrupt) { host.finishHandler(&cpu); continue; }
            if(host.beginInterrupt(&cpu)) continue;
            if(host.idle && !host.interrupt) {     // handlers entered from idle run normally
                if(cpu.pc!=MP2InitializationRuntime::idle_pc) throw std::runtime_error("Idle context lost");
                host.runIop(200000); host.checkIop(&cpu);
                if(!host.iop_kernel.idle()) continue;
                auto waited=std::chrono::steady_clock::now()-host.idle_since;
                if(host.idle_since.time_since_epoch().count()==0) host.idle_since=std::chrono::steady_clock::now();
                else if(waited>std::chrono::seconds(5)) { host.reason="idle_without_interrupts"; break; }
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                continue;
            }
            host.idle_since={};
            if((host.executions&255)==0) { host.runIop(20000); host.checkIop(&cpu); }
            auto it=host.entries.find(cpu.pc);
            if(it==host.entries.end()) { host.stopMissing(&cpu,cpu.pc,host.source); break; }
            if(!host.syscall_frames.empty() && cpu.pc==host.syscall_frames.back().resume) {
                auto frame=host.syscall_frames.back(); host.syscall_frames.pop_back();
                auto* context=&cpu; SET_GPR_U32(context,31,frame.saved_ra);
            }
            ++host.executions;
            if(cpu.pc>=0x80076000 && cpu.pc<0x80076740) host.verifyKernelCopy(ram.data());
            if(cpu.pc>=0x80075000 && cpu.pc<0x80075300) host.verifyTlbCopy(ram.data());
            try { it->second(ram.data(),&cpu,&host); }
            catch(const MP2InitializationRuntime::GuestTransfer&) { continue; }
        }
        if(!host.blocked && host.reason.empty()) host.reason="execution_budget";
    } catch(const MP2InitializationRuntime::MemoryStop&) {
        // Precise stop at an unsupported hardware access; no fabricated register value.
    } catch(const std::exception& error) { host.reason=error.what(); }
    // Diagnostics: MP2R_DUMP_RAM=<file> writes guest RAM and registers at the stop (private data).
    if(const char* dump=std::getenv("MP2R_DUMP_RAM")) {
        std::ofstream out(dump,std::ios::binary);
        out.write(reinterpret_cast<const char*>(ram.data()),std::streamsize(ram.size()));
        out.write(reinterpret_cast<const char*>(&cpu),sizeof cpu);
    }
    log<<"stop reason="<<host.reason<<" pc=0x"<<std::hex<<cpu.pc<<" source=0x"<<host.blocked_source
       <<" address=0x"<<host.memory_address<<std::dec<<" steps="<<host.executions<<" dispatches="<<host.dispatches<<" semaphores="<<host.semaphores.size()<<'\n';
    if(host.dispatches>64) {
        log<<"last branches:";
        for(unsigned i=0;i<host.recent_branches.size();++i) {
            const auto& b=host.recent_branches[(host.dispatches+i)%host.recent_branches.size()];
            log<<" 0x"<<std::hex<<b.first<<"->0x"<<b.second<<std::dec;
        }
        log<<'\n';
    }
    host.iop_kernel.dumpState(log);
    log<<"IPU commands="<<host.ipu.commands<<" macroblocks="<<host.ipu.macroblocks<<" GIF tags="<<host.gif.tags<<" GS image bytes="<<host.gs_image_bytes
       <<" DMA qwords="<<host.dmac.qwords_transferred<<" VIF1 unpacked="<<host.vif1.unpacked_qwords<<" interrupts="<<host.interrupts_delivered<<"\n";
    {
        const auto& d4=host.dmac.channels[4]; const auto& d3=host.dmac.channels[3];
        log<<std::hex<<"IPU ctrl=0x"<<host.ipu.readCtrl()<<" bp=0x"<<host.ipu.readBp()<<" cmd=0x"<<host.ipu.readCmd()<<" queued="<<std::dec<<host.ipu.queuedQwords()
           <<" out="<<host.ipu.output.size()<<std::hex<<" D4 chcr=0x"<<d4.chcr<<" madr=0x"<<d4.madr<<" qwc=0x"<<d4.qwc<<" tadr=0x"<<d4.tadr
           <<" D3 chcr=0x"<<d3.chcr<<" madr=0x"<<d3.madr<<" qwc=0x"<<d3.qwc<<" dstat=0x"<<host.dmac_stat<<std::dec<<'\n';
        for(std::size_t i=1;i<=host.ipu_recent.size();++i)
            if(auto& r=host.ipu_recent[(host.ipu_log+i)%host.ipu_recent.size()]; !r.empty() && i+8>host.ipu_recent.size()) log<<"recent "<<r<<'\n';
    }
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
    bool tlb_ready=host.syscall_table.handler(0x55)==0x80075038;
    if(tlb_ready) {
        host.verifyTlbCopy(ram.data());
        if(host.syscall_table.handler(0x56)!=0x800750c8 || host.syscall_table.handler(0x57)!=0x80075108 ||
           host.syscall_table.handler(0x58)!=0x80075158 || host.syscall_table.handler(0x59)!=0x800751a8 ||
           host.syscall_table.handler(0x5b)!=0x80075000)
            throw std::runtime_error("Original TLB service installation mismatch");
        log<<"original TLB service installation verified: Put/Set/Get/Probe/ExpandScratchPad and lookup\n";
    }
    bool main_entry=host.reason=="missing_function" && cpu.pc==0x42f7e0;
    if(main_entry) {
        auto* context=&cpu; auto argv=GPR_U32(context,5);
        auto argv0=MP2InitializationRuntime::readWord(ram.data(),argv);
        if(GPR_U32(context,4)!=1 || argv!=host.thread.args+4 || argv0>=PS2_RAM_SIZE-boot_path.size() ||
           std::string_view(reinterpret_cast<const char*>(ram.data()+argv0),boot_path.size()+1)!=std::string(boot_path)+'\0' ||
           !(cpu.cop0_status&0x10000))
            throw std::runtime_error("Original main entry arguments/interrupt state mismatch");
        log<<"original crt0 reached main(argc=1, argv[0]="<<boot_path<<") with EIE set\n";
    }
    return {host.reason,cpu.pc,host.blocked_source,host.memory_address,host.executions,host.dispatches,host.semaphores.size(),host.syscall_count,alarm_ready,
            host.threads.size(),host.context_switches,host.semaphore_waits,tlb_ready,main_entry};
}
}
