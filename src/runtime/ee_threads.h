#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
namespace mp2r {
class EeThreads {
public:
    enum class State { Dormant,Ready,Running,Waiting };
    enum class Wait { None,Semaphore,Sleep };
    struct Descriptor { uint32_t attributes{},entry{},stack{},size{},gp{}; int priority{}; uint32_t option{}; };
    struct Thread { Descriptor descriptor; State state=State::Dormant; Wait wait=Wait::None; int wait_id{}; uint64_t order{}; int initial_priority{}; };
private:
    std::array<std::optional<Thread>,256> threads_{};
    std::array<int,256> wakeups_{};
    int current_{};
    uint64_t order_{};
public:
    const Thread* get(int id) const { return id>0 && id<256 && threads_[id]?&*threads_[id]:nullptr; }
    int current() const { return current_; }
    void bindMain(Descriptor descriptor) {
        if(current_) throw std::runtime_error("Main thread already bound");
        threads_[1]=Thread{descriptor,State::Running,Wait::None,0,++order_,descriptor.priority}; current_=1;
    }
    int create(Descriptor d) {
        if(!current_ || d.priority<0 || d.priority>=128 || d.entry<0x100000 || d.entry>=0x2000000 ||
           (d.entry&3) || d.stack<0x100000 || (d.stack&15) || d.size<16 ||
           uint64_t(d.stack)+d.size>0x2000000 || d.gp>=0x2000000 || (d.gp&3)) return -1;
        for(int id=2;id<256;++id) if(!threads_[id]) { threads_[id]=Thread{d}; threads_[id]->initial_priority=d.priority; return id; }
        return -1;
    }
    int erase(int id) {
        if(id==current_ || !get(id) || get(id)->state!=State::Dormant) return -1;
        threads_[id].reset(); return 0;
    }
    int start(int id) {
        if(id==current_ || !get(id) || get(id)->state!=State::Dormant) return -1;
        auto& t=*threads_[id]; t.state=State::Ready; t.wait=Wait::None; t.wait_id=0; t.order=++order_; return 0;
    }
    int changePriority(int id,int priority) {
        if(id==0) id=current_;
        if(!get(id) || get(id)->state==State::Dormant || priority<0 || priority>=128) return -1;
        auto& t=*threads_[id]; int previous=t.descriptor.priority; t.descriptor.priority=priority;
        if(previous!=priority && (t.state==State::Ready || t.state==State::Running)) t.order=++order_;
        return previous;
    }
    void block(Wait wait,int id) {
        if(!get(current_) || get(current_)->state!=State::Running || wait==Wait::None)
            throw std::runtime_error("Invalid thread wait transition");
        auto& t=*threads_[current_]; t.state=State::Waiting; t.wait=wait; t.wait_id=id; t.order=++order_;
    }
    int wakeSemaphore(int semaphore,bool by_priority) {
        int chosen=0;
        for(int id=1;id<256;++id) if(get(id)) {
            auto& t=*threads_[id];
            if(t.state!=State::Waiting || t.wait!=Wait::Semaphore || t.wait_id!=semaphore) continue;
            if(!chosen || (by_priority && t.descriptor.priority<get(chosen)->descriptor.priority) ||
               ((!by_priority || t.descriptor.priority==get(chosen)->descriptor.priority) && t.order<get(chosen)->order)) chosen=id;
        }
        if(chosen) { auto& t=*threads_[chosen]; t.state=State::Ready; t.wait=Wait::None; t.wait_id=0; t.order=++order_; }
        return chosen;
    }
    // SleepThread consumes a pending wakeup instead of blocking. Returns true if it blocked.
    bool sleepCurrent() {
        if(!get(current_)) throw std::runtime_error("No current thread");
        if(wakeups_[current_]) { --wakeups_[current_]; return false; }
        block(Wait::Sleep,0); return true;
    }
    // Returns 1 when a sleeping thread became ready, 0 when the wakeup was queued, -1 on error.
    int wakeup(int id) {
        if(!get(id) || get(id)->state==State::Dormant) return -1;
        auto& t=*threads_[id];
        if(t.state==State::Waiting && t.wait==Wait::Sleep) {
            t.state=State::Ready; t.wait=Wait::None; t.order=++order_; return 1;
        }
        ++wakeups_[id]; return 0;
    }
    int cancelWakeups(int id) {
        if(id==0) id=current_;
        if(!get(id)) return -1;
        int previous=wakeups_[id]; wakeups_[id]=0; return previous;
    }
    int wakeups(int id) const { return id>0 && id<256?wakeups_[id]:0; }
    int semaphoreWaiters(int semaphore) const {
        int n=0;
        for(const auto& t:threads_) if(t && t->state==State::Waiting && t->wait==Wait::Semaphore && t->wait_id==semaphore) ++n;
        return n;
    }
    void exitCurrent() {
        if(!get(current_)) throw std::runtime_error("No current thread");
        auto& t=*threads_[current_]; t.state=State::Dormant; t.wait=Wait::None; t.wait_id=0;
    }
    // TerminateThread: another thread goes dormant, leaving any wait it was in.
    int terminate(int id) {
        if(id==current_ || !get(id) || get(id)->state==State::Dormant) return -1;
        auto& t=*threads_[id]; t.state=State::Dormant; t.wait=Wait::None; t.wait_id=0; t.descriptor.priority=t.initial_priority; return 0;
    }
    // ExitDeleteThread: the current thread ends and its slot is freed; the scheduler picks the next.
    void exitDeleteCurrent() {
        if(!get(current_)) throw std::runtime_error("No current thread");
        threads_[current_].reset();
    }
    // Cooperative priority selection: equal priorities keep FIFO queue position.
    int schedule() {
        int chosen=0;
        for(int id=1;id<256;++id) if(get(id)) {
            auto& t=*threads_[id];
            if(t.state!=State::Ready && t.state!=State::Running) continue;
            if(!chosen || t.descriptor.priority<get(chosen)->descriptor.priority ||
               (t.descriptor.priority==get(chosen)->descriptor.priority && t.order<get(chosen)->order)) chosen=id;
        }
        if(!chosen) return 0;
        if(chosen!=current_ && get(current_) && get(current_)->state==State::Running) threads_[current_]->state=State::Ready;
        threads_[chosen]->state=State::Running; current_=chosen; return chosen;
    }
    unsigned size() const { unsigned n=0; for(const auto& t:threads_) if(t) ++n; return n; }
};
}
