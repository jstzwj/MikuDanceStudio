#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include <d3dx9.h>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};
struct Window {
    HWND handle = CreateWindowW(L"STATIC", L"Axis mesh regression", WS_OVERLAPPED,
        0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() { if (handle) DestroyWindow(handle); }
};
}

// Exercise the production resource loader with the actual six-material axis.
// Compare every material byte to D3DX's buffer, including the second and last
// records that an x86 stride misreads on x64.
int main() {
    using namespace mikudancestudio;
    Window window;
    Com<IDirect3D9> d3d;
    d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
    if (!window.handle || !d3d.p) return 77;
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = window.handle;
    Com<IDirect3DDevice9> device;
    if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
        window.handle, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device.p)))
        return 77;
    auto app = std::make_unique<MMDApp>();
    std::memset(&app->state, 0, sizeof(app->state));
    auto renderer = std::make_unique<D3DRenderer>();
    renderer->device = device.p;
    app->Renderer() = renderer.get();
    mdl::AccessoryRecord axis{};
    app->AxisMeshObject() = &axis;
    if (!InitAxisMesh(app.get())) return 1;

    HRSRC resource = FindResourceA(nullptr, MAKEINTRESOURCEA(115), "XFILE");
    const void* bytes = LockResource(LoadResource(nullptr, resource));
    Com<ID3DXBuffer> reference;
    Com<ID3DXMesh> mesh;
    DWORD count = 0;
    const HRESULT hr = D3DXLoadMeshFromXInMemory(bytes, SizeofResource(nullptr, resource),
        544, device.p, nullptr, &reference.p, nullptr, &count, &mesh.p);
    bool equal = SUCCEEDED(hr) && reference.p && count == 6 && axis.materialCount == count;
    if (equal) {
        const auto* expected = static_cast<const D3DXMATERIAL*>(reference->GetBufferPointer());
        const auto* actual = static_cast<const D3DMATERIAL9*>(axis.materials);
        for (DWORD i = 0; i < count; ++i)
            if (std::memcmp(&actual[i], &expected[i].MatD3D, sizeof(actual[i])) != 0) {
                std::fprintf(stderr, "Axis material %lu differs from D3DX\n", i);
                equal = false;
            }
    }
    DisposeAccessory(&axis);
    return equal ? 0 : 1;
}
