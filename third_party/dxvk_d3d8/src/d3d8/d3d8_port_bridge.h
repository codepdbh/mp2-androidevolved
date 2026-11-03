// Puente D3D8 para dxvk-native 1.9.2b (MP2R, no es de DXVK). Ver d3d8_port_bridge.cpp.
#pragma once

#include "../d3d9/d3d9_bridge.h"

namespace dxvk {
  IDxvkD3D8Bridge* createPortD3D8Bridge();
  IDxvkD3D8InterfaceBridge* createPortD3D8InterfaceBridge();
}
