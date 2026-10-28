#pragma once
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <array>
#include <functional>
namespace mp2r {
struct GamePadInput {
    uint16_t pressed{}; // PS2 button bits, active-high; converted to active-low on the SIO2 bus
    std::array<uint8_t,4> sticks{128,128,128,128}; // RX, RY, LX, LY
    std::array<uint8_t,12> pressure{};
};
struct RuntimeCallbacks {
    std::function<GamePadInput()> poll_pad;
    std::function<void(const uint32_t*,uint32_t,uint32_t)> present_frame;
    std::function<bool()> should_stop;
    uint32_t break_pc{}; // optional diagnostic boundary before executing this guest PC
    bool profile{};
    bool compensate_scene_preview{}; // temporary presentation-only PAL scene orientation workaround
    std::function<void(const int16_t*,uint32_t)> play_audio; // interleaved stereo PCM16, 48000 Hz; frame count
};
struct InitializationResult {
    std::string reason;
    uint32_t pc{},source{};
    uint32_t memory_address{};
    uint64_t functions{};
    unsigned dispatches{},semaphores{},syscalls{};
    bool alarm_patch_installed{};
    unsigned threads{},context_switches{},semaphore_waits{};
    bool tlb_services_installed{},main_entry_verified{};
};
InitializationResult runInitialization(const uint8_t* elf,std::size_t bytes,std::ostream& log,std::string_view boot_path,
                                     uint32_t kernel_table_base=0x00060000,unsigned max_checkpoints=2000000,
                                     uint64_t max_executions=200000,std::string_view disc_root={},
                                     const RuntimeCallbacks& callbacks={});
}
