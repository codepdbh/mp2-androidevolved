// Game DLLs that run as lifted code next to Speed.exe. server.dll is EA's LAN
// game server: hosting a LAN race does LoadLibrary("server.dll") and calls its
// StartServer/StopServer/IsServerRunning exports.
//
// Lifted code bakes absolute addresses, so each DLL is lifted (run_lift.py
// --prefix) from a copy relocated to a fixed guest base (prepare_module.py) and
// mapped here at that same base: the original file from the game folder, with
// its base relocations applied for that base. The layout check below ties the
// file to the generated code.
struct GuestModuleSpec {const char* name;uint32_t base,preferredBase,imageSize;bool lifted;const char* folder="";};
// Generado de config/pc/modules.json: las bases están en el código recompilado.
constexpr GuestModuleSpec guestModuleSpecs[]={
    {"msvcr71.dll",0x00600000u,0x7c340000u,0x56000u,true,""},
    {"msvcp71.dll",0x00660000u,0x7c3a0000u,0x7b000u,true,""},
    {"mfc71.dll",0x006e0000u,0x7c140000u,0x103000u,true,""},
    {"rlmfc.dll",0x007f0000u,0x10000000u,0x61000u,true,""},
    {"grphmfc.dll",0x00860000u,0x10000000u,0x1e000u,true,""},
    {"e2mfc.dll",0x00880000u,0x10000000u,0x6a000u,true,""},
    {"t_graphmfc.dll",0x008f0000u,0x10000000u,0x9000u,true,""},
    {"kf2mfc.dll",0x00900000u,0x10000000u,0x5b000u,true,""},
    {"psyslibmfc.dll",0x00960000u,0x10000000u,0x1a000u,true,""},
    {"sndmfc.dll",0x00980000u,0x10000000u,0x1a000u,true,""},
    {"x_helpersmfc.dll",0x009a0000u,0x10000000u,0xd000u,true,""},
    {"x_shareddbmfc.dll",0x009b0000u,0x10000000u,0x6e000u,true,""},
    {"x_messagemfc.dll",0x00a20000u,0x10000000u,0x21000u,true,""},
    {"x_inputmfc.dll",0x00a50000u,0x10000000u,0x14000u,true,""},
    {"x_physicalsimulationmfc.dll",0x00a70000u,0x10000000u,0xd4000u,true,""},
    {"x_levelmfc.dll",0x00b50000u,0x10000000u,0x1f000u,true,""},
    {"x_levelruntimemfc.dll",0x00b70000u,0x10000000u,0x1d000u,true,""},
    {"x_audiomfc.dll",0x00b90000u,0x10000000u,0x37000u,true,""},
    {"x_modesmfc.dll",0x00bd0000u,0x10000000u,0x32000u,true,""},
    {"x_basicmodesmfc.dll",0x00c10000u,0x10000000u,0x50000u,true,""},
    {"x_gameobjectsmfc.dll",0x00c60000u,0x10000000u,0x1d6000u,true,""},
    {"binkw32.dll",0x00e40000u,0x30000000u,0x72000u,true,"e2driver\\"},
    {"e2_d3d8_driver_mfc.dll",0x00ec0000u,0x10000000u,0x87000u,true,"e2driver\\"},
    {"maxpayne2.widescreenfix.asi",0x00f50000u,0x10000000u,0x3c000u,true,"scripts\\"},
};
bool guestExecutable(uint32_t address){
    if(address>=imageBase&&address-imageBase<imageSize)return true;
    for(auto& spec:guestModuleSpecs)if(address>=spec.base&&address-spec.base<spec.imageSize)return true;
    return false;
}
// Static TLS (__declspec(thread)) of palmont.dll and the WidescreenFix: each
// module's template is copied into a block per guest thread, reached through
// the TEB+0x2C array at the module's slot.
struct StaticTls {uint32_t start=0,bytes=0,zeroFill=0;};
std::vector<StaticTls> staticTls;
constexpr uint32_t staticTlsSlots=16;
void attachStaticTlsSlot(uint32_t tib,uint32_t slot){
    uint32_t array=read32(tib+0x2c);
    if(!array){array=allocate(staticTlsSlots*4);std::memset(ptr(array,staticTlsSlots*4),0,staticTlsSlots*4);write32(tib+0x2c,array);}
    if(read32(array+slot*4))return;
    auto& tls=staticTls[slot];uint32_t total=tls.bytes+tls.zeroFill+16,block=allocate(total);
    std::memset(ptr(block,total),0,total);
    if(tls.bytes)std::memcpy(ptr(block,tls.bytes),ptr(tls.start,tls.bytes),tls.bytes);
    write32(array+slot*4,block);
}
// A minimal PEB shared by every guest thread (TEB+0x30): image base, process
// heap, Windows XP SP2 version and empty RTL_USER_PROCESS_PARAMETERS. NFSCO's
// CRT reads PEB->ProcessParameters->Flags to tell a packaged app.
uint32_t guestPeb(){
    static uint32_t peb=0;if(peb)return peb;
    peb=allocate(0x1000);std::memset(ptr(peb,0x1000),0,0x1000);uint32_t parameters=peb+0x800;
    write32(peb+0x8,imageBase);write32(peb+0x10,parameters);write32(peb+0x18,GuestHeaps::process);
    write32(peb+0xa4,5);write32(peb+0xa8,1);write32(peb+0xac,2600);write32(peb+0xb0,2);  // OSMajor/Minor/Build, Platform
    write32(parameters,0x298);write32(parameters+4,0x298);                              // MaximumLength, Length
    return peb;
}
void attachStaticTls(uint32_t tib){
    if(!read32(tib+0x30))write32(tib+0x30,guestPeb());
    for(uint32_t slot=0;slot<staticTls.size();++slot)attachStaticTlsSlot(tib,slot);
}
bool isLiftedModuleName(const std::string& dll){for(auto& s:guestModuleSpecs)if(dll==s.name)return s.lifted;return false;}
struct GuestModule {
    const GuestModuleSpec* spec;uint32_t entry=0,references=0;
    std::map<std::string,uint32_t> exports;  // name or "#ordinal" -> VA
};
std::map<uint32_t,GuestModule> guestModules;  // by base (the HMODULE)

