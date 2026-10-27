#include "iop_modules.h"
#include <iostream>
#include <stdexcept>
#include <vector>
void check(bool value,const char* what) { if(!value) throw std::runtime_error(what); }
using namespace mp2r;
struct Ee {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x200000);
    uint32_t dmac_stat=0;
    uint32_t w(uint32_t a) { uint32_t v; std::memcpy(&v,&ram[a],4); return v; }
    void put(uint32_t a,std::initializer_list<uint32_t> words) { for(auto v:words) { std::memcpy(&ram[a],&v,4); a+=4; } }
    // One SifSetDma descriptor list at 0x1000: optional payload, then the command packet.
    uint32_t send(IopSif& iop,uint32_t packet,uint32_t size,uint32_t data=0,uint32_t data_dest=0,uint32_t data_size=0) {
        uint32_t n=0;
        if(data_size) { put(0x1000,{data,data_dest,data_size,0}); n=1; }
        put(0x1000+n*16,{packet,IopSif::command_buffer,size,0});
        auto id=iop.setDma(ram.data(),0x1000,n+1); iop.pump(ram.data(),dmac_stat); return id;
    }
};
int main() {
    IopSif iop; registerResidentIopServices(iop); iop.on_reboot=[](IopSif& s){ registerResidentIopServices(s); };
    Ee ee;
    check((iop.getReg(4)&(IopSif::SIFINIT|IopSif::CMDINIT|IopSif::BOOTEND))==0x70000 && iop.getReg(2)==IopSif::command_buffer,"boot flags");
    // sceSifInitCmd: INIT_CMD opt=0 gives the EE receive buffer (uncached alias).
    const uint32_t pktbuf=0x2000;
    ee.put(0x3000,{16+4,0,0x80000002,0,0x20000000|pktbuf}); ee.send(iop,0x3000,32);
    // sceSifInitRpc: INIT_CMD opt=1 -> SET_SREG(0,1) arrives through SIF0.
    ee.put(0x3000,{16,0,0x80000002,1}); ee.send(iop,0x3000,16);
    check(ee.dmac_stat==0x20 && ee.w(pktbuf+8)==0x80000001 && ee.w(pktbuf+16)==0 && ee.w(pktbuf+20)==1,"SET_SREG reply");
    // Bind LOADFILE; the reply waits until the EE handler re-arms SIF0 (SifSetDChain).
    ee.put(0x3100,{64,0,0x80000009,0,7,0x3100,9,0x4000,0x80000006}); ee.send(iop,0x3100,64);
    check(ee.w(pktbuf+8)==0x80000001,"SIF0 must not overwrite an unconsumed packet");
    ee.dmac_stat=0; iop.setDChain(ee.ram.data(),ee.dmac_stat);
    check(ee.dmac_stat==0x20 && ee.w(pktbuf+8)==0x80000008 && ee.w(pktbuf+28)==0x4000 && ee.w(pktbuf+32)==0x80000009,"bind end");
    uint32_t sd=ee.w(pktbuf+36),server_buffer=ee.w(pktbuf+40);
    check(sd && server_buffer,"server token/buffer");
    // Call LF_F_MOD_LOAD: payload goes to the server buffer, 8-byte result to the EE recvbuf.
    const char path[]="cdrom0:\\MODULES1\\RWA.IRX;1";
    ee.put(0x5000,{0,0}); std::memcpy(&ee.ram[0x5008],path,sizeof path);
    ee.put(0x3200,{64,0,0x8000000a,0,8,0x3200,10,0x4000,0,512,0x6000,8,1,sd});
    ee.dmac_stat=0; iop.setDChain(ee.ram.data(),ee.dmac_stat);
    ee.send(iop,0x3200,64,0x5000,server_buffer,512);
    check(ee.w(pktbuf+32)==0x8000000a && ee.w(0x6000)==2 && ee.w(0x6004)==0 && iop.modules.at(0)=="RWA.IRX","module load");
    // Unknown server: explicit stop reason, no reply.
    ee.dmac_stat=0; iop.setDChain(ee.ram.data(),ee.dmac_stat);
    ee.put(0x3300,{64,0,0x80000009,0,11,0x3300,12,0x4100,0x12345678}); ee.send(iop,0x3300,64);
    check(iop.stop_reason=="iop_rpc_server_missing sid=0x12345678" && ee.dmac_stat==0,"missing server");
    iop.stop_reason.clear();
    // Reset: flags cleared by the EE stay clear until the EE polls SMFLAG after the reboot.
    ee.put(0x3400,{112,0,0x80000003,0,26,0}); std::memcpy(&ee.ram[0x3418],"rom0:UDNL cdrom0:\\IOPRP280",26);
    iop.setReg(4,IopSif::BOOTEND); ee.send(iop,0x3400,112); iop.setReg(4,IopSif::SIFINIT|IopSif::CMDINIT);
    check(iop.getReg(4)&IopSif::BOOTEND,"reboot completes on SMFLAG poll");
    check(iop.reboot_image=="rom0:UDNL cdrom0:\\IOPRP280" && iop.hasServer(0x80000006),"resident services after reboot");
    std::cout<<"PASS: SIF init/sreg/bind/call/SIF0 queueing/missing server/IOP reset protocol\n";
}
