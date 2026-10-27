#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace mp2r {
struct EeSemaphore {
    int32_t count{},maximum{},initial{};
    uint32_t attributes{},option{};
};
// EE descriptor offsets are count/max/init/waiters/attributes/option.
// They differ from the IOP descriptor. Wait queues live in EeThreads.
class EeSemaphores {
    std::array<std::optional<EeSemaphore>,256> objects_{};
public:
    int create(int32_t initial,int32_t maximum,uint32_t attributes,uint32_t option) {
        if(maximum<=0 || initial<0 || initial>maximum) return -1;
        for(unsigned i=0;i<objects_.size();++i) if(!objects_[i]) {
            objects_[i]=EeSemaphore{initial,maximum,initial,attributes,option};
            return static_cast<int>(i+1);
        }
        return -1;
    }
    const EeSemaphore* find(int id) const {
        return id>0 && id<=256 && objects_[id-1]?&*objects_[id-1]:nullptr;
    }
    int poll(int id) {
        if(!find(id) || objects_[id-1]->count==0) return -1;
        --objects_[id-1]->count; return id;
    }
    int signal(int id) {
        if(!find(id) || objects_[id-1]->count==objects_[id-1]->maximum) return -1;
        ++objects_[id-1]->count; return id;
    }
    int erase(int id) {
        if(!find(id)) return -1;
        objects_[id-1].reset(); return id;
    }
    unsigned size() const {
        unsigned result=0; for(const auto& o:objects_) if(o) ++result; return result;
    }
};
}
