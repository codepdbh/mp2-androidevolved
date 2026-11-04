#include <random>
#include <unwind.h>
#include <dlfcn.h>
#include <sys/statvfs.h>
#include <initializer_list>
#include <set>
#include <condition_variable>
#include <exception>
#include "guest_runtime.h"
#include "android_resolution.h"
#include "guest_memory.h"
#include "guest_heap.h"
#include "runtime_log.h"
#include "frame_pacer.h"
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
#include "d3d8_bridge.h"
#endif
#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <fstream>
#include <iterator>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <time.h>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <pthread.h>
#include <filesystem>
#include <regex>
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <deque>
#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#endif
extern "C" {
#include "recomp_types.h"
#include "pe_format.h"
recomp_func_t mp2_override_lookup(uint32_t va);
extern volatile int mp2_menu_target_pending;
extern volatile float mp2_menu_target_x, mp2_menu_target_y;
extern volatile long long mp2_menu_last_ms;
extern volatile long long mp2_look_last_ms;

uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
#ifndef RECOMP_X87_WINDOW
double g_st[8];
#endif
int g_fp_top;
uint16_t g_fpu_cw = 0x027f;
uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;
uint64_t g_mm[8];
V128 g_xmm[8];
uint32_t g_mxcsr = 0x1f80;
uint32_t g_fs_base, g_gs_base, g_cur_func;
ptrdiff_t g_mem_base;
uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx, g_icall_count;
}

