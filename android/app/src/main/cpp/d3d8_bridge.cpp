// Direct3D 8 para el código invitado: objetos COM de 32 bits, con vtables de tokens, sobre el
// frontend D3D8 de DXVK (third_party/dxvk_d3d8), que traduce a D3D9 y este a Vulkan.
// Adaptado del puente D3D9 de los ports de NFS (d3d9_bridge.cpp).
#include "d3d8_bridge.h"
#include "runtime_log.h"
#include <d3d8.h>
#include <android/native_window.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" IDirect3D8* Direct3DCreate8(UINT sdk);
extern "C" void dxvkAndroidSetWindow(ANativeWindow* window);
extern "C" HWND dxvkAndroidGetWindowHandle();
extern "C" void dxvkAndroidSetRenderSize(uint32_t width, uint32_t height);

namespace {
constexpr uint32_t methodBase = 0x7e000000;
enum class Kind : uint32_t { D3D8 = 1, Device = 2, Texture = 3, Cube = 4, VolumeTexture = 5, Surface = 6, Volume = 7, VertexBuffer = 8, IndexBuffer = 9, SwapChain = 10 };
constexpr uint32_t kindSizes[] = { 0, 16, 97, 19, 19, 19, 11, 11, 14, 14, 5 };

struct Object { IUnknown* native; Kind kind; uint32_t references = 1; uint32_t parent = 0; uint32_t children = 0; uint32_t mirror = 0; };
std::unordered_map<uint32_t, Object> objects;
std::map<IUnknown*, uint32_t> addresses;
uint32_t hotDeviceAddress = 0;
IUnknown* hotDeviceNative = nullptr;
void* guestBase;
uint32_t (*allocateGuest)(uint32_t);
void (*freeGuest)(uint32_t);
std::atomic<ANativeWindow*> hostWindow{ nullptr };
std::atomic<bool> surfaceRequested{ false }, surfaceAvailable{ false };
std::atomic<unsigned> guestModeWidth{ 0 }, guestModeHeight{ 0 };
std::atomic<uint32_t> configuredBackBuffers{ 2 };
bool deviceResetPending = false;

void* pointer(uint32_t address) { return address ? static_cast<uint8_t*>(guestBase) + address : nullptr; }
void write(uint32_t address, uint32_t value) {
    if (!address) throw std::runtime_error("Salida D3D8 nula");
    std::memcpy(pointer(address), &value, 4);
}
float floatValue(uint32_t bits) { float value; std::memcpy(&value, &bits, 4); return value; }
uint32_t floatBits(float value) { uint32_t bits; std::memcpy(&bits, &value, 4); return bits; }

uint32_t wrap(IUnknown* native, Kind kind) {
    if (!native) return 0;
    auto previous = addresses.find(native);
    if (previous != addresses.end()) { ++objects.at(previous->second).references; return previous->second; }
    uint32_t count = kindSizes[static_cast<uint32_t>(kind)];
    uint32_t table = allocateGuest(count * 4), object = allocateGuest(4);
    for (uint32_t i = 0; i < count; ++i) write(table + i * 4, methodBase + static_cast<uint32_t>(kind) * 4096 + i * 16);
    write(object, table);
    objects.emplace(object, Object{ native, kind });
    addresses.emplace(native, object);
    return object;
}
uint32_t wrapTexture(IDirect3DBaseTexture8* texture) {
    if (!texture) return 0;
    switch (texture->GetType()) {
    case D3DRTYPE_TEXTURE: return wrap(texture, Kind::Texture);
    case D3DRTYPE_CUBETEXTURE: return wrap(texture, Kind::Cube);
    case D3DRTYPE_VOLUMETEXTURE: return wrap(texture, Kind::VolumeTexture);
    default: throw std::runtime_error("Tipo de textura D3D8 desconocido");
    }
}
template <class T> T* nativeObject(uint32_t address) {
    if (!address) return nullptr;
    auto found = objects.find(address);
    if (found == objects.end()) throw std::runtime_error("Recurso D3D8 inválido");
    auto native = dynamic_cast<T*>(found->second.native);
    if (!native) throw std::runtime_error("Interfaz D3D8 equivocada");
    return native;
}

// ---- Bloqueos: el invitado escribe en una copia dentro de su memoria de 32 bits ----
struct ResourceLock { void* native; uint32_t guest, size, pitch, rowBytes, rows, flags; bool mirrored = false; uint32_t offset = 0; uint32_t depth = 1; };
std::map<uint64_t, ResourceLock> resourceLocks;
uint64_t lockKey(uint32_t object, uint32_t subresource) { return (uint64_t(object) << 32) | subresource; }
bool blockCompressed(D3DFORMAT format) { return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3 || format == D3DFMT_DXT4 || format == D3DFMT_DXT5; }
uint32_t rowBytes(D3DFORMAT format, uint32_t width) {
    switch (format) {
    case D3DFMT_DXT1: return ((width + 3) / 4) * 8;
    case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5: return ((width + 3) / 4) * 16;
    case D3DFMT_R8G8B8: return width * 3;
    case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_P8: return width;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
    case D3DFMT_X4R4G4B4: case D3DFMT_A8L8: case D3DFMT_V8U8: return width * 2;
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_Q8W8V8U8: case D3DFMT_V16U16: return width * 4;
    default: throw std::runtime_error("Formato D3D8 sin bloqueo: " + std::to_string(format));
    }
}
HRESULT lockPixels(uint32_t address, uint32_t subresource, const D3DSURFACE_DESC& desc, const D3DLOCKED_RECT& native, const RECT* rect, uint32_t flags, uint32_t output) {
    uint32_t width = rect ? rect->right - rect->left : desc.Width, height = rect ? rect->bottom - rect->top : desc.Height;
    uint32_t rows = blockCompressed(desc.Format) ? (height + 3) / 4 : height, bytes = rowBytes(desc.Format, width);
    if (native.Pitch <= 0 || uint32_t(native.Pitch) < bytes || !rows) throw std::runtime_error("Pitch de bloqueo D3D8 inválido");
    uint64_t size = uint64_t(native.Pitch) * rows;
    if (size > 0x7fffffff) throw std::runtime_error("Bloqueo D3D8 demasiado grande");
    uint32_t temporary = allocateGuest(uint32_t(size));
    if (!(flags & D3DLOCK_DISCARD))
        for (uint32_t row = 0; row < rows; ++row)
            std::memcpy(static_cast<uint8_t*>(pointer(temporary)) + row * native.Pitch, static_cast<const uint8_t*>(native.pBits) + row * native.Pitch, bytes);
    resourceLocks.emplace(lockKey(address, subresource), ResourceLock{ native.pBits, temporary, uint32_t(size), uint32_t(native.Pitch), bytes, rows, flags });
    write(output, native.Pitch);
    write(output + 4, temporary);
    return D3D_OK;
}
void copyLockBack(uint32_t address, uint32_t subresource) {
    auto lock = resourceLocks.find(lockKey(address, subresource));
    if (lock == resourceLocks.end()) throw std::runtime_error("Unlock D3D8 sin bloqueo");
    auto& data = lock->second;
    if (!(data.flags & D3DLOCK_READONLY))
        for (uint32_t row = 0; row < data.rows; ++row)
            std::memcpy(static_cast<uint8_t*>(data.native) + row * data.pitch, static_cast<const uint8_t*>(pointer(data.guest)) + row * data.pitch, data.rowBytes);
}
void releaseLock(uint32_t address, uint32_t subresource) {
    auto lock = resourceLocks.find(lockKey(address, subresource));
    if (lock == resourceLocks.end()) return;
    if (!lock->second.mirrored) freeGuest(lock->second.guest - lock->second.offset);
    resourceLocks.erase(lock);
}
void dropGuestLocks(uint32_t address) {
    for (auto it = resourceLocks.lower_bound(lockKey(address, 0)); it != resourceLocks.end() && uint32_t(it->first >> 32) == address;) {
        if (!it->second.mirrored) freeGuest(it->second.guest - it->second.offset);
        it = resourceLocks.erase(it);
    }
}
template <class Buffer, class Desc> HRESULT lockBuffer(uint32_t address, uint32_t offset, uint32_t size, uint32_t output, uint32_t flags) {
    if (!output) return D3DERR_INVALIDCALL;
    auto buffer = nativeObject<Buffer>(address);
    Desc desc{};
    HRESULT status = buffer->GetDesc(&desc);
    if (FAILED(status)) return status;
    if (offset > desc.Size || (size && size > desc.Size - offset)) return D3DERR_INVALIDCALL;
    if (auto open = resourceLocks.find(lockKey(address, 0)); open != resourceLocks.end()) {
        auto& lock = open->second;
        if (offset < lock.offset || (size ? offset + size : desc.Size) > lock.offset + lock.size) return D3DERR_INVALIDCALL;
        ++lock.depth;
        write(output, lock.guest + (offset - lock.offset));
        return D3D_OK;
    }
    // Los buffers dinámicos de solo escritura se bloquean muchas veces por frame:
    // una copia invitada para toda su vida, y sin leer de la memoria de la GPU.
    constexpr DWORD streaming = D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY;
    auto& entry = objects.at(address);
    if ((desc.Usage & streaming) == streaming && !entry.mirror) {
        entry.mirror = allocateGuest(desc.Size);
        std::memset(pointer(entry.mirror), 0, desc.Size);
    }
    BYTE* native = nullptr;
    status = buffer->Lock(offset, size, &native, flags);
    if (FAILED(status)) return status;
    if (!size) size = desc.Size - offset;
    if (entry.mirror) {
        resourceLocks.emplace(lockKey(address, 0), ResourceLock{ native, entry.mirror + offset, size, size, size, 1, flags, true, offset });
        write(output, entry.mirror + offset);
        return D3D_OK;
    }
    uint32_t temporary = allocateGuest(desc.Size) + offset;
    if (!(flags & D3DLOCK_DISCARD)) std::memcpy(pointer(temporary), native, size);
    resourceLocks.emplace(lockKey(address, 0), ResourceLock{ native, temporary, size, size, size, 1, flags, false, offset });
    write(output, temporary);
    return D3D_OK;
}

void retireGuestObject(uint32_t address) {
    if (address == hotDeviceAddress) { hotDeviceAddress = 0; hotDeviceNative = nullptr; }
    dropGuestLocks(address);
    auto found = objects.find(address);
    if (found == objects.end()) return;
    uint32_t table;
    std::memcpy(&table, pointer(address), 4);
    if (found->second.mirror) freeGuest(found->second.mirror);
    freeGuest(table);
    freeGuest(address);
    addresses.erase(found->second.native);
    objects.erase(found);
}
void attachTextureSurface(uint32_t texture, uint32_t surface) {
    if (!surface) return;
    auto& child = objects.at(surface);
    if (!child.parent) { child.parent = texture; ++objects.at(texture).children; }
}

// D3DPRESENT_PARAMETERS de D3D8 en memoria invitada: 13 palabras.
D3DPRESENT_PARAMETERS presentation(uint32_t address) {
    auto p = static_cast<const uint32_t*>(pointer(address));
    if (!p) throw std::runtime_error("Parámetros de presentación D3D8 nulos");
    D3DPRESENT_PARAMETERS result{};
    result.BackBufferWidth = guestModeWidth ? guestModeWidth.load() : p[0];
    result.BackBufferHeight = guestModeHeight ? guestModeHeight.load() : p[1];
    result.BackBufferFormat = static_cast<D3DFORMAT>(p[2]);
    result.BackBufferCount = std::max(p[3], configuredBackBuffers.load());
    result.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(p[4]);
    result.SwapEffect = static_cast<D3DSWAPEFFECT>(p[5]);
    result.hDeviceWindow = dxvkAndroidGetWindowHandle();
    result.Windowed = p[7];
    result.EnableAutoDepthStencil = p[8];
    result.AutoDepthStencilFormat = static_cast<D3DFORMAT>(p[9]);
    result.Flags = p[10];
    result.FullScreen_RefreshRateInHz = p[11];
    result.FullScreen_PresentationInterval = p[12];
    return result;
}

// Modos clásicos, luego los del panel: el diálogo de inicio y el juego eligen por lista.
std::vector<D3DDISPLAYMODE> adapterModes(IDirect3D8* d3d, UINT adapter) {
    std::vector<D3DDISPLAYMODE> modes;
    if (guestModeWidth && guestModeHeight) modes.push_back({ guestModeWidth, guestModeHeight, 60, D3DFMT_X8R8G8B8 });
    for (auto size : { std::pair<UINT, UINT>{ 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 960 } })
        modes.push_back({ size.first, size.second, 60, D3DFMT_X8R8G8B8 });
    UINT count = d3d->GetAdapterModeCount(adapter);
    for (UINT i = 0; i < count; ++i) {
        D3DDISPLAYMODE mode{};
        if (FAILED(d3d->EnumAdapterModes(adapter, i, &mode))) continue;
        if (std::none_of(modes.begin(), modes.end(), [&](const D3DDISPLAYMODE& m) { return m.Width == mode.Width && m.Height == mode.Height && m.Format == mode.Format; }))
            modes.push_back(mode);
    }
    return modes;
}
}  // namespace

