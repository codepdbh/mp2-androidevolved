#include "ps2_runtime_macros.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "bootstrap_runtime.h"
#include <algorithm>
#include <iostream>
#include <vector>

class MP2BootstrapRuntime : public mp2r::BootstrapRuntime {};
#define PS2Runtime MP2BootstrapRuntime
#include "mp2_00100008_0x100008.cpp"
// Terminal guard for a direct exit tail-call that this test must never reach.
// No success return is supplied for uncompiled game code.
void mp2_00124F70_0x124f70(uint8_t*,R5900Context*,MP2BootstrapRuntime*) {
    throw std::runtime_error("Uncompiled original exit function reached");
}
#undef PS2Runtime

static void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
int run_bootstrap_entry_probe(std::ostream& output) {
    std::vector<uint8_t> ram(PS2_RAM_SIZE,0xcc);
    std::fill(ram.begin()+0x5c0d00,ram.begin()+0x6e4c00,0xa5);
    R5900Context ctx{}; auto* cpu=&ctx; ctx.pc=0x100008;
    MP2BootstrapRuntime host;
    for(unsigned i=0;i<100 && !host.blocked;++i) mp2_00100008_0x100008(ram.data(),cpu,&host);
    check(host.blocked && host.blocked_target==0x1168c8 && host.blocked_source==0x1001e8,"Wrong first unresolved call");
    check(host.syscall_count==2 && host.syscalls[0]==0x3c && host.syscalls[1]==0x3d,"Wrong bootstrap syscall order");
    check(GPR_U32(cpu,29)==0x02000000 && host.thread.stack==0x01fc0000,"Wrong stack setup");
    check(host.heap_base==0x6e4c00 && host.heap_size_request==0xffffffffu,"Wrong heap request");
    check(GPR_U32(cpu,31)==0x1001f0 && ctx.pc==0x1168c8,"Wrong call/return state");
    check(std::all_of(ram.begin()+0x5c0d00,ram.begin()+0x6e4c00,[](auto b){return b==0;}),"BSS mismatch");
    check(std::all_of(ram.begin()+0x6e4c00,ram.end(),[](auto b){return b==0xcc;}),"Memory above BSS changed");
    bool refused=false;
    try { host.handleSyscall(ram.data(),cpu,0x7f); } catch(const std::runtime_error&) { refused=true; }
    check(refused,"Unknown syscall did not stop");
    output<<"PASS: original entry executed through syscalls 0x3c/0x3d; SP=0x2000000 heap_base=0x6e4c00. Explicit stop at 0x1168c8. No game boot.\n";
    return 0;
}
#ifndef MP2R_EMBED_PROBE
int main() { return run_bootstrap_entry_probe(std::cout); }
#endif
