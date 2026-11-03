// Puente D3D8 para dxvk-native 1.9.2b, que no tiene IDxvkD3D8Bridge (MP2R, no es de DXVK).
// El frontend de DXVK 2.4 lo usa para tres cosas: el nombre del HUD, el modo de
// compatibilidad D3D8 y copiar de MANAGED a DEFAULT. Esta última se hace con la API
// pública de D3D9; el modo de compatibilidad no existe en esta versión de D3D9.
#include "d3d8_include.h"
#include "d3d8_port_bridge.h"
#include "../util/config/config.h"
#include <algorithm>
#include <atomic>
#include <cstring>

namespace dxvk {
  namespace {
    // Bytes de un bloque y lado del bloque en píxeles.
    void formatBlock(d3d9::D3DFORMAT format, UINT& bytes, UINT& side) {
      side = 1;
      switch (static_cast<uint32_t>(format)) {
        case d3d9::D3DFMT_DXT1: bytes = 8; side = 4; return;
        case d3d9::D3DFMT_DXT2: case d3d9::D3DFMT_DXT3:
        case d3d9::D3DFMT_DXT4: case d3d9::D3DFMT_DXT5: bytes = 16; side = 4; return;
        case d3d9::D3DFMT_A8R8G8B8: case d3d9::D3DFMT_X8R8G8B8: case d3d9::D3DFMT_D24S8:
        case d3d9::D3DFMT_D24X8: case d3d9::D3DFMT_D32: bytes = 4; return;
        case d3d9::D3DFMT_R8G8B8: bytes = 3; return;
        case d3d9::D3DFMT_R5G6B5: case d3d9::D3DFMT_A1R5G5B5: case d3d9::D3DFMT_X1R5G5B5:
        case d3d9::D3DFMT_A4R4G4B4: case d3d9::D3DFMT_X4R4G4B4: case d3d9::D3DFMT_D16: bytes = 2; return;
        default: bytes = 1; return;
      }
    }

    class PortBridge final : public IDxvkD3D8Bridge {
    public:
      ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
      ULONG STDMETHODCALLTYPE Release() override {
        ULONG refs = --m_refs;
        if (!refs) delete this;
        return refs;
      }
      HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override {
        if (object) *object = nullptr;
        return E_NOINTERFACE;
      }
      void SetAPIName(const char*) override { }
      void SetD3D8CompatibilityMode(const bool) override { }

      // d3d8_device.cpp pasa primero el origen, aunque el nombre del parámetro diga destino.
      HRESULT UpdateTextureFromBuffer(d3d9::IDirect3DSurface9* source, d3d9::IDirect3DSurface9* destination,
                                      const RECT* sourceRect, const POINT* destinationPoint) override {
        d3d9::D3DSURFACE_DESC desc;
        HRESULT status = source->GetDesc(&desc);
        if (FAILED(status)) return status;
        UINT bytes, side;
        formatBlock(desc.Format, bytes, side);
        RECT from = sourceRect ? *sourceRect : RECT{ 0, 0, LONG(desc.Width), LONG(desc.Height) };
        POINT at = destinationPoint ? *destinationPoint : POINT{ 0, 0 };
        RECT to = { at.x, at.y, at.x + (from.right - from.left), at.y + (from.bottom - from.top) };
        d3d9::D3DLOCKED_RECT in, out;
        status = source->LockRect(&in, &from, D3DLOCK_READONLY);
        if (FAILED(status)) return status;
        status = destination->LockRect(&out, &to, 0);
        if (SUCCEEDED(status)) {
          UINT rows = (UINT(from.bottom - from.top) + side - 1) / side;
          UINT row = (UINT(from.right - from.left) + side - 1) / side * bytes;
          for (UINT y = 0; y < rows; y++)
            std::memcpy(static_cast<uint8_t*>(out.pBits) + size_t(y) * out.Pitch,
                        static_cast<const uint8_t*>(in.pBits) + size_t(y) * in.Pitch, row);
          destination->UnlockRect();
        }
        source->UnlockRect();
        return status;
      }

    private:
      std::atomic<ULONG> m_refs{ 0 };
    };

    class PortInterfaceBridge final : public IDxvkD3D8InterfaceBridge {
    public:
      ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
      ULONG STDMETHODCALLTYPE Release() override {
        ULONG refs = --m_refs;
        if (!refs) delete this;
        return refs;
      }
      HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override {
        if (object) *object = nullptr;
        return E_NOINTERFACE;
      }
      const Config* GetConfig() const override { return &m_config; }

    private:
      std::atomic<ULONG> m_refs{ 0 };
      Config m_config;
    };
  }

  IDxvkD3D8Bridge* createPortD3D8Bridge() { return new PortBridge(); }
  IDxvkD3D8InterfaceBridge* createPortD3D8InterfaceBridge() { return new PortInterfaceBridge(); }
}
