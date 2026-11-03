// msvcrt.dll for lifted guest DLLs. speed.exe carries its own CRT, but
// d3dx9_26.dll imports 63 msvcrt entries (heap, strings, ctype, math, qsort,
// _initterm). All are cdecl: the caller pops the arguments (ret(value,0)).
// Doubles come in on the stack and leave in ST(0); the _CI* intrinsics take
// their operands on the x87 stack. Anything not listed stays unsupported and
// stops with its name.
uint32_t msvcrtHeap(){return GuestHeaps::process;}
double guestDouble(uint32_t address){uint64_t bits=uint64_t(read32(address))|uint64_t(read32(address+4))<<32;double value;std::memcpy(&value,&bits,8);return value;}
void retDouble(double value){fp_push_impl(g_st,&g_fp_top,value);ret(0,0);}
double popDouble(){return fp_pop_impl(g_st,&g_fp_top);}

void crtMalloc(){ret(heaps->alloc(msvcrtHeap(),0,arg(0)),0);}
void crtCalloc(){uint64_t bytes=uint64_t(arg(0))*arg(1);ret(bytes>UINT32_MAX?0:heaps->alloc(msvcrtHeap(),8,uint32_t(bytes)),0);}
void crtFree(){if(arg(0))heaps->free(msvcrtHeap(),arg(0));ret(0,0);}
void crtRealloc(){
    uint32_t block=arg(0),bytes=arg(1);
    if(!block){ret(heaps->alloc(msvcrtHeap(),0,bytes),0);return;}
    if(!bytes){heaps->free(msvcrtHeap(),block);ret(0,0);return;}
    ret(heaps->realloc(msvcrtHeap(),0,block,bytes),0);
}
void crtStrdup(){
    if(!arg(0)){ret(0,0);return;}
    auto text=guestString(arg(0));uint32_t copy=heaps->alloc(msvcrtHeap(),0,uint32_t(text.size()+1));
    if(copy)std::memcpy(ptr(copy,text.size()+1),text.c_str(),text.size()+1);
    ret(copy,0);
}
void crtMemmove(){if(arg(2))std::memmove(ptr(arg(0),arg(2)),ptr(arg(1),arg(2)),arg(2));ret(arg(0),0);}
void crtStrchr(){
    uint32_t text=arg(0);char wanted=char(arg(1));
    for(uint32_t i=0;;++i){char c=*static_cast<char*>(ptr(text+i,1));if(c==wanted){ret(text+i,0);return;}if(!c){ret(0,0);return;}}
}
void crtStricmp(){
    auto a=lower(guestString(arg(0))),b=lower(guestString(arg(1)));int r=a.compare(b);
    ret(uint32_t(r<0?-1:r>0?1:0),0);
}
void crtWcslen(){uint32_t n=0;for(;;++n){uint16_t c;std::memcpy(&c,ptr(arg(0)+n*2,2),2);if(!c)break;}ret(n,0);}
void crtWcstombs(){
    uint32_t dest=arg(0),source=arg(1),limit=arg(2),n=0;
    for(;;++n){uint16_t c;std::memcpy(&c,ptr(source+n*2,2),2);if(!c)break;if(c>0x7f){ret(UINT32_MAX,0);return;}
        if(dest){if(n>=limit)break;*static_cast<char*>(ptr(dest+n,1))=char(c);}}
    if(dest&&n<limit)*static_cast<char*>(ptr(dest+n,1))=0;
    ret(n,0);
}
template<int(*F)(int)> void crtCtype(){ret(uint32_t(F(int(arg(0)&0xff))?1:0),0);}
void crtToupper(){ret(uint32_t(std::toupper(int(arg(0)&0xff))),0);}
void crtTolower(){ret(uint32_t(std::tolower(int(arg(0)&0xff))),0);}
void crtIswspace(){uint32_t c=arg(0)&0xffff;ret(c<128&&std::isspace(int(c))?1:0,0);}
void crtIswalpha(){uint32_t c=arg(0)&0xffff;ret(c<128&&std::isalpha(int(c))?1:0,0);}
void crtIswdigit(){uint32_t c=arg(0)&0xffff;ret(c<128&&std::isdigit(int(c))?1:0,0);}
void crtIswpunct(){uint32_t c=arg(0)&0xffff;ret(c<128&&std::ispunct(int(c))?1:0,0);}
void crtAtoi(){ret(uint32_t(std::atoi(guestString(arg(0)).c_str())),0);}
void crtAtof(){retDouble(std::atof(guestString(arg(0)).c_str()));}
void crtUltoa(){
    uint32_t value=arg(0),out=arg(1),radix=arg(2);
    if(radix<2||radix>36){ret(out,0);return;}
    std::string digits;do{uint32_t d=value%radix;digits.insert(digits.begin(),char(d<10?'0'+d:'a'+d-10));value/=radix;}while(value);
    std::memcpy(ptr(out,digits.size()+1),digits.c_str(),digits.size()+1);ret(out,0);
}
void crtFloor(){retDouble(std::floor(guestDouble(g_esp+4)));}
void crtCeil(){retDouble(std::ceil(guestDouble(g_esp+4)));}
void crtLdexp(){retDouble(std::ldexp(guestDouble(g_esp+4),int32_t(read32(g_esp+12))));}
void crtFrexp(){int e=0;double m=std::frexp(guestDouble(g_esp+4),&e);write32(read32(g_esp+12),uint32_t(e));retDouble(m);}
void crtModf(){
    double whole=0,fraction=std::modf(guestDouble(g_esp+4),&whole);uint64_t bits;std::memcpy(&bits,&whole,8);
    uint32_t out=read32(g_esp+12);write32(out,uint32_t(bits));write32(out+4,uint32_t(bits>>32));retDouble(fraction);
}
void crtFinite(){ret(std::isfinite(guestDouble(g_esp+4))?1:0,0);}
void crtIsnan(){ret(std::isnan(guestDouble(g_esp+4))?1:0,0);}
void crtFpclass(){
    double v=guestDouble(g_esp+4);bool negative=std::signbit(v);uint32_t c;
    switch(std::fpclassify(v)){
    case FP_NAN:c=0x0002;break;                          // _FPCLASS_QNAN
    case FP_INFINITE:c=negative?0x0004:0x0200;break;     // _FPCLASS_NINF / PINF
    case FP_ZERO:c=negative?0x0020:0x0040;break;         // NZ / PZ
    case FP_SUBNORMAL:c=negative?0x0010:0x0080;break;    // ND / PD
    default:c=negative?0x0008:0x0100;                    // NN / PN
    }
    ret(c,0);
}
// _CI intrinsics: operand(s) on the x87 stack, result replaces them in ST(0).
template<double(*F)(double)> void crtCI1(){double x=popDouble();retDouble(F(x));}
template<double(*F)(double,double)> void crtCI2(){double y=popDouble(),x=popDouble();retDouble(F(x,y));}
double crtSqrtFn(double x){return std::sqrt(x);}
double crtExpFn(double x){return std::exp(x);}
double crtCoshFn(double x){return std::cosh(x);}
double crtSinhFn(double x){return std::sinh(x);}
double crtTanhFn(double x){return std::tanh(x);}
double crtAsinFn(double x){return std::asin(x);}
double crtAcosFn(double x){return std::acos(x);}
double crtPowFn(double x,double y){return std::pow(x,y);}
double crtFmodFn(double x,double y){return std::fmod(x,y);}
// _controlfp(new, mask): only the x87 control word is modelled.
void crtControlfp(){uint32_t value=arg(0),mask=arg(1);g_fpu_cw=uint16_t((g_fpu_cw&~mask)|(value&mask));ret(g_fpu_cw,0);}
uint32_t crtRandState=1;
void crtSrand(){crtRandState=arg(0);ret(0,0);}
void crtRand(){crtRandState=crtRandState*214013u+2531011u;ret((crtRandState>>16)&0x7fff,0);}
// _initterm(begin, end): run the non-null function pointers in [begin, end).
void crtInitterm(){
    uint32_t begin=arg(0),end=arg(1);
    for(uint32_t p=begin;p<end;p+=4)if(uint32_t fn=read32(p))callGuest(fn,{});
    ret(0,0);
}
// qsort(base, count, width, compare): a stable merge sort through the guest comparator.
void crtQsort(){
    uint32_t base=arg(0),count=arg(1),width=arg(2),compare=arg(3);
    if(count<2||!width){ret(0,0);return;}
    std::vector<uint32_t> order(count);for(uint32_t i=0;i<count;++i)order[i]=i;
    std::vector<unsigned char> copy(uint64_t(count)*width);std::memcpy(copy.data(),ptr(base,uint32_t(copy.size())),copy.size());
    uint32_t scratch=heaps->alloc(msvcrtHeap(),0,width*2);
    auto less=[&](uint32_t a,uint32_t b){
        std::memcpy(ptr(scratch,width),copy.data()+uint64_t(a)*width,width);
        std::memcpy(ptr(scratch+width,width),copy.data()+uint64_t(b)*width,width);
        // The comparator is cdecl: its RET removes only the return address.
        // The caller must release its two 32-bit arguments explicitly.
        return int32_t(callGuest(compare,{scratch,scratch+width},8))<0;};
    std::stable_sort(order.begin(),order.end(),less);
    heaps->free(msvcrtHeap(),scratch);
    for(uint32_t i=0;i<count;++i)std::memcpy(ptr(base+i*width,width),copy.data()+uint64_t(order[i])*width,width);
    ret(0,0);
}
constexpr uint32_t crtSortTestAddress=0x7b000050u,crtCompareTestAddress=0x7b000060u;
bool crtSortTestActive=false;
void crtCompareTest(){
    int32_t a=int32_t(read32(arg(0))),b=int32_t(read32(arg(1)));
    // Also exercise preservation of callback register state.
    g_ebx=0xdead1234;ret(uint32_t(a<b?-1:a>b?1:0),0);
}
void testGuestQsort(){
    State saved;saved.save();uint32_t data=allocate(20);
    struct Cleanup{uint32_t data;State& saved;~Cleanup(){crtSortTestActive=false;heaps->free(GuestHeaps::process,data);saved.load();}} cleanup{data,saved};
    const int32_t values[]={7,-4,7,0,2},expected[]={-4,0,2,7,7};
    for(unsigned i=0;i<5;++i)write32(data+i*4,uint32_t(values[i]));
    crtSortTestActive=true;g_ebx=0x12345678;uint32_t before=g_esp;
    callGuest(crtSortTestAddress,{data,5,4,crtCompareTestAddress},16);
    if(g_esp!=before||g_ebx!=0x12345678)throw GuestStop("qsort cdecl callback corrupted caller registers or stack");
    for(unsigned i=0;i<5;++i)if(int32_t(read32(data+i*4))!=expected[i])throw GuestStop("qsort cdecl comparator returned incorrect ordering");
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Guest qsort cdecl callback tests passed: ordering, duplicates, registers and stack");
}
void crtVsnprintf(){
    uint32_t out=arg(0),limit=arg(1);auto text=formatGuestA(guestString(arg(2)),arg(3));
    if(text.size()>=limit){if(limit)std::memcpy(ptr(out,limit),text.data(),limit);ret(UINT32_MAX,0);return;}
    std::memcpy(ptr(out,text.size()+1),text.c_str(),text.size()+1);ret(uint32_t(text.size()),0);
}
uint32_t crtLocaleName=0;
void crtSetlocale(){if(!crtLocaleName)crtLocaleName=ansiString("C");ret(crtLocaleName,0);}
void crtPurecall(){throw GuestStop("msvcrt _purecall: pure virtual function called from "+hex(g_cur_func));}
void crtExit(){throw GuestStop("msvcrt exit("+std::to_string(int32_t(arg(0)))+") from "+hex(g_cur_func));}

