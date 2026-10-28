#pragma once
#include "disc_image.h"
#include "iop_sif.h"
#include <cctype>
#include <ctime>
#include <functional>
namespace mp2r {
// Native cdvdfsv (the EE libcdvd RPC servers) over the verified disc image. Version numbers
// are the ones of CDVDFSV/CDVDMAN 2.35 shipped in the game's IOPRP280.IMG. Reads complete
// synchronously: sector data goes straight to EE RAM, so no unaligned remainder is reported.
// Packet layouts: PS2SDK common/include/libcdvd-rpc.h (protocol-compatible definitions).
// stream: forwards NCMD 9/10 to the cdrom_stm0: devctl of CDVDSTM (cmd, packet) -> retres.
inline void registerCdvdServices(IopSif& iop,DiscImage* disc,std::function<int32_t(uint32_t cmd,uint32_t packet,uint32_t size)> stream={}) {
    constexpr uint32_t version=0x0223;
    auto ee=[&iop](uint32_t address){ return iop.eeMemory()+(address&0x1ffffff); };
    auto put=[ee](uint32_t address,uint32_t value){ std::memcpy(ee(address),&value,4); };
    iop.addServer(0x80000592,0x40,[](IopSif& sif,int,uint32_t buffer,uint32_t) -> uint32_t {  // sceCdInit
        sif.write32(buffer,1); sif.write32(buffer+4,version); sif.write32(buffer+8,version); sif.write32(buffer+12,0);
        return buffer;
    },"CDVDFSV");
    for(uint32_t sid:{0x8000059Au,0x8000059Cu})
        iop.addServer(sid,0x40,[](IopSif& sif,int,uint32_t buffer,uint32_t){ sif.write32(buffer,2); return buffer; },"CDVDFSV"); // SCECdComplete
    iop.addServer(0x80000593,0x100,[](IopSif& sif,int function,uint32_t buffer,uint32_t) -> uint32_t {
        auto bcd=[](int v){ return uint8_t((v/10)<<4|(v%10)); };
        switch(function) {
            case 0x01: {                                   // ReadClock: {retres, stat sec min hour pad day month year} BCD
                std::time_t now=std::time(nullptr); std::tm t{};
#ifdef _WIN32
                localtime_s(&t,&now);
#else
                localtime_r(&now,&t);
#endif
                uint8_t clock[8]={0,bcd(t.tm_sec),bcd(t.tm_min),bcd(t.tm_hour),0,bcd(t.tm_mday),bcd(t.tm_mon+1),bcd(t.tm_year%100)};
                sif.write32(buffer,1); std::memcpy(sif.ram(buffer+4,8),clock,8); return buffer;
            }
            case 0x03: sif.write32(buffer,0x14); return buffer;   // GetDiskType: SCECdPS2DVD
            case 0x04: sif.write32(buffer,0); return buffer;      // GetError: SCECdErNO
            case 0x05: sif.write32(buffer,1); sif.write32(buffer+4,0); return buffer; // TrayReq: tray never moved
            case 0x0c: sif.write32(buffer,0x0a); return buffer;   // Status: SCECdStatPause (spun up, idle)
            case 0x16: case 0x1d: case 0x22: case 0x23:           // Break / SpinCtrl / Mmode / SetThreadPri: retres 1
                sif.write32(buffer,1); return buffer;
            default: sif.stop_reason="cdvd_scmd_unsupported fno="+std::to_string(function); return 0;
        }
    },"CDVDFSV");
    iop.addServer(0x80000595,0x100,[disc,ee,put,stream](IopSif& sif,int function,uint32_t buffer,uint32_t size) -> uint32_t {
        switch(function) {
            case 0x01: case 0x03: {                        // Read / DvdRead {lbn, sectors, eebuf, mode, align data, &curReadPos}
                if(!disc) { sif.stop_reason="cdvd_read_without_disc"; return 0; }
                uint32_t lbn=sif.read32(buffer),count=sif.read32(buffer+4),dest=sif.read32(buffer+8),mode=sif.read32(buffer+12);
                if(((mode>>16)&0xff)!=0) { sif.stop_reason="cdvd_read_non_2048_sectors"; return 0; }
                for(uint32_t i=0;i<count;++i) disc->read(lbn+i,ee(dest+i*2048));
                if(uint32_t align=sif.read32(buffer+16)) std::memset(ee(align),0,16); // no unaligned remainder
                if(uint32_t pos=sif.read32(buffer+20)) put(pos,count*2048);
                sif.write32(buffer,1); return buffer;
            }
            case 0x05: case 0x06: case 0x07: case 0x08:    // Seek / Standby / Stop / Pause
                sif.write32(buffer,1); return buffer;
            case 0x0e: sif.write32(buffer,2); return buffer;  // DiskReady
            case 0x09: case 0x0a:                          // Stream / CddaStream -> devctl("cdrom_stm0:", 0x4396/0x4398)
                if(!stream) { sif.stop_reason="cdvd_stream_without_cdvdstm"; return 0; }
                sif.write32(buffer,uint32_t(stream(function==9?0x4396:0x4398,buffer,size))); return buffer;
            default: sif.stop_reason="cdvd_ncmd_unsupported fno="+std::to_string(function); return 0;
        }
    },"CDVDFSV");
    iop.addServer(0x80000597,0x200,[disc,ee](IopSif& sif,int,uint32_t buffer,uint32_t size) -> uint32_t { // sceCdSearchFile
        // Layouts by size: 0x124 {file, path[256], eedest}, 0x128 adds attributes, 0x12c adds layer.
        uint32_t path_offset=size==0x124?32:36,dest_offset=path_offset+256;
        auto path=sif.readString(buffer+path_offset,256);
        std::string key; for(char c:path) { if(c==';') break; key+=char(c=='\\'?'/':std::toupper(static_cast<unsigned char>(c))); }
        while(!key.empty() && key.front()=='/') key.erase(0,1);
        const DiscImage::Extent* found=nullptr;
        if(disc) for(const auto& e:disc->files()) {
            std::string upper; for(char c:e.path) upper+=char(std::toupper(static_cast<unsigned char>(c)));
            if(upper==key) { found=&e; break; }
        }
        if(sif.log) *sif.log<<"CD SearchFile "<<path<<(found?" found":" not found")<<'\n';
        if(found) {
            uint8_t file[32]{};                            // sceCdlFILE {lsn, size, name[16], date[8]}
            std::memcpy(file,&found->lba,4); std::memcpy(file+4,&found->size,4);
            auto name=found->path.substr(found->path.find_last_of('/')+1)+";1";
            std::memcpy(file+8,name.c_str(),std::min<size_t>(name.size(),15));
            std::memcpy(ee(sif.read32(buffer+dest_offset)),file,32);
        }
        sif.write32(buffer,found?1:0); return buffer;
    },"CDVDFSV");
}
}