namespace {
std::atomic<uint32_t> displayWidth{1280},displayHeight{720};
std::atomic<uint32_t> renderWidth{0},renderHeight{0};
unsigned graphicsQuality=0;
uint32_t displayModeWidth();
uint32_t displayModeHeight();
std::atomic<unsigned> frameLimit{60};
std::string hex(uint32_t va) {
    std::ostringstream out; out << "0x" << std::hex << std::setw(8) << std::setfill('0') << va;
    return out.str();
}
struct GuestStop : std::runtime_error { using std::runtime_error::runtime_error; };
struct State {
    uint32_t eax{}, ecx{}, edx{}, esp{}, ebx{}, esi{}, edi{}, ebp{}, fs{}, gs{}, cur{};
    uint32_t flags[4]{}, mxcsr{0x1f80};
    uint16_t segments[6]{}, cw{0x027f};
    double st[8]{}; int top{};
    uint64_t mm[8]{}; V128 xmm[8]{};
    void save() {
        eax=g_eax; ecx=g_ecx; edx=g_edx; esp=g_esp; ebx=g_ebx; esi=g_esi; edi=g_edi; ebp=g_ebp;
        fs=g_fs_base; gs=g_gs_base; cur=g_cur_func; mxcsr=g_mxcsr;
        flags[0]=g_flag_k; flags[1]=g_flag_a; flags[2]=g_flag_b; flags[3]=g_flag_cf;
        segments[0]=g_seg_cs; segments[1]=g_seg_ds; segments[2]=g_seg_es;
        segments[3]=g_seg_fs; segments[4]=g_seg_gs; segments[5]=g_seg_ss;
        std::memcpy(st,g_st,sizeof st); top=g_fp_top; cw=g_fpu_cw;
        std::memcpy(mm,g_mm,sizeof mm); std::memcpy(xmm,g_xmm,sizeof xmm);
    }
    void load() const {
        g_eax=eax; g_ecx=ecx; g_edx=edx; g_esp=esp; g_ebx=ebx; g_esi=esi; g_edi=edi; g_ebp=ebp;
        g_fs_base=fs; g_gs_base=gs; g_cur_func=cur; g_mxcsr=mxcsr;
        g_flag_k=flags[0]; g_flag_a=flags[1]; g_flag_b=flags[2]; g_flag_cf=flags[3];
        g_seg_cs=segments[0]; g_seg_ds=segments[1]; g_seg_es=segments[2];
        g_seg_fs=segments[3]; g_seg_gs=segments[4]; g_seg_ss=segments[5];
        std::memcpy(g_st,st,sizeof st); g_fp_top=top; g_fpu_cw=cw;
        std::memcpy(g_mm,mm,sizeof mm); std::memcpy(g_xmm,xmm,sizeof xmm);
    }
};
struct Import { std::string name; recomp_func_t function{}; uint64_t calls{}; };
std::unique_ptr<GuestMemory> memory;
std::unique_ptr<GuestHeaps> heaps;
// The guest machine lock: FIFO by ticket and recursive per thread, with the
// std::recursive_mutex interface the scheduler code already uses.
//
// std::recursive_mutex is not fair. The game thread releases the machine
// around imports and every 500 us, then asks for it again at once and usually
// wins; the guest audio mixer thread waited behind it, filled its DirectSound
// ring too late, and the mixer replayed stale data ("the sound loops"). The
// same starvation made the Windows build stutter until native32 got a ticket
// lock. Here every unlock hands the machine to the longest waiter.
// Estado por hilo invitado. El acceso a thread_local en una biblioteca cargada con dlopen
// pasa por el resolutor de descriptores: era el 4 % de la carga. Solo un hilo tiene la
// máquina a la vez, así que basta un puntero global que se actualiza al adquirirla.
struct GuestPerThread {uint32_t lastErrorValue=0;std::array<uint32_t,1088> tls{};void* running=nullptr;};
thread_local GuestPerThread guestPerThreadStorage;
GuestPerThread* guestCurrent=nullptr;
class MachineMutex {
    std::mutex m_;
    std::condition_variable cv_;
    uint64_t next_=0,serving_=0;
    std::thread::id owner_{};
    unsigned depth_=0;
public:
    void lock(){
        const auto me=std::this_thread::get_id();
        std::unique_lock<std::mutex> l(m_);
        if(owner_==me){++depth_;return;}
        const uint64_t ticket=next_++;
        cv_.wait(l,[&]{return serving_==ticket;});
        owner_=me;depth_=1;acquired=std::chrono::steady_clock::now();guestCurrent=&guestPerThreadStorage;
    }
    // When the current owner got the machine; only the owner reads it.
    std::chrono::steady_clock::time_point acquired{};
    bool try_lock(){
        const auto me=std::this_thread::get_id();
        std::lock_guard<std::mutex> l(m_);
        if(owner_==me){++depth_;return true;}
        if(next_!=serving_)return false;
        ++next_;owner_=me;depth_=1;acquired=std::chrono::steady_clock::now();guestCurrent=&guestPerThreadStorage;return true;
    }
    bool hasWaiters(){
        std::lock_guard<std::mutex> lock(m_);
        return next_>serving_+1; // the current owner already holds one ticket
    }
    void unlock(){
        {
            std::lock_guard<std::mutex> l(m_);
            if(--depth_)return;
            owner_={};++serving_;
            if(next_==serving_)return;          // nobody waiting
        }
        cv_.notify_all();
    }
};
MachineMutex machine;
thread_local std::unique_lock<MachineMutex>* machineLease=nullptr;
thread_local uint32_t guestThreadId=1;
std::vector<Import> imports;
std::map<std::string,uint32_t> modules;
#define lastError (guestCurrent->lastErrorValue)
#define tlsValues (guestCurrent->tls)
uint32_t imageBase{}, imageSize{}, entry{};
uint32_t resourceRva{},resourceSize{};
// Set and read under the machine lock with no yield in between (lookup -> call):
// plain globals, not thread_local, which cost a TLS descriptor call per COM call.
uint32_t lastImport{};
std::string hostGameRoot;
// The app's copy of the WidescreenFix build the guest code was lifted from
// (assets, extracted by GameActivity), for game folders with another build.
std::string bundledWidescreenFix;
std::string guestLanguage="Spanish";
bool guestWidescreen=true;
constexpr const char* guestRootPath="C:\\mp2";
std::string guestExtraFlags;
std::set<std::string> guestHiddenFiles;  // Idioma: el motor carga todos los *.RAS que encuentra.
std::atomic<bool> stopRequested{false};
std::atomic<bool> guestPaused{false};
std::chrono::steady_clock::time_point bootDeadline;
std::string guestThreadFault;
bool timerTestActive=false;std::vector<uint32_t> timerTestCalls;
constexpr uint32_t timerTestAddress=0x7b000010u;
uint32_t read32(uint32_t va);
// Stall diagnostics: where each guest thread last handed the machine over and
// what it was waiting for, dumped when the game stops presenting frames.
struct GuestThreadTrace {
    std::atomic<uint32_t> func{0},handle{0},esp{0};std::atomic<const char*> wait{nullptr};
    std::array<std::atomic<uint32_t>,8> held{};  // critical sections this thread owns (outermost entries)
    void hold(uint32_t cs){for(auto& slot:held){uint32_t empty=0;if(slot.compare_exchange_strong(empty,cs))return;}}
    void release(uint32_t cs){for(auto& slot:held){uint32_t value=cs;if(slot.compare_exchange_strong(value,0))return;}}
};
std::mutex threadTracesMutex;
std::map<uint32_t,std::shared_ptr<GuestThreadTrace>> threadTraces;
thread_local std::shared_ptr<GuestThreadTrace> threadTrace;
GuestThreadTrace& currentTrace(){
    if(!threadTrace){threadTrace=std::make_shared<GuestThreadTrace>();std::lock_guard<std::mutex> lock(threadTracesMutex);threadTraces[guestThreadId]=threadTrace;}
    return *threadTrace;
}
void forgetThreadTrace(){std::lock_guard<std::mutex> lock(threadTracesMutex);threadTraces.erase(guestThreadId);threadTrace.reset();}
struct WaitScope {
    WaitScope(const char* what,uint32_t handle){auto& trace=currentTrace();trace.wait=what;trace.handle=handle;}
    ~WaitScope(){currentTrace().wait=nullptr;}
};
std::atomic<int64_t> lastPresentMs{0};
int64_t steadyMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void startStallWatch(){
    static std::once_flag started;
    std::call_once(started,[]{std::thread([]{
        int64_t reportedFor=0;
        while(!stopRequested.load()){
            std::this_thread::sleep_for(std::chrono::seconds(2));
            int64_t last=lastPresentMs.load(),idle=steadyMs()-last;
            if(guestPaused.load()||idle<12000||reportedFor==last)continue;
            reportedFor=last;
            NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSC","Guest stall: no frame for %lld s; guest threads:",static_cast<long long>(idle/1000));
            std::lock_guard<std::mutex> lock(threadTracesMutex);
            for(auto& [id,trace]:threadTraces){
                const char* wait=trace->wait.load();uint32_t handle=trace->handle.load();
                std::string detail,held,callers;
                // Read-only peeks at guest memory from this host thread: stable while the guest is stuck.
                if(wait&&!std::strcmp(wait,"EnterCriticalSection")&&memory&&memory->readable(handle,24))
                    detail=" (owner thread "+std::to_string(read32(handle+12))+", count "+std::to_string(read32(handle+8))+")";
                for(auto& slot:trace->held)if(uint32_t cs=slot.load())held+=" "+hex(cs);
                uint32_t esp=trace->esp.load();
                for(unsigned i=0,found=0;memory&&i<256&&found<10;++i){
                    if(!memory->readable(esp+i*4,4))break;
                    uint32_t value=read32(esp+i*4);
                    if(value>=0x401000u&&value<0x88F2A5u&&recomp_lookup(value)==nullptr){callers+=" "+hex(value);++found;}
                }
                NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSC","  thread %u at %08x %s %08x%s; holds:%s; stack:%s",id,trace->func.load(),
                    wait?wait:"running",wait?handle:0u,detail.c_str(),held.empty()?" none":held.c_str(),callers.c_str());
            }
        }
    }).detach();});
}
void checkGuestThreadCancellation();
void yieldGuestMachine(unsigned milliseconds=0,std::chrono::steady_clock::time_point deadline={}) {
    if(!machineLease||!machineLease->owns_lock())throw GuestStop("Guest scheduler ownership missing");
    {auto& trace=currentTrace();trace.func=g_cur_func;trace.esp=g_esp;}
    State saved;saved.save();machineLease->unlock();
    if(deadline!=std::chrono::steady_clock::time_point{})std::this_thread::sleep_until(deadline);
    else if(milliseconds)std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));else std::this_thread::yield();
    machineLease->lock();saved.load();checkGuestThreadCancellation();
}
void pollGuestNetwork();
thread_local bool renderThread=false;
void checkGuestProgress() {
    checkGuestThreadCancellation();
    while(guestPaused.load()&&!stopRequested.load())yieldGuestMachine(20);
    if(stopRequested.load())throw GuestStop("Guest execution cancelled");
    // Imports and lifted back edges can reach this hook many times per frame.
    // Keep cancellation immediate, but amortize full register saves and native
    // scheduler handovers over a short time slice rather than every call.
    // One time slice for whichever guest thread holds the machine.
    //
    // Slices count from when this thread acquired the machine (a shared
    // deadline left the incoming thread an expired slice, so it handed the
    // machine straight back). The render thread, the one presenting, gets
    // 3 ms and the others 500 us: every handover wakes the queued threads in
    // turn, and with 500 us slices the Underground 2 render thread spent 56%
    // of its wall time waiting to get the machine back (Redmi Note 8).
    static unsigned checkpoints=0;
    if((++checkpoints&63u)==0){
        auto now=std::chrono::steady_clock::now();
        if(now>bootDeadline)throw GuestStop("CRT bring-up time budget reached at "+hex(g_cur_func));
        auto slice=renderThread?std::chrono::microseconds(3000):std::chrono::microseconds(500);
        if(now-machine.acquired>=slice){pollGuestNetwork();if(machine.hasWaiters())yieldGuestMachine();}
    }
    if(!guestThreadFault.empty())throw GuestStop(guestThreadFault);
}
constexpr uint32_t importBase = 0x7f000000u;
constexpr uint32_t stackBase = 0x20000000u, stackSize = 16u << 20, tib = 0x21000000u;

