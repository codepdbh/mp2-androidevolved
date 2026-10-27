// Private-data check: sectors served from extracted files + MP2RDISC.BIN must equal the ISO.
#include "disc_image.h"
#include <array>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3) { std::cerr<<"Usage: disc_image_probe <extracted dir> <iso>\n"; return 2; }
    try {
        mp2r::DiscImage disc(argv[1]);
        std::ifstream iso(argv[2],std::ios::binary);
        std::array<uint8_t,2048> a{},b{};
        std::vector<uint32_t> lbas;
        for(uint32_t s=0;s<1100;++s) lbas.push_back(s);                       // system area, PVD, directories
        for(const auto& f:disc.files()) { lbas.push_back(f.lba); lbas.push_back(f.lba+(f.size-1)/2048); }
        for(uint32_t x=12345,i=0;i<4000;++i) { x=x*1103515245u+12345u; lbas.push_back(x%disc.sectors()); }
        for(auto lba:lbas) {
            disc.read(lba,a.data());
            iso.seekg(std::streamoff(uint64_t(lba)*2048)); iso.read(reinterpret_cast<char*>(b.data()),2048);
            if(a!=b) { std::cerr<<"Sector mismatch at LBA "<<lba<<'\n'; return 1; }
        }
        std::cout<<"PASS: "<<lbas.size()<<" sectors identical to the ISO (metadata, file boundaries, random sample)\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