const std::map<std::string,recomp_func_t> msvcrtFunctions={
    {"malloc",crtMalloc},{"calloc",crtCalloc},{"realloc",crtRealloc},{"free",crtFree},
    {"??2@YAPAXI@Z",crtMalloc},{"??3@YAXPAX@Z",crtFree},{"_strdup",crtStrdup},{"memmove",crtMemmove},
    {"strchr",crtStrchr},{"_stricmp",crtStricmp},{"wcslen",crtWcslen},{"wcstombs",crtWcstombs},
    {"isxdigit",crtCtype<std::isxdigit>},{"isalnum",crtCtype<std::isalnum>},{"isalpha",crtCtype<std::isalpha>},
    {"isspace",crtCtype<std::isspace>},{"isdigit",crtCtype<std::isdigit>},{"toupper",crtToupper},{"tolower",crtTolower},
    {"iswspace",crtIswspace},{"iswalpha",crtIswalpha},{"iswdigit",crtIswdigit},{"iswpunct",crtIswpunct},
    {"atoi",crtAtoi},{"atol",crtAtoi},{"atof",crtAtof},{"_ultoa",crtUltoa},
    {"floor",crtFloor},{"ceil",crtCeil},{"ldexp",crtLdexp},{"frexp",crtFrexp},{"modf",crtModf},
    {"_finite",crtFinite},{"_isnan",crtIsnan},{"_fpclass",crtFpclass},
    {"_CIsqrt",crtCI1<crtSqrtFn>},{"_CIexp",crtCI1<crtExpFn>},{"_CIcosh",crtCI1<crtCoshFn>},{"_CIsinh",crtCI1<crtSinhFn>},
    {"_CItanh",crtCI1<crtTanhFn>},{"_CIasin",crtCI1<crtAsinFn>},{"_CIacos",crtCI1<crtAcosFn>},
    {"_CIpow",crtCI2<crtPowFn>},{"_CIfmod",crtCI2<crtFmodFn>},
    {"_controlfp",crtControlfp},{"srand",crtSrand},{"rand",crtRand},{"_initterm",crtInitterm},{"qsort",crtQsort},
    {"_vsnprintf",crtVsnprintf},{"setlocale",crtSetlocale},{"_purecall",crtPurecall},{"exit",crtExit},
    {"?terminate@@YAXXZ",crtPurecall},
};
// Data exports: a zeroed guest cell whose address goes into the IAT slot.
uint32_t msvcrtData(const std::string& name){
    static std::map<std::string,uint32_t> cells;
    if(name!="_adjust_fdiv")return 0;   // the only one d3dx9_26 imports (0: no Pentium FDIV workaround)
    auto found=cells.find(name);if(found!=cells.end())return found->second;
    uint32_t cell=allocate(16);std::memset(ptr(cell,16),0,16);cells[name]=cell;return cell;
}