void* ptr(uint32_t va, uint64_t bytes) { return memory->address(va, bytes); }
uint32_t read32(uint32_t va) { uint32_t v; std::memcpy(&v, ptr(va,4),4); return v; }
void write32(uint32_t va, uint32_t v) { std::memcpy(ptr(va,4),&v,4); }
uint32_t arg(uint32_t index) { return read32(g_esp + 4 + index*4); }
void ret(uint32_t value, uint32_t argc) { g_eax=value; g_esp+=4+argc*4; }
std::string guestString(uint32_t address) {
    if(!address) return {};
    std::string s;
    for(uint32_t i=0;i<32768;++i) {
        char c=*static_cast<char*>(ptr(address+i,1));
        if(!c) return s;
        s.push_back(c);
    }
    throw GuestStop("Unterminated guest string at "+hex(address));
}
std::string lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));}); return s;
}
uint32_t allocate(uint32_t bytes) {
    uint32_t result=heaps->alloc(GuestHeaps::process,8,bytes);
    if(!result) throw GuestStop("Guest allocator exhausted");
    return result;
}
void unsupported() {
    uint32_t index = (lastImport-importBase)/16;
    throw GuestStop("HLE pendiente: " + imports.at(index).name + " desde " + hex(g_cur_func));
}
void getVersion() {
    uint32_t out = arg(0), bytes=read32(out);
    if (bytes != 148 && bytes != 156) { ret(0,1); return; }
    std::memset(ptr(out+4,bytes-4),0,bytes-4);
    write32(out+4,5); write32(out+8,1); write32(out+12,2600); write32(out+16,2);
    if (bytes==156) { auto* p=static_cast<unsigned char*>(ptr(out+148,8)); p[6]=1; }
    ret(1,1);
}
// guest_modules.inc (game DLLs run as lifted code) needs callGuest, so it comes later.
uint32_t loadGuestModule(const std::string& name);
bool freeGuestModule(uint32_t handle);
uint32_t guestModuleHandle(const std::string& name);
std::string guestModuleFilePath(uint32_t handle);
bool guestModuleExport(uint32_t handle,const std::string& name,uint32_t& va);
bool isGuestModule(uint32_t handle);
void moduleHandle() {
    if(!arg(0)) { ret(imageBase,1); return; }
    auto name=lower(guestString(arg(0)));
    auto separator=name.find_last_of("/\\"); if(separator!=std::string::npos) name=name.substr(separator+1);
    if(name=="nfsc.exe") { ret(imageBase,1); return; }
    if(name.find('.')==std::string::npos) name+=".dll";
    if(uint32_t loaded=guestModuleHandle(name)) { ret(loaded,1); return; }
    auto module=modules.find(name);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Module lookup %s",name.c_str());
    if(module==modules.end()) lastError=126;
    ret(module==modules.end()?0:module->second,1);
}
void heapCreate() {
    ret(heaps->create(arg(0),arg(1),arg(2)),3);
}
void heapAlloc() { ret(heaps->alloc(arg(0),arg(1),arg(2)),3); }
void heapFree() { ret(heaps->free(arg(0),arg(2)),3); }
void heapRealloc() { ret(heaps->realloc(arg(0),arg(1),arg(2),arg(3)),4); }
void heapSize() { ret(heaps->size(arg(0),arg(2)),3); }
void heapDestroy() { ret(heaps->destroy(arg(0)),1); }
void getProcessHeap() { ret(GuestHeaps::process,0); }
void getProcAddress();
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
uint32_t d3dToken{};
void direct3DCreate(){ret(createGuestD3D8(arg(0)),1);}
void freeD3DGuest(uint32_t address){heaps->free(GuestHeaps::process,address);}
void serviceGuestSound();
constexpr uint32_t d3d8PresentToken=0x7e0020f0u;  // Device, método 15
void d3dDispatch(){
    // The arguments are read in place on the guest stack (one bounds check)
    // instead of copying 128 bytes for each D3D9 call.
    const uint32_t* args=static_cast<const uint32_t*>(ptr(g_esp+4,128));uint32_t count=0;uint32_t token=d3dToken;
    static uint64_t bridgeCalls=0;static std::chrono::nanoseconds bridgeTime{},presentTime{};
    // Time one call in 64 (and every Present) so the statistic does not cost
    // two clock reads on each of the ~40k calls in a race frame.
    bool timed=(++bridgeCalls&63u)==0||token==d3d8PresentToken;
    std::chrono::steady_clock::time_point bridgeStart;if(timed)bridgeStart=std::chrono::steady_clock::now();uint32_t result;
    try{result=dispatchGuestD3D8(token,args,&count);}catch(...){
        NFS_RUNTIME_LOG(ANDROID_LOG_ERROR,"NFSC","D3D8 guest caller=%08x stack=%08x token=%08x self=%08x",g_cur_func,g_esp,token,args[0]);throw;}
    if(timed){auto spent=std::chrono::steady_clock::now()-bridgeStart;bridgeTime+=token==d3d8PresentToken?spent:spent*64;presentTime+=token==d3d8PresentToken?spent:std::chrono::nanoseconds{};}
    if(token==d3d8PresentToken){lastPresentMs=steadyMs();startStallWatch();}
    if(token==d3d8PresentToken&&result==0){
        static FramePacer pacer;
        auto now=std::chrono::steady_clock::now();
        auto deadline=pacer.deadline(now,frameLimit.load(std::memory_order_relaxed));
        // The renderer has returned; guest audio/streaming workers can use the
        // machine while this thread waits. Late frames incur no extra delay.
        renderThread=true;
        if(deadline>now)yieldGuestMachine(0,deadline);
        else if(machine.hasWaiters())yieldGuestMachine();
        static unsigned frames=0;static auto since=std::chrono::steady_clock::now();
        if(++frames==120){serviceGuestSound();auto now=std::chrono::steady_clock::now();double seconds=std::chrono::duration<double>(now-since).count();
            NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest D3D8 presentation %.1f FPS over %.2f seconds; bridge ~%.2f ms/frame (present %.2f), %.0f calls/frame",
                frames/seconds,seconds,double(bridgeTime.count())/1e6/frames,double(presentTime.count())/1e6/frames,double(bridgeCalls)/frames);
            frames=0;since=now;bridgeTime=presentTime=std::chrono::nanoseconds{};bridgeCalls=0;}
    }ret(result,count);}
