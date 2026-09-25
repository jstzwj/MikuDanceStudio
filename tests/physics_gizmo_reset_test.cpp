#include <windows.h>
#include <d3d9.h>

#include <cstdio>
#include <memory>

#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/ported_funcs.hpp"

int main() {
    HWND window = CreateWindowW(L"STATIC", L"Physics gizmo reset", WS_OVERLAPPED,
                                0, 0, 128, 128, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (window == nullptr || d3d == nullptr) return 77;

    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = pp.BackBufferHeight = 128;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    IDirect3DDevice9* device = nullptr;
    const HRESULT create = d3d->CreateDevice(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(create)) {
        d3d->Release();
        DestroyWindow(window);
        return 77;
    }

    auto renderer = std::make_unique<mikudancestudio::D3DRenderer>();
    renderer->device = device;
    mikudancestudio::PhysicsScene scene{};
    const bool built = mikudancestudio::SceneConstruct(&scene, renderer.get());
    const HRESULT reset = built ? device->Reset(&pp) : E_FAIL;
    mikudancestudio::DisposePhysicsWorld(&scene);
    device->Release();
    d3d->Release();
    DestroyWindow(window);
    if (!built || FAILED(reset)) {
        std::fprintf(stderr, "Physics scene construction=%d, live-gizmo Reset=0x%08X\n",
                     built, static_cast<unsigned>(reset));
        return 1;
    }
    std::puts("Physics gizmo buffers survive a real D3D9 Reset");
    return 0;
}
