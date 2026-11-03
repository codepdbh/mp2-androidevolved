// Win32 entries MaxPayne2.exe imports that the Underground and Most Wanted ports
// never needed. All stdcall: ret(value, argument count).

void k32SetThreadIdealProcessor(){ret(0,2);}  // previous ideal processor; the host schedules threads
void k32InterlockedExchangeAdd(){uint32_t old=read32(arg(0));write32(arg(0),old+arg(1));ret(old,2);}  // under the machine lock
void k32GetDiskFreeSpaceExA(){
    struct statvfs disk{};if(statvfs(hostGameRoot.c_str(),&disk)){lastError=fileError();ret(0,4);return;}
    uint64_t available=uint64_t(disk.f_bavail)*disk.f_frsize,total=uint64_t(disk.f_blocks)*disk.f_frsize,free=uint64_t(disk.f_bfree)*disk.f_frsize;
    auto put=[](uint32_t at,uint64_t value){if(at){write32(at,uint32_t(value));write32(at+4,uint32_t(value>>32));}};
    put(arg(1),available);put(arg(2),total);put(arg(3),free);ret(1,4);
}
void k32GetComputerNameA(){
    static const char name[]="ANDROID";uint32_t out=arg(0),size=arg(1),capacity=read32(size);
    if(capacity<sizeof name){write32(size,sizeof name);lastError=111;ret(0,2);return;}  // ERROR_BUFFER_OVERFLOW
    std::memcpy(ptr(out,sizeof name),name,sizeof name);write32(size,sizeof name-1);ret(1,2);
}

// A Windows system folder: paths under it resolve to the nfsc/_outside sandbox
// (gamePath), so the game finds no system files there.
void k32GetSystemDirectoryA(){
    static const char path[]="C:\\WINDOWS\\system32";uint32_t out=arg(0),capacity=arg(1);
    if(capacity<sizeof path){ret(sizeof path,2);return;}
    std::memcpy(ptr(out,sizeof path),path,sizeof path);ret(sizeof path-1,2);
}
// GlobalAlloc with GMEM_FIXED (the handle is the pointer); movable blocks
// would need GlobalLock and are refused.

// The administrators check (AllocateAndInitializeSid + CheckTokenMembership):
// the Android app owns its data, so the player counts as an administrator.
void advAllocateAndInitializeSid(){
    uint32_t sid=allocate(16);std::memset(ptr(sid,16),0,16);write32(arg(10),sid);ret(1,11);
}
void advFreeSid(){if(arg(0))heaps->free(GuestHeaps::process,arg(0));ret(0,1);}
void advCheckTokenMembership(){write32(arg(2),1);ret(1,3);}

// CryptoAPI random numbers and GUIDs from the host generator.
std::mt19937& hostRandom(){static std::mt19937 generator{std::random_device{}()};return generator;}
void fillRandom(uint32_t out,uint32_t bytes){auto* data=static_cast<uint8_t*>(ptr(out,bytes));for(uint32_t i=0;i<bytes;++i)data[i]=uint8_t(hostRandom()());}
void advCryptAcquireContextA(){write32(arg(0),0xf0070001u);ret(1,5);}
void advCryptGenRandom(){if(arg(0)!=0xf0070001u){lastError=6;ret(0,3);return;}if(arg(1))fillRandom(arg(2),arg(1));ret(1,3);}
// GetAdaptersInfo: one Ethernet adapter whose address is the stable guest MAC
// (guest_network.inc, from the launcher's network identity); NFSCO derives
// its machine identity from it. IP_ADAPTER_INFO is 640 bytes.
void iphlpGetAdaptersInfo(){
    uint32_t out=arg(0),sizePointer=arg(1);
    if(!sizePointer){ret(87,2);return;}
    if(!out||read32(sizePointer)<640){write32(sizePointer,640);ret(111,2);return;}  // ERROR_BUFFER_OVERFLOW
    std::memset(ptr(out,640),0,640);write32(out+4,1);
    static const char name[]="{4E46534E-4353-0000-0000-414E44524F49}",description[]="Android network";
    std::memcpy(ptr(out+8,sizeof name),name,sizeof name);std::memcpy(ptr(out+268,sizeof description),description,sizeof description);
    write32(out+400,6);std::memcpy(ptr(out+404,6),guestNetworkMac.data(),6);write32(out+412,1);write32(out+416,6);  // MIB_IF_TYPE_ETHERNET
    for(uint32_t list:{428u,468u})std::memcpy(ptr(out+list+4,8),"0.0.0.0",8);
    ret(0,2);
}
// RtlGenRandom (advapi32!SystemFunction036): rand_s and std::random_device in
// NFSCO's CRT. Returns BOOLEAN TRUE.
void advRtlGenRandom(){if(arg(1))fillRandom(arg(0),arg(1));ret(1,2);}
void advCryptReleaseContext(){ret(arg(0)==0xf0070001u?1:0,2);}
void oleCoCreateGuid(){
    fillRandom(arg(0),16);auto* guid=static_cast<uint8_t*>(ptr(arg(0),16));
    guid[7]=uint8_t((guid[7]&0x0f)|0x40);guid[8]=uint8_t((guid[8]&0x3f)|0x80);  // version 4, RFC 4122 variant
    ret(0,1);
}

const std::map<std::string,recomp_func_t> carbonFunctions={
    {"kernel32.dll!SetThreadIdealProcessor",k32SetThreadIdealProcessor},{"kernel32.dll!InterlockedExchangeAdd",k32InterlockedExchangeAdd},
    {"kernel32.dll!GetDiskFreeSpaceExA",k32GetDiskFreeSpaceExA},{"kernel32.dll!GetComputerNameA",k32GetComputerNameA},
    {"kernel32.dll!GetSystemDirectoryA",k32GetSystemDirectoryA},
    {"advapi32.dll!AllocateAndInitializeSid",advAllocateAndInitializeSid},{"advapi32.dll!FreeSid",advFreeSid},
    {"advapi32.dll!CheckTokenMembership",advCheckTokenMembership},{"advapi32.dll!CryptAcquireContextA",advCryptAcquireContextA},
    {"advapi32.dll!CryptGenRandom",advCryptGenRandom},{"advapi32.dll!CryptReleaseContext",advCryptReleaseContext},
    {"ole32.dll!CoCreateGuid",oleCoCreateGuid},
    {"advapi32.dll!SystemFunction036",advRtlGenRandom},
    {"iphlpapi.dll!GetAdaptersInfo",iphlpGetAdaptersInfo},
};