// KERNEL32 entries only lifted DLLs import (d3dx9_26.dll).
void k32DisableThreadLibraryCalls(){ret(1,1);}
// VirtualAlloc/VirtualFree on the process heap, page aligned (MEM_COMMIT|MEM_RESERVE
// of fresh memory only; the d3dx9 pools never ask for a fixed address).
std::map<uint32_t,uint32_t> virtualBlocks;  // aligned VA -> heap block
void k32VirtualAlloc(){
    uint32_t address=arg(0),bytes=arg(1);
    // Confirmar páginas de una reserva anterior: ya están en memoria.
    if(address){
        auto owner=virtualBlocks.upper_bound(address);
        if(owner!=virtualBlocks.begin()){--owner;ret(address&~0xfffu,4);return;}
        lastError=87;ret(0,4);return;
    }
    uint32_t block=heaps->alloc(msvcrtHeap(),8,bytes+0x1000);if(!block){lastError=8;ret(0,4);return;}
    uint32_t aligned=(block+0xfffu)&~0xfffu;virtualBlocks[aligned]=block;ret(aligned,4);
}
void k32VirtualFree(){
    auto found=virtualBlocks.find(arg(0));
    if(found==virtualBlocks.end()){if(arg(2)&0x4000){ret(1,3);return;}lastError=487;ret(0,3);return;}
    if(arg(2)&0x8000){heaps->free(msvcrtHeap(),found->second);virtualBlocks.erase(found);}  // MEM_RELEASE
    ret(1,3);
}
void k32GlobalMemoryStatus(){
    uint32_t out=arg(0);std::memset(ptr(out,32),0,32);
    const uint32_t values[]={32,40,0x7fffffffu,0x40000000u,0x7fffffffu,0x40000000u,0x7ffe0000u,0x60000000u};
    for(int i=0;i<8;++i)write32(out+4*i,values[i]);
    ret(0,1);
}
void k32IsDBCSLeadByte(){ret(0,1);}
void k32IsBadCodePtr(){ret(0,1);}
// CompareStringA(locale, flags, s1, n1, s2, n2) -> 1/2/3; NORM_IGNORECASE folds ASCII.
void k32CompareStringA(){
    auto read=[&](uint32_t text,int32_t count){std::string s=count<0?guestString(text):std::string(static_cast<char*>(ptr(text,uint32_t(count))),uint32_t(count));
        return (arg(1)&1)?lower(s):s;};
    int r=read(arg(2),int32_t(arg(3))).compare(read(arg(4),int32_t(arg(5))));ret(r<0?1u:r>0?3u:2u,6);
}
void k32ExpandEnvironmentStringsA(){
    auto text=guestString(arg(0));uint32_t out=arg(1),size=arg(2),need=uint32_t(text.size()+1);
    if(out&&size>=need)std::memcpy(ptr(out,need),text.c_str(),need);
    ret(need,3);
}

