#include "ee_dmac.h"
#include "ee_ipu.h"
#include <iostream>
#include <stdexcept>
#include <vector>
void check(bool value,const char* what) { if(!value) throw std::runtime_error(what); }
int run() {
    mp2r::EeIpu ipu;
    // BCLR sets the bit pointer of the data that follows; FDEC shows 32 bits from there.
    ipu.command(0x00000004);
    uint8_t q[16]={0x0f,0x00,0x00,0x01,0xb3,0x28,0x01,0x30,0,0,0,0,0,0,0,0};
    ipu.inputBytes(q,16);
    ipu.command(0x40000000);
    check(!ipu.busy && ipu.readCmd()==0xf000001bu,"BCLR bit pointer honoured by FDEC");
    // The 64-bit IPU_CMD read keeps BUSY in bit 63 apart from data bit 31.
    ipu.command(0x40000004);
    check(ipu.readCmd()==0x000001b3u && !(ipu.readCmd()>>63),"FDEC skips 4 bits");
    ipu.command(0x40000008);
    check(ipu.readCmd()==0x0001b328u,"FDEC 8");
    ipu.command(0x40000008);
    check(ipu.readCmd()==0x01b32801u,"FDEC 8 again");
    ipu.command(0x40000008);
    check(ipu.readCmd()==0xb3280130u && !(ipu.readCmd()>>63),"data with bit 31 set is not BUSY");
    // IPU_BP: BP in the first buffered qword, FP qwords in the decoder buffer.
    check(ipu.readBp()==(32u|(1u<<16)),"BP/IFC/FP");
    // An FDEC past the queued input stays busy until more arrives.
    ipu.command(0x40000020); ipu.command(0x40000020);            // bits 64 and 96: still in the qword
    ipu.command(0x40000020);                                      // bit 128 needs the next qword
    check(ipu.busy && (ipu.readCmd()>>63),"FDEC waits for input");
    ipu.inputBytes(q,16);
    check(!ipu.busy,"FDEC completes once input arrives");

    // CSC of one macroblock (384 bytes = 24 qwords) through a flow-controlled IPU_TO channel.
    ipu.reset(); ipu.command(0x00000000);
    std::vector<uint8_t> ram(0x2000000),spr(0x4000);
    mp2r::EeDmac dmac; dmac.ram=ram.data(); dmac.scratchpad=spr.data(); dmac.ctrl=1;
    dmac.sinks[4]=[&](const uint8_t* d,uint32_t count,uint32_t,uint32_t,bool has_tag){ if(!has_tag) ipu.inputBytes(d,count*16); };
    dmac.capacity[4]=[&]{ return ipu.inputRoom(); };
    for(int i=0;i<256;++i) ram[0x1000+i]=128;                     // Y
    for(int i=0;i<128;++i) ram[0x1100+i]=128;                     // Cb, Cr
    for(int i=0;i<384;++i) ram[0x1180+i]=16;                      // second macroblock
    dmac.write(0x1000b410,0x1000); dmac.write(0x1000b420,48);
    int done=dmac.write(0x1000b400,0x101);                        // STR, normal, to IPU
    check(done==-1 && ipu.queuedQwords()==10,"IPU_TO stops at FIFO depth while idle");
    ipu.command(0x70000002);                                      // CSC, 2 macroblocks, RGBA32
    for(int guard=0;guard<100 && done<0;++guard) done=dmac.pumpTo(4);
    check(done==4 && !ipu.busy,"CSC drains the whole transfer");
    check(ipu.output.size()==2*256*4,"two RGBA32 macroblocks");
    check(ipu.output[3]==0x80 && ipu.output[0]==ipu.output[1],"grey pixel, alpha 0x80");
    std::cout<<"IPU FIFO, FDEC, BP and CSC flow control verified\n";
    return 0;
}
int main() {
    try { return run(); }
    catch(const std::exception& e) { std::cerr<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
