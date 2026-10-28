#pragma once
#include "iop_kernel.h"
#include <array>
#include <vector>
namespace mp2r {
// DualShock 2 as seen on the SIO2 bus: the standard pad command protocol (0x40-0x4F),
// digital (0x41), analog (0x73) and pressure (0x79) modes, config mode (0xF3).
class DualShock2 {
public:
    uint16_t buttons=0xffff;                  // active-low, standard bit order
    std::array<uint8_t,4> sticks{0x80,0x80,0x80,0x80};   // RX RY LX LY
    std::array<uint8_t,12> pressure{};        // R L U D Tri Cir Cro Sq L1 R1 L2 R2
    bool analog=false,locked=false,config=false,pressure_mode=false;
    std::array<uint8_t,6> vibration{0xff,0xff,0xff,0xff,0xff,0xff};
    uint8_t mode() const { return config?0xf3:pressure_mode?0x79:analog?0x73:0x41; }
    // tx[0] is the device id (0x01); the reply has the same length.
    std::vector<uint8_t> transfer(const std::vector<uint8_t>& tx) {
        std::vector<uint8_t> rx(tx.size(),0);
        if(tx.size()<2) return rx;
        rx[0]=0xff;
        auto fill=[&](std::initializer_list<uint8_t> data){ size_t i=3; for(auto b:data) if(i<rx.size()) rx[i++]=b; };
        uint8_t cmd=tx[1],arg=tx.size()>3?tx[3]:0;
        if(rx.size()>1) rx[1]=mode();
        if(rx.size()>2) rx[2]=0x5a;
        auto poll=[&]{
            std::vector<uint8_t> d{uint8_t(buttons),uint8_t(buttons>>8)};
            if(analog || pressure_mode) d.insert(d.end(),sticks.begin(),sticks.end());
            if(pressure_mode) d.insert(d.end(),pressure.begin(),pressure.end());
            for(size_t i=0;i<d.size() && i+3<rx.size();++i) rx[i+3]=d[i];
        };
        switch(cmd) {
            case 0x42: poll(); break;
            case 0x43:
                if(config) fill({0,0,0,0,0,0}); else poll();
                config=arg==1; break;
            case 0x44: if(config) { analog=arg==1; locked=tx.size()>4 && tx[4]==3; if(!analog) pressure_mode=false; } fill({0,0,0,0,0,0}); break;
            case 0x45: fill({0x03,0x02,uint8_t(analog?1:0),0x02,0x01,0x00}); break;
            case 0x46: if(arg==0) fill({0x00,0x00,0x01,0x02,0x00,0x0a}); else fill({0x00,0x00,0x01,0x01,0x01,0x14}); break;
            case 0x47: fill({0x00,0x00,0x02,0x00,0x01,0x00}); break;
            case 0x4c: if(arg==0) fill({0x00,0x00,0x00,0x04,0x00,0x00}); else fill({0x00,0x00,0x00,0x07,0x00,0x00}); break;
            case 0x4d:
                for(size_t i=0;i<6 && i+3<rx.size();++i) { rx[i+3]=vibration[i]; if(i+3<tx.size()) vibration[i]=tx[i+3]; }
                break;
            case 0x4f: pressure_mode=true; analog=true; fill({0x00,0x00,0x00,0x00,0x00,0x5a}); break;
            case 0x41: if(analog) fill({0xff,0xff,0x03,0x00,0x00,0x5a}); else fill({0,0,0,0,0,0}); break;
            case 0x40: fill({0x00,0x00,0x02,0x00,0x00,0x5a}); break;
            default: fill({0,0,0,0,0,0}); break;
        }
        return rx;
    }
};

// Native sio2man (v2 API) for IOP pad/memory-card drivers. Transfers follow the SIO2 send3
// descriptors: port bits 0-1, TX length bits 8-16, RX length bits 18-26. Port 0 carries the
// DualShock 2; ports with no device answer 0xFF and report "no response" in stat6c.
class IopSio2 {
public:
    std::array<DualShock2,2> pads;
    std::array<bool,4> connected{true,false,false,false};
    uint64_t transfers{};
    void install(IopKernel& kernel) {
        auto add=[&](int index,IopKernel::Hle fn){ kernel.hle[{"sio2man",index}]=std::move(fn); };
        auto lock=[](IopKernel& k){ k.ret(1); };
        for(int index:{46,47,48,49,50,61,63}) add(index,lock);     // *_transfer_init: exclusive bus access
        add(52,[](IopKernel& k){ k.ret(0); }); add(62,[](IopKernel& k){ k.ret(0); });   // transfer_reset
        add(51,[this](IopKernel& k){ k.ret(transfer(k,k.arg(0))); });                    // sio2_transfer2
        // v1 indices of the same functions (the disc SIO2MAN exports 23-26 at the addresses of 46/47/51/52).
        for(int index:{23,24}) add(index,lock);
        add(25,[this](IopKernel& k){ k.ret(transfer(k,k.arg(0))); });
        add(26,[](IopKernel& k){ k.ret(0); });
        // Multitap hooks with no MTAPMAN registered (SIO2MAN default paths):
        // 57 change_slot(s32 slots[8]): slots[4+i] = (slots[i] is -1 or 0), returns 1 if all were.
        add(57,[](IopKernel& k){
            uint32_t p=k.arg(0),all=1;
            for(uint32_t i=0;i<4;++i) { bool ok=k.read(p+i*4,4)+1u<2u; k.write(p+16+i*4,ok?1:0,4); if(!ok) all=0; }
            k.ret(all);
        });
        add(58,[](IopKernel& k){ k.ret(1); });                                            // get_slot_max: 1
        add(59,[](IopKernel& k){ k.ret(1); });                                            // get_slot_max2: 1
    }
private:
    // sio2_transfer_data_t: stat6c, port_ctrl1[4], port_ctrl2[4], stat70, regdata[16], stat74,
    // in_size, out_size, in, out, in_dma{addr,size,count}, out_dma{addr,size,count}
    uint32_t transfer(IopKernel& k,uint32_t td) {
        auto r=[&](uint32_t o){ return k.read(td+o,4); };
        uint32_t in_size=r(0x6c),out_size=r(0x70),in=r(0x74),out=r(0x78);
        uint32_t in_dma=r(0x7c),in_dma_size=r(0x80)*r(0x84)*4,out_dma=r(0x88),out_dma_size=r(0x8c)*r(0x90)*4;
        std::vector<uint8_t> tx,rx;
        for(uint32_t i=0;i<in_size;++i) tx.push_back(uint8_t(k.read(in+i,1)));
        for(uint32_t i=0;in_dma && i<in_dma_size;++i) tx.push_back(uint8_t(k.read(in_dma+i,1)));
        size_t cursor=0; bool missing=false;
        for(int i=0;i<16;++i) {
            uint32_t reg=r(0x28+i*4); if(!reg) break;
            uint32_t port=reg&3,send=(reg>>8)&0x1ff,recv=(reg>>18)&0x1ff;
            std::vector<uint8_t> chunk(tx.begin()+std::min(cursor,tx.size()),tx.begin()+std::min(cursor+send,tx.size()));
            cursor+=send;
            std::vector<uint8_t> reply;
            if(port<2 && connected[port] && !chunk.empty() && chunk[0]==0x01) reply=pads[port].transfer(chunk);
            else { reply.assign(recv,0xff); missing=true; }
            reply.resize(recv,0xff);
            rx.insert(rx.end(),reply.begin(),reply.end());
        }
        ++transfers;
        k.write(td,missing?0x1d100u:0x1100u,4);            // stat6c: 0x1D100 no response, 0x1100 device answered
        k.write(td+0x24,0xf,4); k.write(td+0x68,0,4);      // stat70, stat74
        size_t pos=0;
        for(uint32_t i=0;i<out_size && pos<rx.size();++i) k.write(out+i,rx[pos++],1);
        for(uint32_t i=0;out_dma && i<out_dma_size && pos<rx.size();++i) k.write(out_dma+i,rx[pos++],1);
        return 1;
    }
};
}
