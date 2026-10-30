#pragma once
#include "iop_sif.h"
#include <algorithm>
#include <functional>
#include <optional>
#include <vector>
namespace mp2r {
// Loads a module requested through LOADFILE; returns its module id, or a negative error.
using IopModuleLoader=std::function<int(IopSif&,const std::string& path,const std::string& name,const std::string& args)>;
using IopModuleStopper=std::function<std::pair<int,int>(int id,const std::string& args)>; // API status, module result
using IopModuleUnloader=std::function<int(int id)>;                                       // id, or KE_* error
// Resident IOP services MP2R provides natively after every IOP (re)boot.
// LOADFILE (sid 0x80000006): module load requests go to the loader. Without one, a
// module is only recorded; the first bind to one of its servers then stops execution.
// Reads a whole file named by an IOP device path ("cdrom0:\DIR\FILE;1"); empty optional if absent.
using IopFileReader=std::function<std::optional<std::vector<uint8_t>>(const std::string& path)>;
inline std::optional<std::string> irxInternalName(const std::vector<uint8_t>& data) {
    auto word=[&](uint64_t at)->std::optional<uint32_t>{
        if(at+4>data.size()) return {}; uint32_t value; std::memcpy(&value,data.data()+at,4); return value;
    };
    if(data.size()<52 || word(0)!=0x464c457fu) return {};
    auto ph=word(28); if(!ph) return {};
    auto info=word(uint64_t(*ph)+4),load=word(uint64_t(*ph)+36); if(!info || !load) return {};
    auto module=word(*info); if(!module || *module==0xffffffffu) return {};
    auto name=word(uint64_t(*load)+*module); if(!name) return {};
    uint64_t start=uint64_t(*load)+*name;
    if(start>=data.size()) return {};
    std::string result;
    for(uint64_t n=start;n<data.size() && n<start+252;++n) {
        if(!data[n]) return result.empty()?std::nullopt:std::optional(result);
        if(data[n]<32 || data[n]>126) return {};
        result+=char(data[n]);
    }
    return {};
}
inline void registerResidentIopServices(IopSif& iop,IopModuleLoader loader={},IopFileReader files={},IopModuleStopper stopper={},IopModuleUnloader unloader={}) {
    // IOP heap (sid 0x80000003, Sony sce_iopmem in IOPRP280 FILEIO): 1 alloc(size), 2 free(addr),
    // 3 load(addr, path), 4 alloc(size, mode, addr), 5 QueryMemSize, 6 QueryMaxFreeMemSize,
    // 7 QueryTotalFreeMemSize, 8 QueryBlockTopAddress(addr), 9 QueryBlockSize(addr).
    iop.addServer(0x80000003,0x200,[files](IopSif& sif,int function,uint32_t buffer,uint32_t) -> uint32_t {
        uint32_t a0=sif.read32(buffer);
        switch(function) {
            case 1: sif.write32(buffer,sif.allocateMode(0,a0)); return buffer;
            case 2: sif.write32(buffer,uint32_t(sif.freeBlock(a0))); return buffer;
            case 4: sif.write32(buffer,sif.allocateMode(int(sif.read32(buffer+4)),a0,sif.read32(buffer+8))); return buffer;
            case 5: sif.write32(buffer,IopSif::ram_size); return buffer;
            case 6: sif.write32(buffer,sif.maxFree()); return buffer;
            case 7: sif.write32(buffer,sif.totalFree()); return buffer;
            case 8: sif.write32(buffer,sif.blockTop(a0)); return buffer;
            case 9: sif.write32(buffer,sif.blockSize(a0)); return buffer;
        }
        if(function==3) {
            uint32_t address=sif.read32(buffer); auto path=sif.readString(buffer+4,252);
            auto data=files?files(path):std::nullopt;
            if(sif.log) *sif.log<<"IOP LoadIopHeap "<<path<<" -> 0x"<<std::hex<<address<<std::dec<<" bytes="<<(data?data->size():0)<<'\n';
            if(!data) { sif.write32(buffer,uint32_t(-203)); return buffer; }        // KE_NOFILE
            std::memcpy(sif.ram(address,uint32_t(data->size())),data->data(),data->size());
            sif.write32(buffer,0); return buffer;
        }
        sif.stop_reason="iop_heap_function_unsupported fno="+std::to_string(function);
        return 0;
    },"IOPHEAP");
    iop.addServer(0x80000006,0x200,[loader,stopper,unloader](IopSif& sif,int function,uint32_t buffer,uint32_t) -> uint32_t {
        // struct _lf_module_load_arg { int arg_len|result; int modres; char path[252]; char args[252]; }
        // LF_F_GET_VERSION: the IOPRP280 LOADFILE replies with its 4-character version string "2800".
        if(function==0xff) { std::memcpy(sif.ram(buffer,4),"2800",4); return buffer; }
        if(function==7) { // LF_F_MOD_STOP: id, argument length, padding, arguments
            int id=int32_t(sif.read32(buffer));
            uint32_t length=std::min<uint32_t>(sif.read32(buffer+4),252);
            std::string args(reinterpret_cast<const char*>(sif.ram(buffer+260,length)),length);
            if(!stopper) { sif.stop_reason="iop_module_stop_without_handler id="+std::to_string(id); return 0; }
            auto [status,result]=stopper(id,args);
            if(sif.log) *sif.log<<"IOP StopModule id="<<id<<" status="<<status<<" module_result="<<result<<'\n';
            sif.write32(buffer,uint32_t(status)); sif.write32(buffer+4,uint32_t(result)); return buffer;
        }
        if(function==8) { // LF_F_MOD_UNLOAD (IOPRP280 LOADFILE -> modload UnloadModule): id in, id or KE_* out
            int id=int32_t(sif.read32(buffer));
            if(!unloader) { sif.stop_reason="iop_module_unload_without_handler id="+std::to_string(id); return 0; }
            int status=unloader(id);
            if(sif.log) *sif.log<<"IOP UnloadModule id="<<id<<" -> "<<status<<'\n';
            if(status>=0) for(auto it=sif.module_ids.begin();it!=sif.module_ids.end();) it=it->second==id?sif.module_ids.erase(it):std::next(it);
            sif.write32(buffer,uint32_t(status)); return buffer;
        }
        if(function==9) { // LF_F_SEARCH_MOD_BY_NAME: name at offset 8; exact match, newest resident id
            auto name=sif.readString(buffer+8,252);
            auto found=sif.module_ids.find(name);
            int id=found==sif.module_ids.end()?-202:found->second; // KE_UNKNOWN_MODULE
            if(sif.log) *sif.log<<"IOP SearchModuleByName "<<name<<" -> "<<id<<'\n';
            sif.write32(buffer,uint32_t(id)); return buffer;
        }
        if(function==0 || function==4) {                         // LF_F_MOD_LOAD, LF_F_MG_MOD_LOAD
            auto path=sif.readString(buffer+8,252);
            auto arg_len=std::min<uint32_t>(sif.read32(buffer),252);
            std::string args(reinterpret_cast<const char*>(sif.ram(buffer+260,arg_len)),arg_len);
            auto slash=path.find_last_of("\\:/");
            auto name=path.substr(slash==std::string::npos?0:slash+1);
            if(auto semi=name.find(';');semi!=std::string::npos) name.resize(semi);
            sif.modules.push_back(name);
            int id=loader?loader(sif,path,name,args):int(sif.modules.size()+1);
            if(sif.log) *sif.log<<"IOP LoadModule "<<path<<" -> id "<<id<<'\n';
            sif.write32(buffer,uint32_t(id));
            sif.write32(buffer+4,0);                           // modres: MODULE_RESIDENT_END
            return buffer;
        }
        sif.stop_reason="iop_loadfile_function_unsupported fno="+std::to_string(function);
        return 0;
    },"LOADFILE");
}
}
