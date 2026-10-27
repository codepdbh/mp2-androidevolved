#pragma once
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
namespace mp2r {
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
                                     uint64_t max_executions=200000,std::string_view disc_root={});
}
