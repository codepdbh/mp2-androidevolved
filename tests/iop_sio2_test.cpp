#include "iop_sio2.h"
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
int main() {
    mp2r::IopSif sif; mp2r::IopKernel kernel(sif); mp2r::IopSio2 sio2;
    sio2.install(kernel);
    auto& pad=sio2.pads[0];
    pad.transfer({1,0x43,0,1,0});
    pad.transfer({1,0x44,0,1,3,0,0,0,0});
    pad.transfer({1,0x4f,0,0xff,0xff,3,0,0,0});
    pad.transfer({1,0x43,0,0,0,0,0,0,0});
    check(pad.mode()==0x79,"DualShock2 native pressure mode");
    bool press=true; unsigned samples=0;
    sio2.sample_input=[&](unsigned port,mp2r::DualShock2& target){
        check(port==0,"input sampled for wrong port"); ++samples;
        target.buttons=press?0xbfff:0xffff;
        target.sticks={30,90,128,200}; target.pressure.fill(0); target.pressure[6]=press?255:0;
    };
    constexpr uint32_t td=0x1000,tx=0x2000,rx=0x3000;
    for(unsigned i=0;i<21;++i) kernel.write(tx+i,i==0?1:i==1?0x42:0,1);
    kernel.write(td+0x6c,21,4); kernel.write(td+0x70,21,4);
    kernel.write(td+0x74,tx,4); kernel.write(td+0x78,rx,4);
    auto poll=[&](unsigned port){
        kernel.write(td+0x28,port|(21<<8)|(21<<18),4);
        kernel.cpu.r[4]=td; kernel.hle.at({"sio2man",51})(kernel);
        check(kernel.cpu.r[2]==1,"SIO2 transfer completion");
    };
    poll(0);
    check(samples==1 && kernel.read(td,4)==0x1100,"connected device status");
    check(kernel.read(rx+1,1)==0x79 && kernel.read(rx+2,1)==0x5a,"pad reply header");
    check(kernel.read(rx+3,2)==0xbfff && kernel.read(rx+15,1)==255,"Cross active-low and pressure");
    check(kernel.read(rx+5,1)==30 && kernel.read(rx+8,1)==200,"analog axes");
    press=false; poll(0);
    check(samples==2 && kernel.read(rx+3,2)==0xffff && kernel.read(rx+15,1)==0,"release reaches SIO2 reply");
    poll(1);
    check(samples==2 && kernel.read(td,4)==0x1d100 && kernel.read(rx+1,1)==0xff,"absent device does not sample input");
    check(sio2.pad_polls[0]==2 && sio2.pad_polls[1]==0,"per-port poll counters");
    std::cout<<"PASS: DualShock2 negotiation, SIO2 input sampling/press/release/axes/pressure and absent port\n";
}