void configureD3D8Bridge(void* base, uint32_t (*allocator)(uint32_t), void (*deallocator)(uint32_t)) {
    guestBase = base;
    allocateGuest = allocator;
    freeGuest = deallocator;
    if (!std::getenv("DXVK_STATE_CACHE_PATH")) setenv("DXVK_STATE_CACHE", "0", 1);
    setenv("DXVK_LOG_PATH", "none", 1);
}
void shutdownD3D8Bridge() {
    hotDeviceAddress = 0;
    hotDeviceNative = nullptr;
    resourceLocks.clear();
    objects.clear();
    addresses.clear();
    surfaceRequested = false;
    surfaceAvailable = false;
}
void setD3D8HostWindow(ANativeWindow* window) { hostWindow = window; dxvkAndroidSetWindow(window); }
void setD3D8RenderSize(unsigned width, unsigned height) { guestModeWidth = width; guestModeHeight = height; dxvkAndroidSetRenderSize(width, height); }
void setD3D8Compatibility(unsigned backBuffers) { configuredBackBuffers = backBuffers < 1 ? 1u : backBuffers > 3 ? 3u : backBuffers; }
bool d3d8RequestsSurface() { return surfaceRequested; }
void acknowledgeD3D8Surface(bool available) { surfaceAvailable = available; }
uint32_t createGuestD3D8(uint32_t sdk) {
    auto native = Direct3DCreate8(sdk);
    NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "MP2", "Direct3DCreate8(%u) -> %p", sdk, native);
    return wrap(native, Kind::D3D8);
}