// KERNEL32/USER32 entries Carbon's speed.exe calls (measured with the
// Windows host's --trace-native) that the Underground ports never needed.
void k32GlobalMemoryStatusEx(){
    uint32_t out=arg(0);if(read32(out)!=64){lastError=87;ret(0,1);return;}
    std::memset(ptr(out+4,60),0,60);write32(out+4,30);
    auto put64=[&](uint32_t at,uint64_t v){write32(out+at,uint32_t(v));write32(out+at+4,uint32_t(v>>32));};
    put64(8,2ull<<30);put64(16,3ull<<29);put64(24,4ull<<30);put64(32,3ull<<30);put64(40,2ull<<30);put64(48,3ull<<29);
    ret(1,1);
}
void k32HeapValidate(){ret(1,3);}
void systemTimeOf(std::chrono::system_clock::time_point when,uint32_t out){
    auto since=when.time_since_epoch();time_t seconds=std::chrono::duration_cast<std::chrono::seconds>(since).count();
    std::tm utc{};gmtime_r(&seconds,&utc);uint16_t fields[8]={uint16_t(utc.tm_year+1900),uint16_t(utc.tm_mon+1),uint16_t(utc.tm_wday),
        uint16_t(utc.tm_mday),uint16_t(utc.tm_hour),uint16_t(utc.tm_min),uint16_t(utc.tm_sec),
        uint16_t(std::chrono::duration_cast<std::chrono::milliseconds>(since).count()%1000)};
    std::memcpy(ptr(out,16),fields,16);
}
void k32GetSystemTime(){systemTimeOf(std::chrono::system_clock::now(),arg(0));ret(0,1);}
void k32SystemTimeToFileTime(){
    uint16_t f[8];std::memcpy(f,ptr(arg(0),16),16);
    if(f[1]<1||f[1]>12||f[3]<1||f[3]>31||f[4]>23||f[5]>59||f[6]>59||f[7]>999){lastError=87;ret(0,2);return;}
    std::tm t{};t.tm_year=f[0]-1900;t.tm_mon=f[1]-1;t.tm_mday=f[3];t.tm_hour=f[4];t.tm_min=f[5];t.tm_sec=f[6];
    int64_t seconds=int64_t(timegm(&t));uint64_t ft=uint64_t((seconds*1000+f[7])*10000)+116444736000000000ull;
    write32(arg(1),uint32_t(ft));write32(arg(1)+4,uint32_t(ft>>32));ret(1,2);
}
void u32IsIconic(){ret(0,1);}
void u32GetKeyState(){ret(0,1);}   // keys come through DirectInput
void u32GetKeyboardLayout(){ret(0x04090409u,1);}
void u32SendInput(){ret(arg(0),3);}  // the game only sends key releases; nothing to inject on Android
// MapVirtualKey(code, type): VK <-> set-1 scan code for the keys the controls menu names.
const std::pair<uint8_t,uint8_t> vkScan[]={{0x08,0x0e},{0x09,0x0f},{0x0d,0x1c},{0x10,0x2a},{0x11,0x1d},{0x12,0x38},{0x1b,0x01},{0x20,0x39},
    {0x25,0x4b},{0x26,0x48},{0x27,0x4d},{0x28,0x50},{0x2d,0x52},{0x2e,0x53},{0x24,0x47},{0x23,0x4f},{0x21,0x49},{0x22,0x51},
    {'0',0x0b},{'1',0x02},{'2',0x03},{'3',0x04},{'4',0x05},{'5',0x06},{'6',0x07},{'7',0x08},{'8',0x09},{'9',0x0a},
    {'A',0x1e},{'B',0x30},{'C',0x2e},{'D',0x20},{'E',0x12},{'F',0x21},{'G',0x22},{'H',0x23},{'I',0x17},{'J',0x24},{'K',0x25},{'L',0x26},{'M',0x32},
    {'N',0x31},{'O',0x18},{'P',0x19},{'Q',0x10},{'R',0x13},{'S',0x1f},{'T',0x14},{'U',0x16},{'V',0x2f},{'W',0x11},{'X',0x2d},{'Y',0x15},{'Z',0x2c}};
