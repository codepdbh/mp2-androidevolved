#pragma once
#include <cstdint>
#include <mutex>

// Counts successful game Presents, excluding the bootstrap and paused intervals.
class FrameRateCounter {
    mutable std::mutex mutex_;
    uint64_t start_=0, last_=0;
    unsigned intervals_=0;
    float fps_=0;
    bool started_=false;
public:
    void presented(uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!started_ || now<last_ || now-last_>2000000000ull) {
            started_=true;start_=last_=now;intervals_=0;fps_=0;return;
        }
        last_=now;++intervals_;
        if(now-start_>=500000000ull) {
            fps_=float(double(intervals_)*1e9/double(now-start_));start_=now;intervals_=0;
        }
    }
    float value(uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !started_ || now<last_ || now-last_>2000000000ull ? 0.f : fps_;
    }
};