uint32_t dispatchGuestD3D8(uint32_t token, const uint32_t* args, uint32_t* argumentCount) {
    uint32_t kind = (token - methodBase) / 4096, method = ((token - methodBase) % 4096) / 16;
    auto found = objects.end();
    IUnknown* object = nullptr;
    if (kind == uint32_t(Kind::Device) && method > 2 && args[0] == hotDeviceAddress && hotDeviceNative) object = hotDeviceNative;
    else {
        found = objects.find(args[0]);
        if (found == objects.end() || uint32_t(found->second.kind) != kind) {
            NFS_RUNTIME_LOG(ANDROID_LOG_ERROR, "MP2", "COM D3D8 inválido objeto=%08x token=%08x clase=%u método=%u", args[0], token, kind, method);
            throw std::runtime_error("Objeto COM D3D8 inválido");
        }
        object = found->second.native;
        if (kind == uint32_t(Kind::Device)) { hotDeviceAddress = args[0]; hotDeviceNative = object; }
    }
    static uint8_t methodCalls[4096];
    auto& calls = methodCalls[((token - methodBase) / 16) & 4095];
    if (calls < 2 && ++calls) NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "MP2", "D3D8 clase=%u método=%u", kind, method);

    if (method == 1) { *argumentCount = 1; object->AddRef(); return ++found->second.references; }
    if (method == 2) {
        *argumentCount = 1;
        if (!found->second.references) {
            if (found->second.children) return 0;
            throw std::runtime_error("Release D3D8 sin referencia");
        }
        auto native = object->Release();
        uint32_t references = --found->second.references;
        if (!references) {
            if (found->second.children) return uint32_t(native);
            auto parent = found->second.parent;
            retireGuestObject(args[0]);
            if (parent) {
                auto owner = objects.find(parent);
                if (owner != objects.end() && !--owner->second.children && !owner->second.references) retireGuestObject(parent);
            }
        }
        return references;
    }
    if (method == 0) {
        *argumentCount = 3;
        IUnknown* result = nullptr;
        HRESULT status = object->QueryInterface(*static_cast<const GUID*>(pointer(args[1])), reinterpret_cast<void**>(&result));
        write(args[2], SUCCEEDED(status) ? wrap(result, found->second.kind) : 0);
        return status;
    }

    if (kind == uint32_t(Kind::D3D8)) {
        auto d3d = static_cast<IDirect3D8*>(object);
        switch (method) {
        case 3: *argumentCount = 2; return D3DERR_NOTAVAILABLE;
        case 4: *argumentCount = 1; return d3d->GetAdapterCount();
        case 5: {
            *argumentCount = 4;
            auto identifier = static_cast<D3DADAPTER_IDENTIFIER8*>(pointer(args[3]));
            HRESULT status = d3d->GetAdapterIdentifier(args[1], args[2], identifier);
            // El juego compara este nombre con "Driver" del registro (guest_registry.inc).
            if (SUCCEEDED(status)) { std::strcpy(identifier->Description, "Android Vulkan"); }
            return status;
        }
        case 6: *argumentCount = 2; return uint32_t(adapterModes(d3d, args[1]).size());
        case 7: {
            *argumentCount = 4;
            auto modes = adapterModes(d3d, args[1]);
            if (args[2] >= modes.size()) return D3DERR_INVALIDCALL;
            std::memcpy(pointer(args[3]), &modes[args[2]], sizeof(D3DDISPLAYMODE));
            return D3D_OK;
        }
        case 8: {
            *argumentCount = 3;
            auto mode = static_cast<D3DDISPLAYMODE*>(pointer(args[2]));
            HRESULT status = d3d->GetAdapterDisplayMode(args[1], mode);
            if (SUCCEEDED(status) && mode && guestModeWidth && guestModeHeight) { mode->Width = guestModeWidth; mode->Height = guestModeHeight; }
            return status;
        }
        case 9: *argumentCount = 6; return d3d->CheckDeviceType(args[1], static_cast<D3DDEVTYPE>(args[2]), static_cast<D3DFORMAT>(args[3]), static_cast<D3DFORMAT>(args[4]), args[5]);
        case 10: *argumentCount = 7; return d3d->CheckDeviceFormat(args[1], static_cast<D3DDEVTYPE>(args[2]), static_cast<D3DFORMAT>(args[3]), args[4], static_cast<D3DRESOURCETYPE>(args[5]), static_cast<D3DFORMAT>(args[6]));
        case 11: *argumentCount = 6; return d3d->CheckDeviceMultiSampleType(args[1], static_cast<D3DDEVTYPE>(args[2]), static_cast<D3DFORMAT>(args[3]), args[4], static_cast<D3DMULTISAMPLE_TYPE>(args[5]));
        case 12: *argumentCount = 6; return d3d->CheckDepthStencilMatch(args[1], static_cast<D3DDEVTYPE>(args[2]), static_cast<D3DFORMAT>(args[3]), static_cast<D3DFORMAT>(args[4]), static_cast<D3DFORMAT>(args[5]));
        case 13: *argumentCount = 4; return d3d->GetDeviceCaps(args[1], static_cast<D3DDEVTYPE>(args[2]), static_cast<D3DCAPS8*>(pointer(args[3])));
        case 14: *argumentCount = 2; return d3d->GetAdapterMonitor(args[1]) ? 0xf0000300 : 0;
        case 15: {
            *argumentCount = 7;
            if (!args[5] || !args[6]) return D3DERR_INVALIDCALL;
            if (!hostWindow.load()) throw std::runtime_error("CreateDevice requiere superficie Android");
            surfaceRequested = true;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!surfaceAvailable) {
                if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Superficie Android sin entregar");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            auto window = dxvkAndroidGetWindowHandle();
            if (!window) return D3DERR_DEVICELOST;
            auto parameters = presentation(args[5]);
            parameters.Windowed = TRUE;
            parameters.FullScreen_RefreshRateInHz = 0;
            parameters.FullScreen_PresentationInterval = 0;
            IDirect3DDevice8* device = nullptr;
            HRESULT status = d3d->CreateDevice(args[1], static_cast<D3DDEVTYPE>(args[2]), window, args[4], &parameters, &device);
            NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "MP2", "CreateDevice %ux%u fmt=%u profundidad=%u -> %08x", parameters.BackBufferWidth, parameters.BackBufferHeight,
                            unsigned(parameters.BackBufferFormat), unsigned(parameters.AutoDepthStencilFormat), uint32_t(status));
            if (FAILED(status)) return status;
            write(args[6], wrap(device, Kind::Device));
            return status;
        }
        }
    }
    if (kind == uint32_t(Kind::Device)) {
        auto device = static_cast<IDirect3DDevice8*>(object);
        switch (method) {
        case 36: {
            // Clear con rectángulos: en D3D8 uno vacío o invertido no borra nada, pero al llegar
            // a DXVK borra la pantalla entera. Las bandas de cinemática del WidescreenFix tienen
            // altura cero o negativa en pantallas anchas: era la cinemática en negro.
            *argumentCount = 7;
            uint32_t count = args[1];
            auto rects = static_cast<const D3DRECT*>(pointer(args[2]));
            if (!count || !rects)
                return device->Clear(count, rects, args[3], args[4], floatValue(args[5]), args[6]);
            std::vector<D3DRECT> valid;
            for (uint32_t i = 0; i < count; ++i)
                if (rects[i].x2 > rects[i].x1 && rects[i].y2 > rects[i].y1) valid.push_back(rects[i]);
            if (valid.empty()) return D3D_OK;
            return device->Clear(uint32_t(valid.size()), valid.data(), args[3], args[4], floatValue(args[5]), args[6]);
        }
        case 3: *argumentCount = 1; return deviceResetPending ? D3DERR_DEVICENOTRESET : device->TestCooperativeLevel();
        case 14: {
            *argumentCount = 2;
            auto parameters = presentation(args[1]);
            parameters.Windowed = TRUE;
            parameters.FullScreen_RefreshRateInHz = 0;
            parameters.FullScreen_PresentationInterval = 0;
            HRESULT status = D3DERR_DEVICELOST;
            for (int attempt = 0; attempt < 2; ++attempt) {
                try { status = device->Reset(&parameters); break; }
                catch (...) {
                    status = D3DERR_DEVICELOST;
                    if (!attempt) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            deviceResetPending = status == D3DERR_DEVICELOST;
            NFS_RUNTIME_LOG(ANDROID_LOG_INFO, "MP2", "Reset %ux%u -> %08x", parameters.BackBufferWidth, parameters.BackBufferHeight, uint32_t(status));
            return status;
        }
        case 15: {
            *argumentCount = 5;
            auto window = dxvkAndroidGetWindowHandle();
            if (!window || deviceResetPending) return D3DERR_DEVICELOST;
            return device->Present(static_cast<const RECT*>(pointer(args[1])), static_cast<const RECT*>(pointer(args[2])), window, static_cast<const RGNDATA*>(pointer(args[4])));
        }
        case 13: *argumentCount = 3; throw std::runtime_error("CreateAdditionalSwapChain no se usa en MP2");
        case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: {
            uint32_t output;
            HRESULT status;
            switch (method) {
            case 20: { *argumentCount = 8; output = args[7]; IDirect3DTexture8* t = nullptr; status = device->CreateTexture(args[1], args[2], args[3], args[4], static_cast<D3DFORMAT>(args[5]), static_cast<D3DPOOL>(args[6]), &t); write(output, SUCCEEDED(status) ? wrap(t, Kind::Texture) : 0); return status; }
            case 21: { *argumentCount = 9; output = args[8]; IDirect3DVolumeTexture8* t = nullptr; status = device->CreateVolumeTexture(args[1], args[2], args[3], args[4], args[5], static_cast<D3DFORMAT>(args[6]), static_cast<D3DPOOL>(args[7]), &t); write(output, SUCCEEDED(status) ? wrap(t, Kind::VolumeTexture) : 0); return status; }
            case 22: { *argumentCount = 7; output = args[6]; IDirect3DCubeTexture8* t = nullptr; status = device->CreateCubeTexture(args[1], args[2], args[3], static_cast<D3DFORMAT>(args[4]), static_cast<D3DPOOL>(args[5]), &t); write(output, SUCCEEDED(status) ? wrap(t, Kind::Cube) : 0); return status; }
            case 23: { *argumentCount = 6; output = args[5]; IDirect3DVertexBuffer8* b = nullptr; status = device->CreateVertexBuffer(args[1], args[2], args[3], static_cast<D3DPOOL>(args[4]), &b); write(output, SUCCEEDED(status) ? wrap(b, Kind::VertexBuffer) : 0); return status; }
            case 24: { *argumentCount = 6; output = args[5]; IDirect3DIndexBuffer8* b = nullptr; status = device->CreateIndexBuffer(args[1], args[2], static_cast<D3DFORMAT>(args[3]), static_cast<D3DPOOL>(args[4]), &b); write(output, SUCCEEDED(status) ? wrap(b, Kind::IndexBuffer) : 0); return status; }
            case 25: { *argumentCount = 7; output = args[6]; IDirect3DSurface8* s = nullptr; status = device->CreateRenderTarget(args[1], args[2], static_cast<D3DFORMAT>(args[3]), static_cast<D3DMULTISAMPLE_TYPE>(args[4]), args[5], &s); write(output, SUCCEEDED(status) ? wrap(s, Kind::Surface) : 0); return status; }
            case 26: { *argumentCount = 6; output = args[5]; IDirect3DSurface8* s = nullptr; status = device->CreateDepthStencilSurface(args[1], args[2], static_cast<D3DFORMAT>(args[3]), static_cast<D3DMULTISAMPLE_TYPE>(args[4]), &s); write(output, SUCCEEDED(status) ? wrap(s, Kind::Surface) : 0); return status; }
            default: { *argumentCount = 5; output = args[4]; IDirect3DSurface8* s = nullptr; status = device->CreateImageSurface(args[1], args[2], static_cast<D3DFORMAT>(args[3]), &s); write(output, SUCCEEDED(status) ? wrap(s, Kind::Surface) : 0); return status; }
            }
        }
        }
    }
    if (kind == uint32_t(Kind::Texture)) {
        auto texture = static_cast<IDirect3DTexture8*>(object);
        if (method == 15) {
            *argumentCount = 3;
            IDirect3DSurface8* surface = nullptr;
            HRESULT status = texture->GetSurfaceLevel(args[1], &surface);
            uint32_t address = SUCCEEDED(status) ? wrap(surface, Kind::Surface) : 0;
            if (address) attachTextureSurface(args[0], address);
            write(args[2], address);
            return status;
        }
        if (method == 16) {
            *argumentCount = 5;
            if (!args[2] || resourceLocks.count(lockKey(args[0], args[1]))) return D3DERR_INVALIDCALL;
            D3DSURFACE_DESC desc{};
            HRESULT status = texture->GetLevelDesc(args[1], &desc);
            if (FAILED(status)) return status;
            D3DLOCKED_RECT lock{};
            auto rect = static_cast<const RECT*>(pointer(args[3]));
            status = texture->LockRect(args[1], &lock, rect, args[4]);
            return FAILED(status) ? status : lockPixels(args[0], args[1], desc, lock, rect, args[4], args[2]);
        }
        if (method == 17) { *argumentCount = 2; copyLockBack(args[0], args[1]); HRESULT status = texture->UnlockRect(args[1]); releaseLock(args[0], args[1]); return status; }
    }
    if (kind == uint32_t(Kind::Cube)) {
        auto texture = static_cast<IDirect3DCubeTexture8*>(object);
        if (method == 15) {
            *argumentCount = 4;
            IDirect3DSurface8* surface = nullptr;
            HRESULT status = texture->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(args[1]), args[2], &surface);
            uint32_t address = SUCCEEDED(status) ? wrap(surface, Kind::Surface) : 0;
            if (address) attachTextureSurface(args[0], address);
            write(args[3], address);
            return status;
        }
        if (method == 16) {
            *argumentCount = 6;
            uint32_t subresource = args[1] * 32 + args[2];
            if (!args[3] || resourceLocks.count(lockKey(args[0], subresource))) return D3DERR_INVALIDCALL;
            D3DSURFACE_DESC desc{};
            HRESULT status = texture->GetLevelDesc(args[2], &desc);
            if (FAILED(status)) return status;
            D3DLOCKED_RECT lock{};
            auto rect = static_cast<const RECT*>(pointer(args[4]));
            status = texture->LockRect(static_cast<D3DCUBEMAP_FACES>(args[1]), args[2], &lock, rect, args[5]);
            return FAILED(status) ? status : lockPixels(args[0], subresource, desc, lock, rect, args[5], args[3]);
        }
        if (method == 17) {
            *argumentCount = 3;
            uint32_t subresource = args[1] * 32 + args[2];
            copyLockBack(args[0], subresource);
            HRESULT status = texture->UnlockRect(static_cast<D3DCUBEMAP_FACES>(args[1]), args[2]);
            releaseLock(args[0], subresource);
            return status;
        }
    }
    if (kind == uint32_t(Kind::Surface)) {
        auto surface = static_cast<IDirect3DSurface8*>(object);
        if (method == 9) {
            *argumentCount = 4;
            if (!args[1] || resourceLocks.count(lockKey(args[0], 0))) return D3DERR_INVALIDCALL;
            D3DSURFACE_DESC desc{};
            HRESULT status = surface->GetDesc(&desc);
            if (FAILED(status)) return status;
            D3DLOCKED_RECT lock{};
            auto rect = static_cast<const RECT*>(pointer(args[2]));
            status = surface->LockRect(&lock, rect, args[3]);
            return FAILED(status) ? status : lockPixels(args[0], 0, desc, lock, rect, args[3], args[1]);
        }
        if (method == 10) { *argumentCount = 1; copyLockBack(args[0], 0); HRESULT status = surface->UnlockRect(); releaseLock(args[0], 0); return status; }
    }
    if (kind == uint32_t(Kind::VertexBuffer) || kind == uint32_t(Kind::IndexBuffer)) {
        bool vertex = kind == uint32_t(Kind::VertexBuffer);
        if (method == 11) {
            *argumentCount = 5;
            return vertex ? lockBuffer<IDirect3DVertexBuffer8, D3DVERTEXBUFFER_DESC>(args[0], args[1], args[2], args[3], args[4])
                          : lockBuffer<IDirect3DIndexBuffer8, D3DINDEXBUFFER_DESC>(args[0], args[1], args[2], args[3], args[4]);
        }
        if (method == 12) {
            *argumentCount = 1;
            auto open = resourceLocks.find(lockKey(args[0], 0));
            if (open == resourceLocks.end()) return D3D_OK;
            if (open->second.depth > 1) { --open->second.depth; return D3D_OK; }
            copyLockBack(args[0], 0);
            HRESULT status = vertex ? static_cast<IDirect3DVertexBuffer8*>(object)->Unlock() : static_cast<IDirect3DIndexBuffer8*>(object)->Unlock();
            releaseLock(args[0], 0);
            return status;
        }
    }
#include "d3d8_methods.inc"
    throw std::runtime_error("Método D3D8 sin implementar: clase " + std::to_string(kind) + " ranura " + std::to_string(method));
}
