#pragma once
#include "iop_sif.h"
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
namespace mp2r {
// Native FILEIO server (sid 0x80000001) speaking the asynchronous protocol of the FILEIO 2.16
// module in the game's IOPRP280 (analysed from that IRX). Each request carries an EE semaphore,
// a result pointer and size; the RPC itself only answers 1 ("accepted"). On completion the
// module sends SIF command 0x80000011 (opt = buffer index) whose extra data is a result block
// copied to one of two EE buffers announced by the 0xFF handshake, alternating each time:
//   block[0]=semaphore [1]=type [2]=result pointer [3]=result size [4]=result value
//   type 0 open, 1 close, 2 read (0xA4 bytes: +0x14/+0x18 head/tail lengths, +0x1C/+0x20 their
//   EE destinations, +0x24/+0x64 64-byte head/tail data), 3 write, 4 lseek.
// Only the disc device exists: "cdrom0:" opens the extracted files (case-insensitive ISO names),
// other devices report ENODEV. Read data goes straight to EE RAM, so head/tail are always empty.
class IopFileio {
    struct Open { std::ifstream file; uint64_t size{}; std::string path; };
    std::map<int,std::unique_ptr<Open>> files_;
    int next_fd_=3;
    std::array<uint32_t,2> ee_blocks_{};
    unsigned index_{};
public:
    std::function<std::optional<std::filesystem::path>(const std::string& path)> resolve;
    std::ostream* tty{};
    void install(IopSif& iop) {
        files_.clear(); index_=0;
        iop.addServer(0x80000001,0x500,[this](IopSif& sif,int function,uint32_t buffer,uint32_t size) -> uint32_t {
            auto ee=[&sif](uint32_t address){ return sif.eeMemory()+(address&0x1ffffff); };
            auto req=[&](uint32_t offset){ return sif.read32(buffer+offset); };
            if(function==0xff) {                                          // handshake: two EE result buffers
                if(size==8) { ee_blocks_[0]=req(0); ee_blocks_[1]=req(4); }
                else { ee_blocks_[1]=req(0); ee_blocks_[0]=ee_blocks_[1]; }
                std::memcpy(sif.ram(buffer,4),"2800",4); sif.write32(buffer+4,2);
                return buffer;
            }
            std::vector<uint32_t> block(8,0);
            block[0]=req(0); block[2]=req(4); block[3]=req(8);
            int32_t result=0;
            switch(function) {
            case 0: {                                                     // open: mode +0xC, name +0x14
                auto name=sif.readString(buffer+0x14,0x400);
                result=-19;                                               // ENODEV
                if(name.rfind("cdrom0:",0)==0) {
                    auto path=resolve?resolve(name):std::nullopt;
                    if(!path) result=-2;                                  // ENOENT
                    else {
                        auto f=std::make_unique<Open>(); f->file.open(*path,std::ios::binary);
                        f->size=std::filesystem::file_size(*path); f->path=name;
                        result=next_fd_++; files_[result]=std::move(f);
                    }
                }
                if(sif.log) *sif.log<<"FIO open "<<name<<" mode=0x"<<std::hex<<req(0xc)<<std::dec<<" -> "<<result<<'\n';
                block[1]=0; break;
            }
            case 1: result=files_.erase(int(req(0xc)))?0:-9; block[1]=1; break;   // close
            case 2: {                                                     // read: fd +0xC, dest +0x10, size +0x14
                auto it=files_.find(int(req(0xc))); uint32_t dest=req(0x10),count=req(0x14);
                result=-9;                                                // EBADF
                if(it!=files_.end()) {
                    it->second->file.read(reinterpret_cast<char*>(ee(dest)),count);
                    result=int32_t(it->second->file.gcount()); it->second->file.clear();
                }
                block.resize(0xa4/4,0); block[1]=2; block[7]=dest;        // no head/tail remainder
                break;
            }
            case 3: {                                                     // write: only the console exists
                uint32_t fd=req(0xc),src=req(0x10),count=req(0x14);
                if(fd==1 || fd==2) {
                    std::string text(reinterpret_cast<const char*>(ee(src)),count);
                    if(tty) *tty<<"[EE tty] "<<text<<(text.empty() || text.back()!='\n'?"\n":"");
                    result=int32_t(count);
                } else result=-9;
                block[1]=3; break;
            }
            case 4: {                                                     // lseek: fd +0xC, offset +0x10, whence +0x14
                auto it=files_.find(int(req(0xc))); int32_t offset=int32_t(req(0x10)),whence=int32_t(req(0x14));
                if(it==files_.end() || whence<0 || whence>2) result=-9;
                else {
                    auto& f=it->second->file; f.clear();
                    int64_t base=whence==0?0:whence==1?int64_t(f.tellg()):int64_t(it->second->size),target=base+offset;
                    if(target<0) result=-22; else { f.seekg(target); result=int32_t(target); }
                }
                block[1]=4; break;
            }
            default:
                sif.stop_reason="fileio_function_unsupported fno="+std::to_string(function); return 0;
            }
            block[4]=uint32_t(result);
            if(!ee_blocks_[index_]) { sif.stop_reason="fileio_without_handshake"; return 0; }
            std::vector<uint8_t> bytes(block.size()*4); std::memcpy(bytes.data(),block.data(),bytes.size());
            sif.sendSystemCommand(0x80000011,index_,ee_blocks_[index_],std::move(bytes));
            index_^=1;
            sif.write32(buffer,1);                                        // request accepted
            return buffer;
        },"FILEIO");
    }
};
}
