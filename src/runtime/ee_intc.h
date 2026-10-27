#pragma once
#include <array>
#include <cstdint>
namespace mp2r {
// Native registration/state. Delivery is an explicit executor responsibility.
class EeIntc {
public:
    struct Handler { int cause{}; uint32_t entry{},argument{}; int next{}; };
private:
    std::array<Handler,65> handlers_{};
    std::array<int,15> heads_{};
    uint32_t status_{},mask_{};
public:
    int add(int cause,uint32_t entry,int before,uint32_t argument) {
        if(cause<0 || cause>=15 || !entry || (entry&3) || before< -1 || before>=65) return -1;
        if(before>0 && (handlers_[before].entry==0 || handlers_[before].cause!=cause)) return -1;
        int id=1; while(id<65 && handlers_[id].entry) ++id;
        if(id==65) return -1;
        int* link=&heads_[cause];
        if(before==-1) { while(*link) link=&handlers_[*link].next; }
        else if(before>0) { while(*link && *link!=before) link=&handlers_[*link].next; }
        handlers_[id]={cause,entry,argument,*link}; *link=id; return id;
    }
    int remove(int cause,int id) {
        if(cause<0 || cause>=15 || id<=0 || id>=65 || !handlers_[id].entry || handlers_[id].cause!=cause) return -1;
        int* link=&heads_[cause]; while(*link && *link!=id) link=&handlers_[*link].next;
        if(!*link) return -1;
        *link=handlers_[id].next; handlers_[id]={}; return id;
    }
    const Handler* get(int id) const { return id>0 && id<65 && handlers_[id].entry?&handlers_[id]:nullptr; }
    int head(int cause) const { return cause>=0 && cause<15?heads_[cause]:0; }
    uint32_t status() const { return status_; }
    uint32_t mask() const { return mask_; }
    uint32_t pending() const { return status_&mask_; }
    void raise(unsigned cause) { if(cause<15) status_|=1u<<cause; }
    void acknowledge(uint32_t bits) { status_&=~(bits&0x7fff); }
    void toggleMask(uint32_t bits) { mask_^=bits&0x7fff; }
};
}
