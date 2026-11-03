#pragma once
// Addresses of this game's globals that the runtime reads (0: not located yet
// for Carbon, the dependent behaviour is off). Each must be measured on
// NFSC.exe; none may be copied from the Underground or Most Wanted ports.
#include <cstdint>
namespace game {
constexpr uint32_t mouseObject=0;      // DirectInput mouse object: +4 cursor X, +8 cursor Y (NFSU1: 0x706498)
constexpr uint32_t presentParams=0;    // D3DPRESENT_PARAMETERS the game fills (NFSU1: 0x71A96C)
constexpr uint32_t windowHandle=0;     // global holding the game's HWND (NFSU1: 0x736380)
}
