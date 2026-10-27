#include "ee_threads.h"
#include "ee_semaphores.h"
#include <iostream>
#include <stdexcept>
using Threads=mp2r::EeThreads;
void check(bool value) { if(!value) throw std::runtime_error("EE thread scheduler contract mismatch"); }
int main() {
    Threads threads;
    Threads::Descriptor d{0,0x10e2e0,0x5c1c80,0x400,0x5c8870,0,7};
    check(threads.create(d)==-1); // main context must exist first
    threads.bindMain({0,0x100008,0x1fc0000,0x40000,0x5c8870,0,0});
    auto invalid=d; invalid.size=0xffffffff; check(threads.create(invalid)==-1);
    invalid=d; invalid.stack=0x5c1c81; check(threads.create(invalid)==-1);
    invalid=d; invalid.priority=128; check(threads.create(invalid)==-1);
    invalid=d; invalid.entry=0x100001; check(threads.create(invalid)==-1);
    int child=threads.create(d); check(child==2 && threads.get(child)->state==Threads::State::Dormant);
    check(threads.changePriority(child,3)==-1 && threads.start(1)==-1);
    check(threads.start(child)==0 && threads.start(child)==-1 && threads.schedule()==1);
    check(threads.erase(child)==-1);
    check(threads.changePriority(0,1)==0 && threads.schedule()==child);
    mp2r::EeSemaphores semaphores;
    int sema=semaphores.create(0,1,0,0);
    check(semaphores.poll(sema)==-1);
    threads.block(Threads::Wait::Semaphore,sema); check(threads.schedule()==1);
    check(threads.wakeSemaphore(sema,false)==child && semaphores.find(sema)->count==0);
    check(threads.schedule()==child); // direct handoff, no count increment
    check(threads.wakeSemaphore(sema,false)==0);
    check(semaphores.signal(sema)==sema && semaphores.signal(sema)==-1);
    check(semaphores.poll(sema)==sema && semaphores.poll(sema)==-1);
    threads.exitCurrent(); check(threads.schedule()==1 && threads.erase(child)==0 && !threads.get(child));
    check(threads.create(d)==child && threads.start(child)==0 && threads.schedule()==child);
    threads.block(Threads::Wait::Semaphore,sema); check(threads.schedule()==1);
    threads.block(Threads::Wait::Semaphore,sema); check(threads.schedule()==0);
    check(threads.wakeSemaphore(sema,false)==child && threads.schedule()==child); // FIFO
    check(threads.wakeSemaphore(sema,false)==1);
    Threads priorities;
    priorities.bindMain({0,0x100008,0x1fc0000,0x40000,0x5c8870,10,0});
    auto low=d; low.priority=5; int a=priorities.create(low); int b=priorities.create(d);
    check(priorities.start(a)==0 && priorities.schedule()==a);
    priorities.block(Threads::Wait::Semaphore,7); check(priorities.schedule()==1);
    check(priorities.start(b)==0 && priorities.schedule()==b);
    priorities.block(Threads::Wait::Semaphore,7); check(priorities.schedule()==1);
    check(priorities.wakeSemaphore(7,true)==b && priorities.wakeSemaphore(7,true)==a);
    {
        Threads t; t.bindMain({0,0x100008,0x1fc0000,0x40000,0x5c8870,1,0});
        int worker=t.create({0,0x10e2e0,0x5c1c80,0x400,0x5c8870,0,0}); check(worker>0 && t.start(worker)==0);
        check(t.wakeup(worker)==0);                 // early wakeup is queued
        check(t.schedule()==worker);
        check(!t.sleepCurrent() && t.get(worker)->state==Threads::State::Running); // consumed, no block
        check(t.sleepCurrent() && t.get(worker)->state==Threads::State::Waiting);
        check(t.schedule()==1 && t.wakeup(worker)==1 && t.schedule()==worker);
        check(t.wakeup(worker)==0 && t.wakeup(worker)==0 && t.cancelWakeups(worker)==2 && t.cancelWakeups(worker)==0);
        check(t.wakeup(99)==-1);
    }
    std::cout<<"PASS: thread descriptors/IDs/dormancy/priority/preemption/FIFO/semaphore handoff/exit/restart/exhausted ready queue; sleep/wakeup counting\n";
}
