cmake_minimum_required(VERSION 3.22)
project(MP2Android C CXX)
if(NOT ANDROID OR NOT (ANDROID_ABI STREQUAL "arm64-v8a" OR ANDROID_ABI STREQUAL "armeabi-v7a"))
    message(FATAL_ERROR "Este objetivo requiere Android arm64-v8a o armeabi-v7a experimental")
endif()
add_library(native_app_glue STATIC "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC "${ANDROID_NDK}/sources/android/native_app_glue")

get_filename_component(MP2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(PCRECOMP "${MP2_ROOT}/external/pcrecomp")
# Port de DXVK a Android: copia propia y fija, sacada de la versión confirmada de
# nfsu2-recompiled, para que los cambios de aquel proyecto no rompan esta compilación.
set(DXVK_PORT "${MP2_ROOT}/work/pc/dxvk_android/port" CACHE PATH "Port dxvk_android")
set(DXVK_SOURCE "${MP2_ROOT}/work/pc/dxvk_android/source" CACHE PATH "Fuente DXVK nativa preparada" FORCE)
set(DXVK_ANDROID_STATIC ON CACHE BOOL "Enlazar dentro de la aplicación" FORCE)
add_subdirectory("${DXVK_PORT}" "${CMAKE_CURRENT_BINARY_DIR}/dxvk")

# Código recompilado: se genera en local desde la instalación del usuario y no se sube.
set(GEN_DIR "${MP2_ROOT}/work/pc/gen")
file(GLOB GUEST_SOURCES CONFIGURE_DEPENDS "${GEN_DIR}/*/*recomp_[0-9]*.c")
if(NOT GUEST_SOURCES)
    message(FATAL_ERROR "Genera antes el código: scripts/pc_recomp/recompile_modules.py")
endif()
list(APPEND GUEST_SOURCES "${GEN_DIR}/merged/mp2_dispatch.c")
foreach(source IN LISTS GUEST_SOURCES)
    get_filename_component(source_dir "${source}" DIRECTORY)
    set_source_files_properties("${source}" PROPERTIES COMPILE_OPTIONS "-I${source_dir}")
endforeach()
# Límite de trabajos: la compilación no debe acaparar la máquina del usuario.
set(MP2_GUEST_JOBS 8 CACHE STRING "Compilaciones simultáneas de código recompilado")
set_property(GLOBAL PROPERTY JOB_POOLS guest_compile=${MP2_GUEST_JOBS})
add_library(mp2_guest OBJECT ${GUEST_SOURCES})
set_property(TARGET mp2_guest PROPERTY JOB_POOL_COMPILE guest_compile)
target_include_directories(mp2_guest PRIVATE "${PCRECOMP}/runtime/recomp32")
# Mismos flags que la prueba de subconjunto (scripts/pc_recomp/compile_arm64_subset.sh),
# con los marcos SEH validados en Windows. x87 con ventana deslizante en toda la biblioteca.
target_compile_definitions(mp2_guest PRIVATE RECOMP_SEH_FRAMES RECOMP_STRICT_ICALL RECOMP_LOCAL_REGS RECOMP_X87_WINDOW "RECOMP_MEM_QUAL=")
target_compile_options(mp2_guest PRIVATE -O2 -g0 -w -fwrapv -fno-strict-aliasing -fexceptions -ffp-contract=off -fvisibility=hidden
    -include "${CMAKE_CURRENT_SOURCE_DIR}/arm64_guest.h")

# Frontend D3D8 de DXVK (zlib) sobre el D3D9 de dxvk-native.
set(D3D8_DIR "${MP2_ROOT}/third_party/dxvk_d3d8/src/d3d8")
file(GLOB D3D8_SOURCES CONFIGURE_DEPENDS "${D3D8_DIR}/*.cpp")
add_library(mp2_d3d8 OBJECT ${D3D8_SOURCES})
target_compile_features(mp2_d3d8 PRIVATE cxx_std_17)
target_compile_definitions(mp2_d3d8 PRIVATE DXVK_NATIVE DXVK_PLATFORM_LINUX NOMINMAX)
target_include_directories(mp2_d3d8 PRIVATE "${DXVK_SOURCE}" "${DXVK_SOURCE}/include" "${DXVK_SOURCE}/src/util"
    "${DXVK_SOURCE}/include/native" "${DXVK_SOURCE}/include/native/windows" "${DXVK_SOURCE}/include/native/directx"
    "${MP2_ROOT}/third_party/dxvk_d3d8/src/d3d9" "${DXVK_SOURCE}/src/d3d9" "${DXVK_SOURCE}/generated")
target_compile_options(mp2_d3d8 PRIVATE -O2 -w)

add_library(mp2_diagnostics SHARED gpu_probe.cpp vulkan_driver.cpp)
target_link_libraries(mp2_diagnostics PRIVATE android log dl)
target_compile_options(mp2_diagnostics PRIVATE -O2 -w)
target_link_options(mp2_diagnostics PRIVATE "-Wl,-z,max-page-size=16384")

add_library(mp2_android SHARED main.cpp guest_runtime.cpp d3d8_bridge.cpp vulkan_driver.cpp mp2_overrides.c
    $<TARGET_OBJECTS:mp2_guest> $<TARGET_OBJECTS:mp2_d3d8>)
target_include_directories(mp2_android SYSTEM PRIVATE "${DXVK_SOURCE}/include/native"
    "${DXVK_SOURCE}/include/native/windows" "${DXVK_SOURCE}/include/native/directx")
target_include_directories(mp2_android PRIVATE "${PCRECOMP}/runtime/recomp32" "${PCRECOMP}/runtime/win32hle")
target_compile_definitions(mp2_android PRIVATE RECOMP_STRICT_ICALL RECOMP_X87_WINDOW VK_USE_PLATFORM_ANDROID_KHR)
target_compile_features(mp2_android PRIVATE cxx_std_17)
target_compile_options(mp2_android PRIVATE -O2 -fno-strict-aliasing -ffp-contract=off -w)
target_link_options(mp2_android PRIVATE "-uANativeActivity_onCreate" "-Wl,-z,max-page-size=16384")
target_link_libraries(mp2_android PRIVATE native_app_glue android log vulkan dxvk_android aaudio dl)