#endif
// threadStart: a thread procedure may be stdcall or cdecl; Windows exits the thread right after either.
uint32_t callGuest(uint32_t va,const uint32_t* args,size_t count,uint32_t cleanup,bool threadStart);
// Braced argument lists stay on the host stack: callbacks run many times a
// frame and a std::vector per call cost a malloc/free pair.
inline uint32_t callGuest(uint32_t va,std::initializer_list<uint32_t> args,uint32_t cleanup=0,bool threadStart=false){return callGuest(va,args.begin(),args.size(),cleanup,threadStart);}
inline uint32_t callGuest(uint32_t va,const std::vector<uint32_t>& args,uint32_t cleanup=0,bool threadStart=false){return callGuest(va,args.data(),args.size(),cleanup,threadStart);}
#include "guest_seh.inc"
#include "guest_threads.inc"
#include "guest_files.inc"
#include "guest_kernel.inc"
#include "guest_input.inc"
#include "guest_audio.inc"
#include "guest_network.inc"
#include "guest_msvcrt.inc"
#include "guest_carbon.inc"
#include "guest_nfsco.inc"
#include "guest_mp2.inc"
void testGuestScheduling(){
    State original;original.save();uint32_t savedError=lastError,savedTLS=tlsValues[1087];
    g_eax=0x11223344;g_fs_base=0x21000000;g_st[0]=123.5;g_xmm[7].u64[0]=0x123456789abcdef0ull;
    lastError=111;tlsValues[1087]=0x55;
    bool done=false;std::string failure;
    std::thread worker([&]{
        std::unique_lock<MachineMutex> lease(machine);machineLease=&lease;guestThreadId=9000;
        State fresh;fresh.eax=0xaabbccdd;fresh.fs=0xdead1000;fresh.st[0]=-456.25;fresh.xmm[7].u64[0]=0xfedcba9876543210ull;fresh.load();
        lastError=222;tlsValues[1087]=0xaa;
        try{for(unsigned i=0;i<100;++i){checkGuestProgress();yieldGuestMachine();
            if(g_eax!=0xaabbccdd||g_fs_base!=0xdead1000||g_st[0]!=-456.25||g_xmm[7].u64[0]!=0xfedcba9876543210ull||lastError!=222||tlsValues[1087]!=0xaa)
                throw GuestStop("Guest worker context was not isolated");}}
        catch(const std::exception& error){failure=error.what();}
        forgetThreadTrace();done=true;machineLease=nullptr;
    });
    try{while(!done){checkGuestProgress();yieldGuestMachine(0,std::chrono::steady_clock::now()+std::chrono::milliseconds(1));
        if(g_eax!=0x11223344||g_fs_base!=0x21000000||g_st[0]!=123.5||g_xmm[7].u64[0]!=0x123456789abcdef0ull||lastError!=111||tlsValues[1087]!=0x55)
            failure="Main guest context was not preserved during handover";
    }}catch(const std::exception& error){failure=error.what();}
    machineLease->unlock();worker.join();machineLease->lock();original.load();lastError=savedError;tlsValues[1087]=savedTLS;
    if(!failure.empty())throw GuestStop(failure);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest scheduler passed 100 handovers with separate integer, FS, x87, XMM, TLS and last-error state");
}
void testGuestSoundCaps(){
    State saved;saved.save();
    uint32_t output=allocate(96),device=createSoundObject(1);
    write32(output,96);
    uint32_t result=callGuest(soundMethodBase+0x1000+4*16,{device,output});
    bool ok=result==0&&read32(output)==96&&read32(output+4)==0xf1f&&read32(output+8)==100&&
        read32(output+12)==200000&&read32(output+16)==1&&read32(output+20)==0;
    callGuest(soundMethodBase+0x1000+2*16,{device});heaps->free(GuestHeaps::process,output);saved.load();
    if(!ok)throw GuestStop("DirectSound PCM capabilities regression");
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","DirectSound caps passed: mono/stereo, 8/16-bit, continuous rates, software buffers and guest ABI");
}
recomp_func_t resolveFunction(const std::string& dll, const std::string& name) {
    if(auto found=resolveMp2Function(dll,name))return found;
    if(auto found=carbonFunctions.find(dll+"!"+name);found!=carbonFunctions.end())return found->second;
    if(dll=="kernel32.dll"||dll=="user32.dll"||dll=="shell32.dll"||dll=="ole32.dll"||dll=="gdi32.dll"||dll=="imm32.dll"||dll=="dwmapi.dll"||dll=="ws2_32.dll")if(auto found=nfscoFunctions.find(name);found!=nfscoFunctions.end())return found->second;
    if((dll=="shfolder.dll"||dll=="shell32.dll")&&name=="SHGetFolderPathA")return shellFolderPath;
    if(dll=="gdi32.dll"&&name=="DeleteObject")return deleteGdiObject;
    if(dll=="ws2_32.dll"||dll=="wsock32.dll"){auto found=networkFunctions.find(name);if(found!=networkFunctions.end())return found->second;}
    if(dll=="msvcrt.dll"){auto found=msvcrtFunctions.find(name);if(found!=msvcrtFunctions.end())return found->second;}
    if(dll=="netapi32.dll"&&name=="Netbios")return netbios;
    if(dll=="tapi32.dll"&&name=="lineInitialize")return lineInitialize;
    if(dll=="tapi32.dll"&&name=="lineShutdown")return lineShutdown;
    if(dll=="user32.dll") {auto found=windowFunctions.find(name);if(found!=windowFunctions.end())return found->second;
        auto extra=windowModuleFunctions.find(name);if(extra!=windowModuleFunctions.end())return extra->second;}
    if(dll=="winmm.dll") {auto found=timerFunctions.find(name);if(found!=timerFunctions.end())return found->second;}
    if (dll == "kernel32.dll") {
        auto resource=resourceFunctions.find(name);if(resource!=resourceFunctions.end())return resource->second;
        auto file=fileFunctions.find(name);if(file!=fileFunctions.end())return file->second;
        auto kernel=kernelFunctions.find(name); if(kernel!=kernelFunctions.end()) return kernel->second;
        auto moduleOnly=kernelModuleFunctions.find(name); if(moduleOnly!=kernelModuleFunctions.end()) return moduleOnly->second;
        if (name=="GetVersionExA") return getVersion;
        if (name=="GetModuleHandleA") return moduleHandle;
        if (name=="HeapCreate") return heapCreate;
        if (name=="HeapAlloc") return heapAlloc;
        if (name=="HeapFree") return heapFree;
        if (name=="HeapReAlloc") return heapRealloc;
        if (name=="HeapSize") return heapSize;
        if (name=="HeapDestroy") return heapDestroy;
        if (name=="GetProcessHeap") return getProcessHeap;
    }
    return unsupported;
}
void getProcAddress() {
    uint32_t module=arg(0),namePointer=arg(1);
    auto found=std::find_if(modules.begin(),modules.end(),[&](auto& m){return m.second==module;});
    auto name=namePointer<=0xffff?"#"+std::to_string(namePointer):guestString(namePointer);
    if(uint32_t va;found==modules.end()&&guestModuleExport(module,name,va)) { ret(va,2); return; }
    if(found==modules.end()) { lastError=isGuestModule(module)?127:126; ret(0,2); return; }
    auto full=found->first+"!"+name;
    for(uint32_t i=0;i<imports.size();++i) if(imports[i].name==full) { ret(importBase+i*16,2); return; }
    // Only advertise exports that are actually implemented, not permissive stubs.
    auto fn=resolveFunction(found->first,name);
    if(fn==unsupported) { lastError=127; ret(0,2); return; }
    uint32_t va=importBase+uint32_t(imports.size())*16;
    imports.push_back({full,fn}); ret(va,2);
}

