struct ResourceRecord {uint32_t address,size;};
std::map<uint32_t,ResourceRecord> resourceHandles;
std::map<uint32_t,uint32_t> loadedResources;
// Directorio de recursos del módulo actual: MFC los busca en el EXE y en sus DLL.
uint32_t resourceModule=0,resourceDirectory=0,resourceDirectoryBytes=0,resourceImageBytes=0;
bool selectResourceModule(uint32_t handle){
    uint32_t base=handle?handle:imageBase;
    if(base!=imageBase&&!isGuestModule(base)){lastError=1812;return false;}
    uint32_t pe=base+read32(base+0x3c);
    resourceModule=base;resourceImageBytes=read32(pe+80);resourceDirectory=read32(pe+24+96+2*8);resourceDirectoryBytes=read32(pe+24+96+2*8+4);
    if(!resourceDirectory||!resourceDirectoryBytes){lastError=1812;return false;}
    return true;
}
void* resourcePointer(uint32_t offset,uint32_t length){
    if(uint64_t(offset)+length>resourceDirectoryBytes||uint64_t(resourceDirectory)+offset+length>resourceImageBytes)throw GuestStop("PE resource directory bounds exceeded");
    return ptr(resourceModule+resourceDirectory+offset,length);
}
uint16_t resourceWord(uint32_t offset){uint16_t value;std::memcpy(&value,resourcePointer(offset,2),2);return value;}
uint32_t resourceDword(uint32_t offset){uint32_t value;std::memcpy(&value,resourcePointer(offset,4),4);return value;}
uint32_t resourceChild(uint32_t directory,uint32_t requested,bool first=false){
    uint32_t count=resourceWord(directory+12)+resourceWord(directory+14);
    bool named=requested>0xffff;std::string target=named?lower(guestString(requested)):std::string{};
    for(uint32_t i=0;i<count;++i){uint32_t entryOffset=directory+16+i*8,name=resourceDword(entryOffset),child=resourceDword(entryOffset+4);
        bool match=first;
        if(!match&&bool(name&0x80000000u)==named){
            if(named){uint32_t nameOffset=name&0x7fffffffu,length=resourceWord(nameOffset);std::string text;
                for(uint32_t c=0;c<length;++c){uint16_t wide=resourceWord(nameOffset+2+c*2);int byte=encodeByte(1252,wide);text.push_back(byte<0?'?':static_cast<char>(byte));}match=lower(text)==target;
            }else match=(name&0xffff)==requested;
        }
        if(match)return child;
    }
    return UINT32_MAX;
}
uint32_t locateResource(uint32_t module,uint32_t name,uint32_t type,uint32_t language){
    if(!selectResourceModule(module))return 0;
    uint32_t child=resourceChild(0,type);if(child==UINT32_MAX){lastError=1813;return 0;}if(!(child&0x80000000u))throw GuestStop("Invalid PE resource type level");
    child=resourceChild(child&0x7fffffffu,name);if(child==UINT32_MAX){lastError=1814;return 0;}if(!(child&0x80000000u))throw GuestStop("Invalid PE resource name level");
    uint32_t languages=child&0x7fffffffu;child=resourceChild(languages,language);
    if(child==UINT32_MAX)child=resourceChild(languages,0);if(child==UINT32_MAX)child=resourceChild(languages,0,true);
    if(child==UINT32_MAX){lastError=1815;return 0;}if(child&0x80000000u)throw GuestStop("Unexpected PE resource depth");
    uint32_t dataRva=resourceDword(child),size=resourceDword(child+4);if(uint64_t(dataRva)+size>resourceImageBytes)throw GuestStop("PE resource data bounds exceeded");
    uint32_t handle=resourceModule+resourceDirectory+child;resourceHandles.emplace(handle,ResourceRecord{resourceModule+dataRva,size});return handle;
}
void findResource(){ret(locateResource(arg(0),arg(1),arg(2),0x409),3);}
void findResourceEx(){ret(locateResource(arg(0),arg(2),arg(1),arg(3)),4);}
void loadResource(){auto found=resourceHandles.find(arg(1));if(found==resourceHandles.end()){lastError=6;ret(0,2);return;}loadedResources.emplace(found->second.address,found->second.size);ret(found->second.address,2);}
void resourceSizeShim(){auto found=resourceHandles.find(arg(1));if(found==resourceHandles.end()){lastError=6;ret(0,2);return;}ret(found->second.size,2);}
void lockResource(){ret(arg(0),1);}
void freeResource(){ret(0,1);} // PE resources remain owned by the module, as on Win32.
const std::map<std::string,recomp_func_t> resourceFunctions={
    {"FindResourceA",findResource},{"FindResourceExA",findResourceEx},{"LoadResource",loadResource},
    {"SizeofResource",resourceSizeShim},{"LockResource",lockResource},{"FreeResource",freeResource}
};
