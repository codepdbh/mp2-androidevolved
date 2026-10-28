#pragma once
#include <chrono>
#include <cstdint>
namespace mp2r {
class RuntimeTimer {
    uint64_t* total_;
    std::chrono::steady_clock::time_point start_;
public:
    RuntimeTimer(bool enabled,uint64_t& total):total_(enabled?&total:nullptr) {
        if(total_) start_=std::chrono::steady_clock::now();
    }
    ~RuntimeTimer(){ if(total_) *total_+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start_).count()); }
};
}