GuestModule* findGuestModule(uint32_t handle){auto found=guestModules.find(handle);return found==guestModules.end()?nullptr:&found->second;}
GuestModule* findGuestModule(const std::string& name){
    for(auto& [base,module]:guestModules)if(module.spec->name==name)return &module;
    return nullptr;
}
bool isGuestModule(uint32_t handle){return findGuestModule(handle)!=nullptr;}
std::string guestModuleFilePath(uint32_t handle){
    if(!handle||handle==imageBase)return "C:\\mp2\\MaxPayne2.exe";
    auto* module=findGuestModule(handle);
    if(!module)return {};
    return std::string("C:\\mp2\\")+module->spec->folder+module->spec->name;
}
uint32_t guestModuleContaining(uint32_t address){for(auto& [base,module]:guestModules)if(address>=base&&address-base<module.spec->imageSize)return base;return 0;}
uint32_t guestModuleHandle(const std::string& name){auto* module=findGuestModule(name);return module?module->spec->base:0;}
bool guestModuleExport(uint32_t handle,const std::string& name,uint32_t& va){
    auto* module=findGuestModule(handle);if(!module)return false;
    auto found=module->exports.find(name);if(found==module->exports.end())return false;
    va=found->second;return true;
}
void applyRelocations(uint32_t base,uint32_t size,uint32_t rva,uint32_t bytes,int64_t delta){
    moduleRange(size,rva,bytes);
    for(uint32_t offset=0;offset+8<=bytes;){
        uint32_t page=read32(base+rva+offset),blockSize=read32(base+rva+offset+4);
        if(blockSize<8||offset+blockSize>bytes)throw GuestStop("Bad base relocation block");
        for(uint32_t entry=8;entry+2<=blockSize;entry+=2){
            uint16_t value;std::memcpy(&value,ptr(base+rva+offset+entry,2),2);
            uint32_t type=value>>12,target=page+(value&0xfff);
            if(type==0)continue;  // IMAGE_REL_BASED_ABSOLUTE padding
            if(type!=3)throw GuestStop("Unsupported base relocation type "+std::to_string(type));
            moduleRange(size,target,4);write32(base+target,uint32_t(int64_t(read32(base+target))+delta));
        }
        offset+=blockSize;
    }
}
// Maps, relocates and binds the DLL, then runs DllMain(PROCESS_ATTACH). Returns the HMODULE or 0.
uint32_t loadGuestModule(const std::string& name){
    if(auto* loaded=findGuestModule(name)){++loaded->references;return loaded->spec->base;}
    const GuestModuleSpec* spec=nullptr;
    for(auto& candidate:guestModuleSpecs)if(name==candidate.name)spec=&candidate;
    if(!spec)return 0;
    if(!spec->lifted){NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSC","%s: this build has no lifted code for it",name.c_str());return 0;}
    auto read=[](const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);
        return stream?std::vector<unsigned char>((std::istreambuf_iterator<char>(stream)),{}):std::vector<unsigned char>{};};
    // Only the build the guest code was lifted from can run: its base, size and entry point.
    auto matches=[&](const std::vector<unsigned char>& candidate){
        if(candidate.size()<0x400)return false;auto header=fileStruct<pe_dos_header>(candidate,0);
        if(header.e_magic!=0x5a4d||header.e_lfanew<0||uint64_t(header.e_lfanew)+sizeof(pe_nt_headers32)>candidate.size())return false;
        auto optional=fileStruct<pe_nt_headers32>(candidate,header.e_lfanew).OptionalHeader;
        return optional.ImageBase==spec->preferredBase&&optional.SizeOfImage==spec->imageSize&&recomp_lookup(spec->base+optional.AddressOfEntryPoint);};
    std::vector<unsigned char> file=read(gamePath(std::string("C:\\mp2\\")+spec->folder+name));
    if(file.empty()){NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSC","%s is missing from the game folder",name.c_str());return 0;}
    auto dos=fileStruct<pe_dos_header>(file,0);
    if(dos.e_magic!=0x5a4d||dos.e_lfanew<0)throw GuestStop(name+": invalid DOS header");
    auto nt=fileStruct<pe_nt_headers32>(file,dos.e_lfanew);auto opt=nt.OptionalHeader;
    if(nt.Signature!=0x4550||nt.FileHeader.Machine!=0x14c||opt.Magic!=0x10b||!(nt.FileHeader.Characteristics&0x2000))
        throw GuestStop(name+": expected an x86 PE32 DLL");
    uint32_t base=spec->base,size=opt.SizeOfImage,entry=base+opt.AddressOfEntryPoint;
    if(!matches(file))
        throw GuestStop(name+": file layout does not match the lifted code (base "+hex(opt.ImageBase)+", size "+hex(size)+")");
    memory->commit(base,size);
    std::memset(ptr(base,size),0,size);
    if(opt.SizeOfHeaders>file.size()||opt.SizeOfHeaders>size)throw GuestStop(name+": truncated PE headers");
    std::memcpy(ptr(base,opt.SizeOfHeaders),file.data(),opt.SizeOfHeaders);
    uint64_t sections=uint64_t(dos.e_lfanew)+24+nt.FileHeader.SizeOfOptionalHeader;
    for(uint32_t i=0;i<nt.FileHeader.NumberOfSections;++i){
        auto section=fileStruct<pe_section_header>(file,sections+i*sizeof(pe_section_header));
        moduleRange(size,section.VirtualAddress,std::max(section.VirtualSize,section.SizeOfRawData));
        if(uint64_t(section.PointerToRawData)+section.SizeOfRawData>file.size())throw GuestStop(name+": truncated PE section");
        std::memcpy(ptr(base+section.VirtualAddress,section.SizeOfRawData),file.data()+section.PointerToRawData,section.SizeOfRawData);
    }
    auto relocations=opt.DataDirectory[PE_DIR_BASERELOC];
    if(!relocations.Size)throw GuestStop(name+": no base relocations to move it to "+hex(base));
    applyRelocations(base,size,relocations.VirtualAddress,relocations.Size,int64_t(base)-int64_t(opt.ImageBase));
    auto tls=opt.DataDirectory[9];
    if(tls.Size){
        moduleRange(size,tls.VirtualAddress,24);uint32_t directory=base+tls.VirtualAddress;
        uint32_t start=read32(directory),end=read32(directory+4),index=read32(directory+8),callbacks=read32(directory+12);
        if(staticTls.size()>=staticTlsSlots)throw GuestStop(name+": too many modules with static TLS");
        if(callbacks&&read32(callbacks))throw GuestStop(name+": callbacks TLS sin implementar");
        if(end<start||end-start>0x10000)throw GuestStop(name+": bad TLS template");
        uint32_t slot=uint32_t(staticTls.size());staticTls.push_back({start,end-start,read32(directory+16)});write32(index,slot);
        attachStaticTls(g_fs_base);
        for(auto& thread:guestThreads)if(thread->tib)attachStaticTls(thread->tib);
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","%s: static TLS %u bytes in slot %u",name.c_str(),end-start,slot);
    }
    size_t ownImports=deferredModuleImports.size();
    bindImports(base,size,opt.DataDirectory[PE_DIR_IMPORT].VirtualAddress,opt.DataDirectory[PE_DIR_IMPORT].Size);
    GuestModule module{spec,entry,1,{}};
    auto exports=opt.DataDirectory[0];
    if(exports.Size){
        moduleRange(size,exports.VirtualAddress,40);uint32_t directory=base+exports.VirtualAddress;
        uint32_t ordinalBase=read32(directory+16),functions=read32(directory+20),names=read32(directory+24);
        uint32_t addressTable=read32(directory+28),nameTable=read32(directory+32),ordinalTable=read32(directory+36);
        moduleRange(size,addressTable,uint64_t(functions)*4);moduleRange(size,nameTable,uint64_t(names)*4);moduleRange(size,ordinalTable,uint64_t(names)*2);
        for(uint32_t i=0;i<functions;++i)if(uint32_t rva=read32(base+addressTable+i*4))module.exports["#"+std::to_string(ordinalBase+i)]=base+rva;
        for(uint32_t i=0;i<names;++i){
            uint16_t index;std::memcpy(&index,ptr(base+ordinalTable+i*2,2),2);
            if(index<functions)module.exports[moduleString(base,size,read32(base+nameTable+i*4))]=base+read32(base+addressTable+index*4u);
        }
    }
    guestModules[base]=module;
    // Its own imports of lifted DLLs (msvcp71 -> msvcr71) before its DllMain.
    bindDeferredModuleImports(ownImports);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","%s mapped at %08x (preferred %08x), %zu exports; running DllMain",name.c_str(),base,opt.ImageBase,module.exports.size());
    if(!callGuest(entry,{base,1,0})){  // DLL_PROCESS_ATTACH
        guestModules.erase(base);
        NFS_RUNTIME_LOG(ANDROID_LOG_WARN,"NFSC","%s: DllMain refused PROCESS_ATTACH",name.c_str());
        return 0;
    }
    return base;
}
// FreeLibrary: the last reference runs DllMain(PROCESS_DETACH); the next load maps a fresh copy.
bool freeGuestModule(uint32_t handle){
    auto* module=findGuestModule(handle);if(!module)return false;
    if(--module->references)return true;
    uint32_t entry=module->entry;
    callGuest(entry,{handle,0,0});  // DLL_PROCESS_DETACH
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","%s unloaded",module->spec->name);
    guestModules.erase(handle);
    return true;
}

// Imports of lifted DLLs recorded by bindImports from index `first` on: map
// each DLL (its DllMain runs here, on the prepared guest machine) and store
// the export VAs. Loading a DLL appends its own imports, so walk by index.
void bindDeferredModuleImports(size_t first){
    size_t bound=0;
    for(size_t i=first;i<deferredModuleImports.size();++i){
        auto item=deferredModuleImports[i];
        uint32_t handle=loadGuestModule(item.dll),va=0;
        if(!handle)throw GuestStop(item.dll+" no está en la carpeta mp2 del teléfono o no coincide con la versión recompilada");
        if(!guestModuleExport(handle,item.name,va))throw GuestStop(item.dll+" has no export "+item.name);
        write32(item.slot,va);++bound;
    }
    deferredModuleImports.resize(first);
    if(bound)NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Bound %zu imports to lifted DLLs",bound);
}

// El WidescreenFix, como lo arranca el cargador ASI en Windows: al primer LoadLibrary
// de D3D, una sola vez. Su hilo parchea el juego unos segundos después; los parches de
// código ya están en la recompilación (config/pc/code_patches.txt) y los de datos se
// aplican en la memoria invitada como en el PC. Sin esto no hay panorámico.
void loadWidescreenFix(){
    static bool loaded=false;if(loaded||!guestWidescreen)return;loaded=true;
    State saved;saved.save();
    if(!loadGuestModule("maxpayne2.widescreenfix.asi"))
        throw GuestStop("WidescreenFix: falta scripts/MaxPayne2.WidescreenFix.asi o no es la versión de 2016");
    saved.load();
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","WidescreenFix cargado");
}
