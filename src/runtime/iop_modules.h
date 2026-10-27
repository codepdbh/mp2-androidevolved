#pragma once
#include "iop_sif.h"
#include <algorithm>
namespace mp2r {
// Resident IOP services MP2R provides natively after every IOP (re)boot.
// LOADFILE (sid 0x80000006): module load requests are recorded by name. A module
// whose RPC servers MP2R does not implement is accepted, but the first bind to one of
// its servers stops execution with the server ID (see IopSif::command).
inline void registerResidentIopServices(IopSif& iop) {
    iop.addServer(0x80000006,0x200,[](IopSif& sif,int function,uint32_t buffer,uint32_t) -> uint32_t {
        // struct _lf_module_load_arg { int arg_len|result; int modres; char path[252]; char args[252]; }
        if(function==0 || function==6) {                         // LF_F_MOD_LOAD, LF_F_MG_MOD_LOAD
            auto path=sif.readString(buffer+8,252);
            auto slash=path.find_last_of("\\:/");
            auto name=path.substr(slash==std::string::npos?0:slash+1);
            if(auto semi=name.find(';');semi!=std::string::npos) name.resize(semi);
            sif.modules.push_back(name);
            if(sif.log) *sif.log<<"IOP LoadModule "<<path<<" -> id "<<sif.modules.size()+1<<'\n';
            sif.write32(buffer,uint32_t(sif.modules.size()+1)); // module id
            sif.write32(buffer+4,0);                           // modres: MODULE_RESIDENT_END
            return buffer;
        }
        sif.stop_reason="iop_loadfile_function_unsupported fno="+std::to_string(function);
        return 0;
    },"LOADFILE");
}
}
