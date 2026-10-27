cmake_minimum_required(VERSION 3.21)
project(mp2r_android LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
get_filename_component(MP2R_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_library(mp2r SHARED native_bridge.cpp
    "${MP2R_ROOT}/src/input/touch_engine.cpp"
    "${MP2R_ROOT}/src/runtime/initialization_probe.cpp"
    "${MP2R_ROOT}/tests/bootstrap_entry_probe.cpp")
target_compile_definitions(mp2r PRIVATE USE_SSE2NEON MP2R_EMBED_PROBE)
target_include_directories(mp2r PRIVATE
    "${MP2R_ROOT}/src/input" "${MP2R_ROOT}/src/runtime"
    "${MP2R_ROOT}/private/generated/ghidra"
    "${MP2R_ROOT}/private/generated/initialization-code"
    "${MP2R_ROOT}/private/generated/initialization"
    "${MP2R_ROOT}/external/PS2Recomp/ps2xRuntime/include"
    "${MP2R_ROOT}/external/PS2Recomp/ps2xRuntime/src/lib/Kernel"
    "${MP2R_ROOT}/external/PS2Recomp/ps2xIOP/include"
    "${MP2R_ROOT}/external/sse2neon")
target_link_options(mp2r PRIVATE "-Wl,-z,max-page-size=16384")