template<class T> T fileStruct(const std::vector<unsigned char>& file, uint64_t offset) {
    if (offset > file.size() || sizeof(T) > file.size()-offset) throw GuestStop("Truncated PE structure");
    T value{}; std::memcpy(&value,file.data()+offset,sizeof value); return value;
}
void moduleRange(uint32_t size, uint32_t rva, uint64_t bytes) {
    if (rva > size || bytes > uint64_t(size)-rva) throw GuestStop("PE image range invalid");
}
std::string moduleString(uint32_t base, uint32_t size, uint32_t rva) {
    moduleRange(size,rva,1); std::string value;
    for (uint32_t i=rva; i<size && value.size()<8192; ++i) {
        char c = *static_cast<char*>(ptr(base+i,1)); if (!c) return value; value.push_back(c);
    }
    throw GuestStop("Unterminated PE import name");
}
void imageRange(uint32_t rva, uint64_t bytes) { moduleRange(imageSize,rva,bytes); }
struct DeferredModuleImport{std::string dll,name;uint32_t slot;};
std::vector<DeferredModuleImport> deferredModuleImports;
bool isLiftedModuleName(const std::string& dll);
uint32_t loadGuestModule(const std::string& name);
void bindDeferredModuleImports(size_t first);
bool guestModuleExport(uint32_t handle,const std::string& name,uint32_t& va);
// Writes an import token into every IAT slot of a mapped image (NFSC.exe or a game DLL).
void bindImports(uint32_t base, uint32_t size, uint32_t directoryRva, uint32_t directorySize) {
    moduleRange(size,directoryRva,directorySize);
    // Like the Windows loader, read descriptors up to the null one: NFSC.exe's
    // directory size (300) counts its 15 descriptors without the terminator.
    bool terminated=false;
    for (uint32_t offset=0; offset<=directorySize+4096u; offset+=sizeof(pe_import_descriptor)) {
        moduleRange(size,directoryRva+offset,sizeof(pe_import_descriptor));
        pe_import_descriptor descriptor{};
        std::memcpy(&descriptor,ptr(base+directoryRva+offset,sizeof descriptor),sizeof descriptor);
        if (!descriptor.Name && !descriptor.FirstThunk) { terminated=true; break; }
        auto dll=moduleString(base,size,descriptor.Name);
        std::transform(dll.begin(),dll.end(),dll.begin(),[](unsigned char c){return char(std::tolower(c));});
        // d3dx9_30's msvcrt.dll imports go to the lifted msvcr71.dll Carbon
        // ships: one CRT (heap, atexit, exception frames) for the whole guest.
        if(dll=="msvcrt.dll"&&isLiftedModuleName("msvcr71.dll"))dll="msvcr71.dll";
        if(isLiftedModuleName(dll)){
            // A DLL that runs as lifted code (d3dx9_26.dll): its slots get the
            // export VAs once it is mapped and its DllMain ran (bindDeferredModuleImports).
            uint32_t names=descriptor.OriginalFirstThunk ? descriptor.OriginalFirstThunk : descriptor.FirstThunk;
            for(uint32_t i=0;;++i){
                uint32_t nameRva=names+i*4,iatRva=descriptor.FirstThunk+i*4;
                moduleRange(size,nameRva,4);moduleRange(size,iatRva,4);
                uint32_t thunk=read32(base+nameRva);if(!thunk)break;
                std::string name=(thunk&PE_ORDINAL_FLAG)?"#"+std::to_string(thunk&0xffff):moduleString(base,size,thunk+2);
                deferredModuleImports.push_back({dll,name,base+iatRva});write32(base+iatRva,0);
            }
            continue;
        }
        if(!modules.count(dll)) modules[dll]=0x71000000u+uint32_t(modules.size())*0x10000u;
        uint32_t names=descriptor.OriginalFirstThunk ? descriptor.OriginalFirstThunk : descriptor.FirstThunk;
        for (uint32_t i=0;;++i) {
            uint64_t nameRva=uint64_t(names)+i*4ull, iatRva=uint64_t(descriptor.FirstThunk)+i*4ull;
            if (nameRva>UINT32_MAX || iatRva>UINT32_MAX) throw GuestStop("PE thunk overflow");
            moduleRange(size,uint32_t(nameRva),4); moduleRange(size,uint32_t(iatRva),4);
            uint32_t thunk=read32(base+uint32_t(nameRva)); if (!thunk) break;
            std::string name;
            if (thunk&PE_ORDINAL_FLAG) name="#"+std::to_string(thunk&0xffff);
            else { moduleRange(size,thunk,3); name=moduleString(base,size,thunk+2); }
            if (imports.size()>=4096) throw GuestStop("Too many PE imports");
            if(dll=="msvcrt.dll")if(uint32_t cell=msvcrtData(name)){write32(base+uint32_t(iatRva),cell);continue;}  // data export
            uint32_t va=importBase+uint32_t(imports.size())*16;
            imports.push_back({dll+"!"+name,resolveFunction(dll,name)});
            write32(base+uint32_t(iatRva),va);
        }
    }
    if (!terminated) throw GuestStop("Unterminated PE import descriptors");
}
void loadImage(const char* path) {
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw GuestStop("Cannot open game PE");
    std::vector<unsigned char> file((std::istreambuf_iterator<char>(stream)),{});
    auto dos=fileStruct<pe_dos_header>(file,0);
    if (dos.e_magic!=0x5a4d || dos.e_lfanew<0) throw GuestStop("Invalid DOS header");
    auto nt=fileStruct<pe_nt_headers32>(file,dos.e_lfanew);
    if (nt.Signature!=0x4550 || nt.FileHeader.Machine!=0x14c || nt.OptionalHeader.Magic!=0x10b ||
        nt.FileHeader.SizeOfOptionalHeader<sizeof(pe_opt_header32)) throw GuestStop("Expected x86 PE32");
    auto opt=nt.OptionalHeader; imageBase=opt.ImageBase; imageSize=opt.SizeOfImage;
    resourceRva=opt.DataDirectory[2].VirtualAddress;resourceSize=opt.DataDirectory[2].Size;
    if (imageBase!=0x400000 || imageSize!=0x16da50 || opt.AddressOfEntryPoint!=0xd34f0)  // config/pc/modules.json
        throw GuestStop("MaxPayne2.exe no coincide con el código recompilado");
    // El cargador mapea páginas enteras: SizeOfImage no está alineado.
    imageSize=(imageSize+0xfffu)&~0xfffu;
    memory->commit(imageBase,imageSize);
    imageRange(0,opt.SizeOfHeaders);
    if (opt.SizeOfHeaders>file.size()) throw GuestStop("Truncated PE headers");
    std::memcpy(ptr(imageBase,opt.SizeOfHeaders),file.data(),opt.SizeOfHeaders);
    uint64_t sections=uint64_t(dos.e_lfanew)+24+nt.FileHeader.SizeOfOptionalHeader;
    for (uint32_t i=0;i<nt.FileHeader.NumberOfSections;++i) {
        auto sec=fileStruct<pe_section_header>(file,sections+i*sizeof(pe_section_header));
        imageRange(sec.VirtualAddress,std::max(sec.VirtualSize,sec.SizeOfRawData));
        if (uint64_t(sec.PointerToRawData)+sec.SizeOfRawData>file.size()) throw GuestStop("Truncated PE section");
        std::memcpy(ptr(imageBase+sec.VirtualAddress,sec.SizeOfRawData),file.data()+sec.PointerToRawData,sec.SizeOfRawData);
    }
    entry=imageBase+opt.AddressOfEntryPoint;
    // Guest VAs stay fixed; the 64-bit host base is ADDR's offset. No PE
    // relocation toward the host address and no executable x86 mapping.
    auto directory=opt.DataDirectory[PE_DIR_IMPORT];
    bindImports(imageBase,imageSize,directory.VirtualAddress,directory.Size);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest PE mapped: base=%08x size=%08x entry=%08x; %zu imports bound; %u lifted functions",
        imageBase,imageSize,entry,imports.size(),recomp_dispatch_count);
}
uint32_t callGuest(uint32_t va, const uint32_t* args, size_t count, uint32_t cleanup, bool threadStart) {
    State saved; saved.save();
    try {
        recomp_func_t fn=recomp_lookup_manual(va);if(!fn)fn=recomp_lookup(va);
        if (!fn) fn=recomp_lookup_import(va);
        if (!fn) throw GuestStop("No guest function at " + hex(va));
        for (size_t i=count;i--;) { g_esp-=4; write32(g_esp,args[i]); }
        g_esp-=4; write32(g_esp,RECOMP_RETADDR);
        fn(); g_esp+=cleanup;
        if (g_esp!=saved.esp&&!(threadStart&&g_esp+4u*uint32_t(count)==saved.esp)) throw GuestStop("Guest call stack imbalance at " + hex(va));
        uint32_t result=g_eax; saved.load(); return result;
    } catch (...) { saved.load(); throw; }
}
#include "guest_modules.inc"
void timerTestCallback(){timerTestCalls.push_back(arg(0));ret(0,5);}
void testGuestTimers(){
    auto imported=[&](const std::string& name){for(uint32_t i=0;i<imports.size();++i)if(imports[i].name==name)return importBase+i*16;return 0u;};
    uint32_t set=imported("winmm.dll!timeSetEvent"),kill=imported("winmm.dll!timeKillEvent");
    if(!set||!kill){NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest timer self-check skipped: the game imports no timeSetEvent/timeKillEvent");return;}
    auto wait=[&](unsigned count){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(timerTestCalls.size()<count){if(std::chrono::steady_clock::now()>until)throw GuestStop("Timer callback deadline failed");checkGuestProgress();yieldGuestMachine(1);}};
    timerTestActive=true;timerTestCalls.clear();
    try{
        auto first=callGuest(set,{2,0,timerTestAddress,0,0x100});if(!first)throw GuestStop("Timer creation failed");wait(1);
        if(timerTestCalls[0]!=first||timerHandles.count(first))throw GuestStop("One-shot timer retirement failed");
        auto worker=timerWorker->id;
        auto periodic=callGuest(set,{2,0,timerTestAddress,0,0x101});wait(3);
        if(callGuest(kill,{periodic})||timerHandles.count(periodic))throw GuestStop("Periodic timer cancellation failed");
        size_t after=timerTestCalls.size();auto cancelled=callGuest(set,{20,0,timerTestAddress,0,0x100});
        if(callGuest(kill,{cancelled}))throw GuestStop("Pending timer cancellation failed");
        auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(25);while(std::chrono::steady_clock::now()<until){checkGuestProgress();yieldGuestMachine(1);}
        if(timerTestCalls.size()!=after||!timerHandles.empty()||timerWorker->id!=worker||guestThreads.size()!=1)throw GuestStop("Timer cancellation/worker reuse failed");
        timerTestActive=false;
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Timer worker passed: one-shot, periodic, synchronous cancel, pending cancel, retirement and single-thread reuse");
    }catch(...){timerTestActive=false;for(auto& timer:timerHandles)timer.second->cancelled=true;timerHandles.clear();throw;}
}
void testGuestFileTime(){
    auto imported=[&](const std::string& name){for(uint32_t i=0;i<imports.size();++i)if(imports[i].name==name)return importBase+i*16;return 0u;};
    uint32_t local=imported("kernel32.dll!FileTimeToLocalFileTime"),system=imported("kernel32.dll!FileTimeToSystemTime");
    if(!local||!system){  // the self-check drives the game's own imports; Speed.exe has neither
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest FILETIME self-check skipped: the game imports no FILETIME conversion");
        return;
    }
    uint32_t data=allocate(48);uint64_t epoch=116444736000000000ull+1234ull*10000ull;
    std::memset(ptr(data,48),0xa5,48);std::memcpy(ptr(data,8),&epoch,8);
    bool okay=callGuest(local,{data,data+8})==1&&read32(data+8)==uint32_t(epoch)&&read32(data+12)==uint32_t(epoch>>32)
        &&callGuest(system,{data+8,data+16})==1;
    const uint16_t expected[]={1970,1,4,1,0,0,1,234};
    okay=okay&&!std::memcmp(ptr(data+16,16),expected,16)&&read32(data+32)==0xa5a5a5a5u;
    okay=okay&&callGuest(local,{data,data})==0;
    uint64_t invalid=0x8000000000000000ull;std::memcpy(ptr(data,8),&invalid,8);
    okay=okay&&callGuest(system,{data,data+16})==0;
    heaps->free(GuestHeaps::process,data);
    if(!okay)throw GuestStop("Guest file-time conversion self-check failed");
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest UTC FILETIME conversions passed: epoch, milliseconds, guards and invalid input");
}
void testGuestCriticalSections(){
    auto imported=[&](const std::string& name){for(uint32_t i=0;i<imports.size();++i)if(imports[i].name=="kernel32.dll!"+name)return importBase+i*16;return 0u;};
    uint32_t initialize=imported("InitializeCriticalSection"),enter=imported("EnterCriticalSection"),leave=imported("LeaveCriticalSection"),remove=imported("DeleteCriticalSection");
    if(!initialize||!enter||!leave||!remove){NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest critical-section self-check skipped: imports missing");return;}
    uint32_t data=allocate(32);std::memset(ptr(data,32),0xa5,32);
    callGuest(initialize,{data});callGuest(enter,{data});callGuest(enter,{data});
    bool okay=read32(data+8)==2&&read32(data+12)==guestThreadId&&read32(data+24)==0xa5a5a5a5u;
    callGuest(remove,{data});okay=okay&&criticalSections.count(data)&&pendingCriticalDeletes.count(data);
    callGuest(leave,{data});okay=okay&&read32(data+8)==1&&criticalSections.count(data);
    callGuest(leave,{data});okay=okay&&!criticalSections.count(data)&&!pendingCriticalDeletes.count(data)&&read32(data+8)==0;
    callGuest(initialize,{data});callGuest(remove,{data});okay=okay&&!criticalSections.count(data);
    heaps->free(GuestHeaps::process,data);
    if(!okay)throw GuestStop("Guest critical-section retirement self-check failed");
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest critical sections passed recursion, owner, guards, deferred retirement and reinitialization");
}
} // namespace

namespace {
// Every indirect call and virtual method resolves its target here, so use an
// open-addressing table (built once, read-only afterwards) instead of a binary
// search over ~70k entries with a cache miss per step.
struct DispatchHash {
    static constexpr uint32_t slots=1u<<18;
    std::unique_ptr<uint32_t[]> keys{new uint32_t[slots]()};
    std::unique_ptr<recomp_func_t[]> funcs{new recomp_func_t[slots]()};
    static uint32_t slot(uint32_t va){return (va*0x9e3779b1u)>>14;}
    void add(const recomp_dispatch_entry_t* table,uint32_t count){
        for(uint32_t i=0;i<count;++i){
            uint32_t va=table[i].address,s=slot(va);
            if(va-0x7c000000u<0x04000000u)throw GuestStop("Lifted function overlaps reserved native dispatch stubs");
            while(keys[s])s=(s+1)&(slots-1);
            keys[s]=va;funcs[s]=table[i].func;
        }
    }
    DispatchHash(){
        static_assert(slots>2*90000,"dispatch hash load factor");  // tabla única de los 25 módulos
        add(recomp_dispatch_table,recomp_dispatch_count);
        uint32_t used=0;for(uint32_t i=0;i<slots;++i)used+=keys[i]!=0;
        if(used>slots/2)throw GuestStop("Dispatch hash over half full: "+std::to_string(used));
    }
    recomp_func_t find(uint32_t va)const{
        if(!va)return nullptr;
        for(uint32_t s=slot(va);keys[s];s=(s+1)&(slots-1))if(keys[s]==va)return funcs[s];
        return nullptr;
    }
};
}
extern "C" recomp_func_t recomp_lookup(uint32_t va) {
    // Sound/input/D3D COM and import stubs resolve in recomp_lookup_import.
    // Avoid an always-failing hash probe on every native method call. The
    // table constructor checks that no lifted module occupies this range.
    if(va-0x7c000000u<0x04000000u)return nullptr;
    static const DispatchHash hash;
    return hash.find(va);
}
namespace {
void testGuestPow(){
    State saved;saved.save();
#ifdef RECOMP_X87_WINDOW
    const int savedFpSp=g_fp_sp;
#endif
    try{
        struct Sample{double base,exponent,result;};
        for(auto sample:{Sample{4,.5,2},Sample{2,3,8},Sample{.5,2,.25},Sample{-2,3,-8},Sample{0,0,1}}){
            g_fp_top=0;fp_push_impl(g_st,&g_fp_top,123.25);fp_push_impl(g_st,&g_fp_top,sample.base);fp_push_impl(g_st,&g_fp_top,sample.exponent);
            uint32_t stack=g_esp;g_esp-=4;write32(g_esp,RECOMP_RETADDR);mp2_override_lookup(0x0060406fu)();
            if(g_esp!=stack||g_fp_top!=2||!std::isfinite(g_st[0])||std::abs(g_st[0]-sample.result)>1e-12||g_st[1]!=123.25)
                throw GuestStop("_CIpow result/stack regression");
        }
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","_CIpow passed powers, roots, fractions, negative base and guest stack preservation");
    }catch(...){
#ifdef RECOMP_X87_WINDOW
        g_fp_sp=savedFpSp;
#endif
        saved.load();throw;
    }
#ifdef RECOMP_X87_WINDOW
    g_fp_sp=savedFpSp;
#endif
    saved.load();
}
}
namespace {
// Every RECOMP_ICALL/ITAIL resolves through recomp_lookup_manual first: vtable
// calls of the game, D3DX and the CRT were ~12% of the Redmi Note 8 main
// thread in hash probes. Code outside the heaps never moves, so its function
// is remembered (overrides first, as before). Heap code (sound banks), imports
// and COM stubs stay uncached: recomp_lookup_import records which method was
// called. Guest threads hold the machine while resolving.
struct LookupEntry{uint32_t va;recomp_func_t fn;};
LookupEntry lookupCache[8192];
inline LookupEntry& lookupSlot(uint32_t va){return lookupCache[((va>>2)^(va>>15))&8191u];}
__attribute__((noinline)) recomp_func_t lookupManualSlow(uint32_t va){
    if(crtSortTestActive&&va==crtSortTestAddress)return crtQsort;
    if(crtSortTestActive&&va==crtCompareTestAddress)return crtCompareTest;
    if(timerTestActive&&va==timerTestAddress)return timerTestCallback;
    if(va-0x7c000000u<0x04000000u)return nullptr;
    recomp_func_t fn=mp2_override_lookup(va);
    if(!fn)fn=recomp_lookup(va);
    if(fn&&!crtSortTestActive&&!timerTestActive){auto& entry=lookupSlot(va);entry.va=va;entry.fn=fn;}
    return fn;
}
}
extern "C" recomp_func_t recomp_lookup_manual(uint32_t va) {
    auto& entry=lookupSlot(va);
    if(__builtin_expect(entry.va==va&&va,1)&&!crtSortTestActive&&!timerTestActive)return entry.fn;
    return lookupManualSlow(va);
}
extern "C" recomp_func_t recomp_lookup_import(uint32_t va) {
    checkGuestProgress();
    if(va>=soundMethodBase+0x1000&&va<soundMethodBase+0x5000&&!(va%16)){soundToken=va;return soundDispatch;}
    if(va>=inputMethodBase+0x1000&&va<inputMethodBase+0x5000&&!(va%16)){inputToken=va;return inputDispatch;}  // kinds 1-4: DirectInput, keyboard, mouse, gamepad
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
    if(va>=0x7e001000u&&va<0x7e010000u&&!(va%16)){d3dToken=va;return d3dDispatch;}
#endif
    if (va<importBase || (va-importBase)%16) return nullptr;
    uint32_t index=(va-importBase)/16;
    if (index>=imports.size()) return nullptr;
    lastImport=va;
    if(++imports[index].calls<=3)
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest import %s from %08x",imports[index].name.c_str(),g_cur_func);
    return imports[index].function;
}
extern "C" void recomp_unresolved(const char* kind,uint32_t va,uint32_t from) {
    NFS_RUNTIME_LOG(ANDROID_LOG_ERROR,"NFSC","Unresolved %s %08x from %08x: eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x esp=%08x",
        kind,va,from,g_eax,g_ebx,g_ecx,g_edx,g_esi,g_edi,g_esp);
    throw GuestStop(std::string(kind)+" unresolved "+hex(va)+" from "+hex(from));
}
extern "C" void recomp_unimpl(uint32_t va,const char* what) {
    throw GuestStop("Instruction unsupported at "+hex(va)+": "+what);
}
extern "C" void recomp_dump_trace(const char*) {}
// (NFSU2 rewrites its six-resolution menu here and intercepts the selector; Speed.exe's
// resolution menu is not mapped yet, so the game keeps its own modes and DXVK scales.)

std::string connectGuestRuntime(const char* executable) {
    std::unique_lock<MachineMutex> guard(machine);machineLease=&guard;
    if (memory) return "Runtime guest ya inicializado";
    State original; original.save();
    try {
        stopRequested=false;
        // Interactive Android sessions stop through lifecycle cancellation.
#ifdef __ANDROID__
        bootDeadline=std::chrono::steady_clock::time_point::max();
#else
        bootDeadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
#endif
        recomp_yield_hook=checkGuestProgress;
        memory=std::make_unique<GuestMemory>();
        hostGameRoot=std::string(executable); hostGameRoot=hostGameRoot.substr(0,hostGameRoot.find_last_of('/'));
        g_mem_base=reinterpret_cast<ptrdiff_t>(memory->base());
        testGuestHeaps(*memory);
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest heaps passed ownership, realloc preservation, zero tail, in-place failure and reuse checks");
        heaps=std::make_unique<GuestHeaps>(*memory);
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
        configureD3D8Bridge(memory->base(),allocate,freeD3DGuest);
#endif
        loadImage(executable);
        memory->commit(stackBase,stackSize); memory->commit(tib,0x1000);
        State fresh; fresh.esp=stackBase+stackSize-64; fresh.fs=tib; fresh.load();
        write32(tib,0xffffffff); write32(tib+4,stackBase+stackSize); write32(tib+8,stackBase); write32(tib+0x18,tib);
        testGuestScheduling();
        testGuestPow();
        testGuestInput();
        testGuestSoundCaps();
        // Restore a clean CPU state before executing the real CRT entry.
        fresh.load();
        attachStaticTls(g_fs_base);
        seedRegistry(displayModeWidth(),displayModeHeight());
        bindDeferredModuleImports(0);
        fresh.load();
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Entering actual lifted CRT entry %08x",entry);
        uint32_t result=callGuest(entry,{});
        joinGuestThreads(guard);machineLease=nullptr;shutdownGuestSound();
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
        shutdownD3D8Bridge();
#endif
        original.load();
        return "Entrada guest terminada: "+std::to_string(result);
    } catch (const std::exception& e) {
        NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSC","Guest integration boundary: %s",e.what());
        joinGuestThreads(guard);machineLease=nullptr;shutdownGuestSound();
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
        shutdownD3D8Bridge();
#endif
        original.load();
        return std::string("Arranque guest detenido: ")+e.what();
    }
}
void requestGuestRuntimeStop() {stopRequested=true;}
void setGuestPaused(bool paused) {
    // The AAudio stream keeps pulling from looping voices while the guest is
    // paused: in the background the last mixed sound repeated until the app
    // closed. Pause the output with the guest and restart it on resume.
    bool previous=guestPaused.exchange(paused);
    if(previous!=paused&&soundStream){
        aaudio_result_t status=paused?AAudioStream_requestPause(soundStream):AAudioStream_requestStart(soundStream);
        if(status!=AAUDIO_OK)NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFS","Audio %s failed: %d",paused?"pause":"resume",int(status));
    }
}
bool guestDrivingControls() {return inputDrivingMode.load();}
void setGuestPad(const float axes[6],uint32_t buttons,int hatX,int hatY){
    std::lock_guard<std::mutex> lock(inputMutex);
    for(unsigned i=0;i<6;++i)inputPad.axis[i]=std::max(-1.f,std::min(1.f,axes[i]));
    inputPad.buttons=buttons;inputPad.hatX=hatX;inputPad.hatY=hatY;
}
void setGuestPadConnected(bool connected){
    std::lock_guard<std::mutex> lock(inputMutex);
    if(connected!=inputPadConnected)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Gamepad %s",connected?"connected":"disconnected");
    inputPadConnected=connected;if(!connected)inputPad=InputPad{};
}
bool guestGamepadInUse(){return inputPadAcquired.load()>0;}
void setGuestDisplaySize(unsigned width,unsigned height){displayWidth=width;displayHeight=height;}
void setGuestResolution(unsigned width,unsigned height){
    std::lock_guard<MachineMutex> lock(machine);if(memory)return;
    if(width<320||height<240||width>8192||height>8192){renderWidth=renderHeight=0;return;}
    renderWidth=width;renderHeight=height;
#if defined(__ANDROID__) || defined(NFS_D3D9_BACKEND)
    setD3D8RenderSize(width,height);
#endif
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Selected render resolution %ux%u",width,height);
}
void setGuestGraphicsQuality(unsigned level){
    std::lock_guard<MachineMutex> lock(machine);if(memory)return;
    graphicsQuality=level<=4?level:0;
}
void setGuestFrameLimit(unsigned framesPerSecond){
    frameLimit=(framesPerSecond>=30&&framesPerSecond<=240)?framesPerSecond:0;
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Android frame limit %u FPS (0 means uncapped)",frameLimit.load());
}
void setGuestKey(unsigned scan,bool down){if(scan>=256)return;std::lock_guard<std::mutex> lock(inputMutex);if(bool(inputKeys[scan])==down)return;inputKeys[scan]=down?0x80:0;if(down)inputKeysUnseen[scan]=0x80;recordInputKey(scan,down);enqueueGuestKey(scan,down);}
void clearGuestInput(){std::lock_guard<std::mutex> lock(inputMutex);for(unsigned scan=0;scan<256;++scan)if(inputKeys[scan]){recordInputKey(scan,false);enqueueGuestKey(scan,false);}inputKeys.fill(0);inputKeysUnseen.fill(0);inputMouseButtons.fill(0);inputMouseX=inputMouseY=inputMouseZ=0;touchButtons.clear();touchMoved=false;}
void setGuestPointer(float x,float y,int phase){
    guestPointerX=std::clamp(x,0.f,1.f);guestPointerY=std::clamp(y,0.f,1.f);
    // Con el WidescreenFix los menús son 640x480 centrados en la pantalla; sin él, estirados.
    float width=float(std::max(displayWidth.load(),1u)),height=float(std::max(displayHeight.load(),1u));
    float menuX=x*640.f;
    if(guestWidescreen){float area=height*4.f/3.f;menuX=(x*width-(width-area)*.5f)/area*640.f;}
    mp2_menu_target_x=std::clamp(menuX,0.f,639.f);mp2_menu_target_y=std::clamp(y*480.f,0.f,479.f);mp2_menu_target_pending=1;
    {std::lock_guard<std::mutex> lock(inputMutex);touchMoved=true;if(phase!=1)touchButtons.push_back(phase==0?0x80:0);}
    enqueueGuestPointer(phase);
}
bool guestMenuActive(){
    struct ::timespec now{};::clock_gettime(CLOCK_MONOTONIC,&now);
    long long ms=(long long)now.tv_sec*1000+now.tv_nsec/1000000;
    return mp2_menu_last_ms&&ms-mp2_menu_last_ms<400;
}
// 0 cinemática, novela gráfica o carga; 1 menú; 2 gameplay.
int guestScreenMode(){
    if(guestMenuActive())return 1;
    struct ::timespec now{};::clock_gettime(CLOCK_MONOTONIC,&now);
    long long ms=(long long)now.tv_sec*1000+now.tv_nsec/1000000;
    int mode=mp2_look_last_ms&&ms-mp2_look_last_ms<300?2:0;
    static int logged=-1;if(mode!=logged){logged=mode;NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Pantalla: %s",mode==2?"gameplay":"sin controles");}
    return mode;
}
void setGuestHiddenFiles(const char* names){
    std::lock_guard<MachineMutex> lock(machine);if(memory||!names)return;
    std::stringstream list(names);std::string name;
    while(std::getline(list,name,';'))if(!name.empty())guestHiddenFiles.insert(lower(name));
}
void setGuestFlags(const char* flags){std::lock_guard<MachineMutex> lock(machine);if(!memory&&flags)guestExtraFlags=flags;}
void typeGuestText(const std::vector<uint32_t>& characters){enqueueGuestText(characters);}
void setGuestWidescreen(bool enabled){std::lock_guard<MachineMutex> lock(machine);if(!memory)guestWidescreen=enabled;}
void addGuestMouseMotion(float dx,float dy){
    std::lock_guard<std::mutex> lock(inputMutex);
    guestMouseFractionX+=dx;guestMouseFractionY+=dy;
    int32_t wholeX=int32_t(guestMouseFractionX),wholeY=int32_t(guestMouseFractionY);
    inputMouseX+=wholeX;inputMouseY+=wholeY;guestMouseFractionX-=float(wholeX);guestMouseFractionY-=float(wholeY);
}
void setGuestMouseButton(unsigned button,bool down){
    if(button>=8)return;
    std::lock_guard<std::mutex> lock(inputMutex);
    if(bool(inputMouseButtons[button])==down)return;
    inputMouseButtons[button]=down?0x80:0;
    recordInputMouse(button,down);
}
void addGuestMouseWheel(int delta){std::lock_guard<std::mutex> lock(inputMutex);inputMouseZ+=delta;recordInputWheel(delta);}
