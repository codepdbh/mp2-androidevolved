#include <rw.h>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

int main(int argc,char** argv) {
    if(argc!=3) { std::cerr<<"Usage: rw_texture_probe LEVEL.IMG output.tga\n"; return 2; }
    const bool scan=std::string(argv[2])=="--scan";
    std::ifstream file(argv[1],std::ios::binary);
    if(!rw::Engine::init() || !rw::Engine::open(nullptr) || !rw::Engine::start()) return 5;
    unsigned dictionaries=0;
    std::streamoff offset=0;
    do {
    file.seekg(offset);
    uint32_t header[3]{}; file.read(reinterpret_cast<char*>(header),12);
    if(scan && file && header[0]!=rw::ID_TEXDICTIONARY) break;
    if(!file || header[0]!=rw::ID_TEXDICTIONARY || header[1]>32*1024*1024) return 3;
    std::vector<uint8_t> bytes(12+header[1]);
    file.seekg(offset); file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    if(!file) return 4;
    rw::StreamMemory stream; stream.open(bytes.data(),static_cast<uint32_t>(bytes.size()));
    rw::ChunkHeaderInfo h; rw::readChunkHeaderInfo(&stream,&h);
    auto* dict=rw::TexDictionary::streamRead(&stream);
    if(!dict || dict->count()!=1) { std::cerr<<"librw failed to load first TXD\n"; return 6; }
    auto* texture=rw::Texture::fromDict(dict->textures.link.next);
    auto* image=texture->raster->toImage();
    if(!image) { std::cerr<<"PS2 raster conversion failed\n"; return 7; }
    std::cout<<"offset="<<offset<<" texture="<<texture->name<<" raster_platform="<<texture->raster->platform
             <<" width="<<image->width<<" height="<<image->height<<" depth="<<image->depth
             <<" stride="<<image->stride<<"\n";
    if(!scan) { image->convertTo32(); rw::writeTGA(image,argv[2]); }
    image->destroy(); dict->destroy();
    ++dictionaries; offset+=bytes.size();
    } while(scan && dictionaries<4096);
    rw::Engine::stop(); rw::Engine::close(); rw::Engine::term();
    std::cout<<"decoded_dictionaries="<<dictionaries<<" consumed_bytes="<<offset<<"\n";
    return scan || std::ifstream(argv[2],std::ios::binary).good()?0:8;
}
