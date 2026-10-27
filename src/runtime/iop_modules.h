#pragma once
#include "iop_sif.h"
#include <algorithm>
#include <functional>
namespace mp2r {
// Loads a module requested through LOADFILE; returns its module id, or a negative error.
using IopModuleLoader=std::function<int(IopSif&,const std::string& path,const std::string& name,const std::string& args)>;
// Resident IOP services MP2R provides natively after every IOP (re)boot.
// LOADFILE (sid 0x80000006): module load requests go to the loader. Without one, a
// module is only recorded; the first bind to one of its servers then stops execution.
inline void registerResidentIopServices(IopSif& iop,IopModuleLoader loader={}) {
    iop.addServer(0x80000006,0x200,[loader](IopSif& sif,int function,uint32_t buffer,uint32_t) -> uint32_t {
        // struct _lf_module_load_arg { int arg_len|result; int modres; char path[252]; char args[252]; }
        if(function==0 || function==6) {                         // LF_F_MOD_LOAD, LF_F_MG_MOD_LOAD
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
