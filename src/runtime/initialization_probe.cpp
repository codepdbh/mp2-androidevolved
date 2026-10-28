#include "initialization_probe.h"
#include "mp2_runtime.h"
#include <thread>
#include <cstdlib>
#include <fstream>
#include <deque>
#include <sstream>

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
                                     std::string_view disc_root,const RuntimeCallbacks& callbacks) {
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
    host.present_frame=callbacks.present_frame;
    host.compensate_scene_preview=callbacks.compensate_scene_preview;
    host.profile=host.gs.profile=callbacks.profile;
    if(callbacks.play_audio) host.libsd.stream_pcm=[&](unsigned core,const int16_t* pcm,uint32_t frames){
        if(core==0) callbacks.play_audio(pcm,frames); // stream tap; full SPU2/core mixing remains separate
    };
    if(callbacks.poll_pad) host.sio2.sample_input=[&](unsigned port,DualShock2& pad){
        if(port!=0) return;
        auto input=callbacks.poll_pad();
        pad.buttons=uint16_t(~input.pressed); pad.sticks=input.sticks; pad.pressure=input.pressure;
    };
    if(!disc_root.empty()) { host.disc_root=std::filesystem::path(disc_root); host.disc.emplace(host.disc_root); host.iop_kernel.disc=&*host.disc; host.registerIopServices(); }
    // Diagnostics: MP2R_WATCH=<hex addr> with MP2R_WATCH_VALUE=<hex> reports, between executor steps,
    // the first time that RAM word takes the value (pc and recent branches).
    uint32_t watch=[]{ const char* v=std::getenv("MP2R_WATCH"); return v?uint32_t(std::strtoul(v,nullptr,16)):0u; }();
    std::deque<std::string> watch_log;
    uint32_t watch_value=[]{ const char* v=std::getenv("MP2R_WATCH_VALUE"); return v?uint32_t(std::strtoul(v,nullptr,16)):0u; }();
    // MP2R_WALL_LIMIT=<seconds>: stop with the full diagnostic report after that much real time.
    double wall_limit=[]{ const char* v=std::getenv("MP2R_WALL_LIMIT"); return v?std::atof(v):0.0; }();
    auto wall_start=std::chrono::steady_clock::now(); uint64_t wall_checks=0,callback_checks=0;
    std::map<uint32_t,std::pair<uint64_t,uint64_t>> sampled_functions; // samples, elapsed ns
    uint32_t sampling_random=0x9e3779b9;
    try {
        while(!host.blocked && host.executions<max_executions) {
            if(callbacks.break_pc && cpu.pc==callbacks.break_pc) { host.reason="breakpoint"; host.blocked_source=host.source; break; }
            if((callback_checks++&0x3ff)==0 && callbacks.should_stop && callbacks.should_stop()) { host.reason="requested_stop"; break; }
            if(wall_limit>0 && (++wall_checks&0xfff)==0 &&
               std::chrono::duration<double>(std::chrono::steady_clock::now()-wall_start).count()>wall_limit) { host.reason="wall_limit"; break; }
            if(watch && !std::getenv("MP2R_WATCH_VALUE")) {                      // every change, last 60 kept
                uint32_t now=MP2InitializationRuntime::readWord(ram.data(),watch);
                if(now!=watch_value) {
                    std::ostringstream line; line<<std::hex<<"watch 0x"<<watch<<": 0x"<<watch_value<<" -> 0x"<<now<<" pc=0x"<<cpu.pc<<" sp=0x"<<GPR_U32((&cpu),29)<<" last=0x"
                        <<host.recent_branches[(host.dispatches+host.recent_branches.size()-1)%host.recent_branches.size()].first<<"->0x"
                        <<host.recent_branches[(host.dispatches+host.recent_branches.size()-1)%host.recent_branches.size()].second;
                    watch_log.push_back(line.str()); if(watch_log.size()>60) watch_log.pop_front();
                    watch_value=now;
                }
            } else if(watch && MP2InitializationRuntime::readWord(ram.data(),watch)==watch_value) {
                log<<"watch 0x"<<std::hex<<watch<<" = 0x"<<watch_value<<" at pc=0x"<<cpu.pc<<" irq="<<bool(host.interrupt)<<" recent:";
                for(unsigned i=0;i<16;++i) { const auto& b=host.recent_branches[(host.dispatches+host.recent_branches.size()-16+i)%host.recent_branches.size()]; log<<" 0x"<<b.first<<"->0x"<<b.second; }
                log<<std::dec<<'\n'; watch=0;
            }
            host.clockTimers();
            if(host.checkpoints>=max_checkpoints) { host.reason="checkpoint_budget"; break; }
            if(cpu.pc==MP2InitializationRuntime::interrupt_return && host.interrupt) { host.finishHandler(&cpu); continue; }
            if(host.beginInterrupt(&cpu)) continue;
            if(host.idle && !host.interrupt) {     // handlers entered from idle run normally
                if(cpu.pc!=MP2InitializationRuntime::idle_pc) throw std::runtime_error("Idle context lost");
                uint64_t iop_before=host.iop_kernel.instructions();
                host.runIop(200000); host.checkIop(&cpu);
                if(!mp2r::EmuClock::real_time) {                       // the IOP's own work while the EE waits (36.864 MHz)
                    mp2r::EmuClock::advance((host.iop_kernel.instructions()-iop_before)*1000/36864*1000/1000);
                    host.clockTimers();
                }
                if(!host.iop_kernel.idle()) continue;
                auto waited=mp2r::EmuClock::now()-host.idle_since;
                if(host.idle_since.time_since_epoch().count()==0) host.idle_since=mp2r::EmuClock::now();
                else if(waited>std::chrono::seconds(5)) { host.reason="idle_without_interrupts"; break; }
                if(mp2r::EmuClock::real_time) std::this_thread::sleep_for(std::chrono::microseconds(200));
                else { mp2r::EmuClock::advance(200000); host.clockTimers(); }   // idle: emulated time moves to the next event
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
            cpu.vu0_vf[0]=_mm_set_ps(1.0f,0.0f,0.0f,0.0f);   // VU0 vf0 is the constant (0,0,0,1) in every context
            if(cpu.pc>=0x80076000 && cpu.pc<0x80076740) host.verifyKernelCopy(ram.data());
            if(cpu.pc>=0x80075000 && cpu.pc<0x80075300) host.verifyTlbCopy(ram.data());
            try {
                bool sample=false; uint64_t ignored=0; uint64_t* elapsed=&ignored;
                if(callbacks.profile) {
                    sampling_random^=sampling_random<<13; sampling_random^=sampling_random>>17; sampling_random^=sampling_random<<5;
                    sample=(sampling_random&1023)==0;
                    if(sample) { auto& entry=sampled_functions[cpu.pc]; ++entry.first; elapsed=&entry.second; }
                }
                RuntimeTimer timing(sample,*elapsed);
                it->second(ram.data(),&cpu,&host);
            }
            catch(const MP2InitializationRuntime::GuestTransfer&) { continue; }
        }
        if(!host.blocked && host.reason.empty()) host.reason="execution_budget";
    } catch(const MP2InitializationRuntime::MemoryStop&) {
        // Precise stop at an unsupported hardware access; no fabricated register value.
    } catch(const std::exception& error) { host.reason=error.what(); }
    // Always retain the last displayed frame when diagnosing a bounded run.
    if(host.dump_frames) host.dumpFrame();
    // Private diagnostic snapshot: GS local memory followed by drawing registers.
    // Display registers remain in the run log; no guest memory is modified.
    if(const char* dump=std::getenv("MP2R_DUMP_GS")) {
        std::ofstream out(dump,std::ios::binary);
        out.write(reinterpret_cast<const char*>(host.gs.vram.data()),std::streamsize(host.gs.vram.size()*sizeof(uint32_t)));
        out.write(reinterpret_cast<const char*>(host.gs.regs.data()),std::streamsize(host.gs.regs.size()*sizeof(uint64_t)));
        if(!out) log<<"GS snapshot write failed: "<<dump<<'\n';
    }
    // Diagnostics: MP2R_DUMP_RAM=<file> writes guest RAM and registers at the stop (private data).
    if(const char* dump=std::getenv("MP2R_DUMP_RAM")) {
        std::ofstream out(dump,std::ios::binary);
        out.write(reinterpret_cast<const char*>(ram.data()),std::streamsize(ram.size()));
        out.write(reinterpret_cast<const char*>(&cpu),sizeof cpu);
    }
    for(const auto& w:watch_log) log<<w<<'\n';
    if(const char* dump=std::getenv("MP2R_DUMP_VU1")) {                       // VU1 code (16 KB) + data (16 KB)
        std::ofstream out(dump,std::ios::binary);
        out.write(reinterpret_cast<const char*>(host.special.vu(0x8000)),0x8000);
    }
    if(const char* dump=std::getenv("MP2R_DUMP_IOP")) {                       // IOP RAM (private data)
        std::ofstream out(dump,std::ios::binary);
        out.write(reinterpret_cast<const char*>(host.iop.ram(0,mp2r::IopSif::ram_size)),std::streamsize(mp2r::IopSif::ram_size));
    }
    log<<"stop reason="<<host.reason<<" pc=0x"<<std::hex<<cpu.pc<<" source=0x"<<host.blocked_source
       <<" address=0x"<<host.memory_address<<std::dec<<" steps="<<host.executions<<" dispatches="<<host.dispatches<<" semaphores="<<host.semaphores.size()<<'\n';
    if(callbacks.profile) log<<"PROFILE wall_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-wall_start).count()
        <<" iop_ns="<<host.iop_ns<<" ipu_ns="<<host.ipu_ns<<" gs_draw_ns="<<host.gs.draw_ns<<" present_ns="<<host.present_ns
        <<" fast_clear_blocks="<<host.gs.fast_clear_blocks<<'\n';
    if(callbacks.profile) {
        log<<"GS PROFILE textured="<<host.gs.textured_primitives<<" types=";
        for(auto n:host.gs.primitive_types) log<<n<<',';
        log<<" formats=";
        for(unsigned n=0;n<host.gs.texture_formats.size();++n)
            if(host.gs.texture_formats[n]) log<<n<<':'<<host.gs.texture_formats[n]<<',';
        log<<'\n';
    }
    if(callbacks.profile) {
        std::vector<std::pair<uint32_t,std::pair<uint64_t,uint64_t>>> samples(sampled_functions.begin(),sampled_functions.end());
        std::sort(samples.begin(),samples.end(),[](const auto& a,const auto& b){ return a.second.second>b.second.second; });
        for(size_t n=0;n<std::min<size_t>(20,samples.size());++n) log<<"PROFILE sample_pc=0x"<<std::hex<<samples[n].first<<std::dec
            <<" samples="<<samples[n].second.first<<" ns="<<samples[n].second.second<<'\n';
    }
    if(host.dispatches>64) {
        log<<"last branches:";
        for(unsigned i=0;i<host.recent_branches.size();++i) {
            const auto& b=host.recent_branches[(host.dispatches+i)%host.recent_branches.size()];
            log<<" 0x"<<std::hex<<b.first<<"->0x"<<b.second<<std::dec;
        }
        log<<'\n';
    }
    host.iop_kernel.dumpState(log);
    for(auto [k,n]:host.vif1.unpack_formats) log<<"UNPACK cmd=0x"<<std::hex<<(k&0xff)<<" usn="<<((k>>8)&1)<<" mode="<<((k>>9)&3)<<" cycle="<<(k>>11)<<std::dec<<" count="<<n<<'\n';
    for(auto [k,n]:host.iop.rpc_histogram) log<<"RPC sid=0x"<<std::hex<<(k>>32)<<" fno="<<std::dec<<(k&0xffffffff)<<" calls="<<n<<'\n';
    log<<"IPU commands="<<host.ipu.commands<<" macroblocks="<<host.ipu.macroblocks<<" GIF tags="<<host.gif.tags<<" GS image bytes="<<host.gs_image_bytes
       <<" DMA qwords="<<host.dmac.qwords_transferred<<" VIF1 unpacked="<<host.vif1.unpacked_qwords<<" VU1 programs="<<host.vu1.programs<<" XGKICKs="<<host.xgkicks<<" GS prims="<<host.gs.primitives<<" pixels="<<host.gs.pixels<<" TEX2 writes="<<host.gs.tex2_writes<<" frames="<<host.frames_dumped<<" interrupts="<<host.interrupts_delivered<<"\n";
    {
        const auto& d4=host.dmac.channels[4]; const auto& d3=host.dmac.channels[3];
        log<<std::hex<<"IPU ctrl=0x"<<host.ipu.readCtrl()<<" bp=0x"<<host.ipu.readBp()<<" cmd=0x"<<host.ipu.readCmd()<<" queued="<<std::dec<<host.ipu.queuedQwords()
           <<" out="<<host.ipu.output.size()<<std::hex<<" D4 chcr=0x"<<d4.chcr<<" madr=0x"<<d4.madr<<" qwc=0x"<<d4.qwc<<" tadr=0x"<<d4.tadr
           <<" D3 chcr=0x"<<d3.chcr<<" madr=0x"<<d3.madr<<" qwc=0x"<<d3.qwc<<" dstat=0x"<<host.dmac_stat<<std::dec<<'\n';
        for(std::size_t i=1;i<=host.ipu_recent.size();++i)
            if(auto& r=host.ipu_recent[(host.ipu_log+i)%host.ipu_recent.size()]; r.valid && i+8>host.ipu_recent.size()) log<<"recent "<<host.ipuLine(r)<<'\n';
    }
    {
        auto reg=[&](uint32_t a){ auto it=host.gs_registers.find(a); return it==host.gs_registers.end()?uint64_t(0):it->second; };
        log<<std::hex<<"GS PMODE=0x"<<reg(0x12000000u)<<" SMODE2=0x"<<reg(0x12000020u)<<" DISPFB1=0x"<<reg(0x12000070u)<<" DISPLAY1=0x"<<reg(0x12000080u)
           <<" DISPFB2=0x"<<reg(0x12000090u)<<" DISPLAY2=0x"<<reg(0x120000a0u)<<" FRAME_1=0x"<<host.gs.regs[0x4c]<<" FRAME_2=0x"<<host.gs.regs[0x4d]
           <<" ZBUF_1=0x"<<host.gs.regs[0x4e]<<" SCISSOR_1=0x"<<host.gs.regs[0x40]<<" XYOFFSET_1=0x"<<host.gs.regs[0x18]<<" TEST_1=0x"<<host.gs.regs[0x47]<<std::dec<<'\n';
    }
    for(int id=1;id<256;++id) if(const auto* t=host.threads.get(id)) {
        auto it=host.thread_contexts.find(id);
        log<<"EE thread #"<<id<<" prio="<<t->descriptor.priority<<" state="<<int(t->state)<<" wait="<<int(t->wait)<<" id="<<t->wait_id
           <<std::hex<<" entry=0x"<<t->descriptor.entry<<" pc=0x"<<(id==host.threads.current()?cpu.pc:it!=host.thread_contexts.end()?it->second.cpu.pc:0)<<std::dec<<'\n';
    }
    log<<"threads="<<host.threads.size()<<" switches="<<host.context_switches<<" semaphore_waits="<<host.semaphore_waits<<'\n';
    log<<"SIO2 transfers="<<host.sio2.transfers<<" pad0_polls="<<host.sio2.pad_polls[0]<<" pad1_polls="<<host.sio2.pad_polls[1]
       <<" IOP DMA completions="<<host.iop_kernel.dma_completions_delivered<<'\n';
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
    bool main_entry=(host.reason=="missing_function" || host.reason=="breakpoint") && cpu.pc==0x42f7e0;
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