uint32_t mapVirtualKey(uint32_t code,uint32_t type){
    for(auto [vk,scan]:vkScan){
        if((type==0||type==2)&&vk==code)return type==0?scan:(vk>='A'&&vk<='Z'?uint32_t(vk):vk>='0'&&vk<='9'?uint32_t(vk):0);
        if((type==1||type==3)&&scan==code)return vk;
    }
    return 0;
}
void u32MapVirtualKeyA(){ret(mapVirtualKey(arg(0),arg(1)),2);}
void u32MapVirtualKeyExA(){ret(mapVirtualKey(arg(0),arg(1)),3);}
const std::map<std::string,recomp_func_t> windowModuleFunctions={
    {"IsIconic",u32IsIconic},{"GetKeyState",u32GetKeyState},{"GetKeyboardLayout",u32GetKeyboardLayout},{"SendInput",u32SendInput},
    {"MapVirtualKeyA",u32MapVirtualKeyA},{"MapVirtualKeyExA",u32MapVirtualKeyExA},
};
void k32UnhandledExceptionFilter(){ret(0,1);}  // EXCEPTION_CONTINUE_SEARCH
void k32TerminateProcess(){throw GuestStop("TerminateProcess("+std::to_string(int32_t(arg(1)))+") from "+hex(g_cur_func));}
const std::map<std::string,recomp_func_t> kernelModuleFunctions={
    {"DisableThreadLibraryCalls",k32DisableThreadLibraryCalls},{"VirtualAlloc",k32VirtualAlloc},{"VirtualFree",k32VirtualFree},
    {"GlobalMemoryStatus",k32GlobalMemoryStatus},{"IsDBCSLeadByte",k32IsDBCSLeadByte},{"IsBadCodePtr",k32IsBadCodePtr},
    {"CompareStringA",k32CompareStringA},{"ExpandEnvironmentStringsA",k32ExpandEnvironmentStringsA},
    {"UnhandledExceptionFilter",k32UnhandledExceptionFilter},{"TerminateProcess",k32TerminateProcess},
    {"GlobalMemoryStatusEx",k32GlobalMemoryStatusEx},{"HeapValidate",k32HeapValidate},{"GetSystemTime",k32GetSystemTime},
    {"SystemTimeToFileTime",k32SystemTimeToFileTime},{"CreateSemaphoreA",createSemaphore},{"ReleaseSemaphore",releaseSemaphore},
    {"CreateWaitableTimerA",createWaitableTimer},{"SetWaitableTimer",setWaitableTimer},
};
