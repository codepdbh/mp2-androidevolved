#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace mp2r {
// Serves 2048-byte disc sectors from the extracted game files plus MP2RDISC.BIN
// (ISO9660 metadata sectors and the LBA of every file, produced from the verified ISO).
class DiscImage {
public:
    struct Extent { uint32_t lba,size; std::string path; };
private:
    std::filesystem::path root_;
    std::vector<Extent> files_;
    std::map<uint32_t,uint32_t> metadata_; // lba -> sector index in the blob
    uint64_t metadata_offset_{};
    uint32_t sectors_{};
    std::ifstream blob_;
    std::map<std::string,std::unique_ptr<std::ifstream>> open_;
    template<class T> static T get(std::ifstream& f) { T v{}; f.read(reinterpret_cast<char*>(&v),sizeof v); return v; }
    std::ifstream& file(const std::string& path) {
        auto& slot=open_[path];
        if(!slot) {
            slot=std::make_unique<std::ifstream>(root_/path,std::ios::binary);
            if(!*slot) throw std::runtime_error("Missing disc file "+path);
        }
        return *slot;
    }
public:
    explicit DiscImage(const std::filesystem::path& root):root_(root),blob_(root/"MP2RDISC.BIN",std::ios::binary) {
        char magic[8]{}; blob_.read(magic,8);
        if(!blob_ || std::memcmp(magic,"MP2RDISC",8)) throw std::runtime_error("Invalid MP2RDISC.BIN");
        auto meta=get<uint32_t>(blob_); sectors_=get<uint32_t>(blob_); auto count=get<uint32_t>(blob_);
        for(uint32_t i=0;i<meta;++i) metadata_[get<uint32_t>(blob_)]=i;
        for(uint32_t i=0;i<count;++i) {
            Extent e{get<uint32_t>(blob_),get<uint32_t>(blob_),{}};
            e.path.resize(get<uint32_t>(blob_)); blob_.read(e.path.data(),e.path.size());
            files_.push_back(std::move(e));
        }
        metadata_offset_=uint64_t(blob_.tellg());
        if(!blob_) throw std::runtime_error("Truncated MP2RDISC.BIN");
    }
    uint32_t sectors() const { return sectors_; }
    const std::vector<Extent>& files() const { return files_; }
    // Reads one sector; bytes past a file's end and unreferenced sectors read as zero.
    void read(uint32_t lba,uint8_t* out) {
        std::memset(out,0,2048);
        if(lba>=sectors_) throw std::runtime_error("Disc LBA out of range");
        if(auto m=metadata_.find(lba);m!=metadata_.end()) {
            blob_.seekg(std::streamoff(metadata_offset_+uint64_t(m->second)*2048)); blob_.read(reinterpret_cast<char*>(out),2048);
            return;
        }
        auto it=std::upper_bound(files_.begin(),files_.end(),lba,[](uint32_t v,const Extent& e){ return v<e.lba; });
        if(it==files_.begin()) return;
        const auto& e=*--it;
        uint64_t offset=uint64_t(lba-e.lba)*2048;
        if(offset>=e.size) return;
        auto& f=file(e.path); f.clear(); f.seekg(std::streamoff(offset));
        f.read(reinterpret_cast<char*>(out),std::streamsize(std::min<uint64_t>(2048,e.size-offset)));
    }
    const Extent* find(const std::string& path) const {
        for(const auto& e:files_) if(e.path==path) return &e;
        return nullptr;
    }
};
}
