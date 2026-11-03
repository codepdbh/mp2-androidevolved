#pragma once
#include <cstdint>
struct ANativeWindow;
void configureD3D8Bridge(void* guestBase, uint32_t (*allocator)(uint32_t), void (*deallocator)(uint32_t));
void shutdownD3D8Bridge();
void setD3D8HostWindow(ANativeWindow* window);
void setD3D8RenderSize(unsigned width, unsigned height);
void setD3D8Compatibility(unsigned backBuffers);
bool d3d8RequestsSurface();
void acknowledgeD3D8Surface(bool available);
uint32_t createGuestD3D8(uint32_t sdk);
uint32_t dispatchGuestD3D8(uint32_t token, const uint32_t* args, uint32_t* argumentCount);
