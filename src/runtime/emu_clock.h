#pragma once
#include <chrono>
#include <cstdint>
#include <cstdlib>
namespace mp2r {
// Emulated time shared by the EE timers/VBlank, IOP alarms and delays, SPU2 and DVD latencies.
// By default it advances with emulated work (callers add the time each unit of work represents)
// so device timing stays consistent with execution speed and runs are deterministic;
// MP2R_REAL_TIME=1 follows the host's steady clock instead.
struct EmuClock {
    using time_point=std::chrono::steady_clock::time_point;
    static inline bool real_time=std::getenv("MP2R_REAL_TIME")!=nullptr;
    static inline uint64_t ns=1000000000ull;                 // starts at 1 s so zero stays a "never" sentinel
    static time_point now() { return real_time?std::chrono::steady_clock::now():time_point(std::chrono::nanoseconds(ns)); }
    static void advance(uint64_t delta_ns) { ns+=delta_ns; }
};
}
