// ===========================================================================
// VA 0x0042C810 - main-window D3D viewport refresh
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cstdint>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
void PicBgOverlayRefresh(MMDApp* app);  // VA 0x00417130
namespace {

struct ScreenVertex {
    float x;
    float y;
    float z;
    float rhw;
    D3DCOLOR color;
    float u;
    float v;
};
static_assert(sizeof(ScreenVertex) == 28);

void WriteQuad(IDirect3DVertexBuffer9* buffer, float left, float top,
               float right, float bottom, float uRight, float vBottom) {
    if (buffer == nullptr)
        return;

    ScreenVertex* out = nullptr;
    if (FAILED(buffer->Lock(0, 6 * sizeof(ScreenVertex),
                            reinterpret_cast<void**>(&out), 0)) ||
        out == nullptr)
        return;

    const ScreenVertex vertices[6] = {
        {right, top,    0.0f, 1.0f, 0xFFFFFFFFu, uRight, 0.0f},
        {right, bottom, 0.0f, 1.0f, 0xFFFFFFFFu, uRight, vBottom},
        {left,  top,    0.0f, 1.0f, 0xFFFFFFFFu, 0.0f,   0.0f},
        {left,  top,    0.0f, 1.0f, 0xFFFFFFFFu, 0.0f,   0.0f},
        {right, bottom, 0.0f, 1.0f, 0xFFFFFFFFu, uRight, vBottom},
        {left,  bottom, 0.0f, 1.0f, 0xFFFFFFFFu, 0.0f,   vBottom},
    };
    std::copy(std::begin(vertices), std::end(vertices), out);
    buffer->Unlock();
}

}  // namespace

void RefreshMainWindowViewport(MMDApp* app) {
    auto& s = *app;
    RECT& view = s.ViewportRect();                     // 0xA0D40
    const HWND hwnd = app->MainWindow();
    if (hwnd == nullptr)
        return;

    GetClientRect(hwnd, &view);                         // 0x42C82D/85F

    // A0D38 selects the separate render window.  The original delegates the
    // remainder to 0x4290F0; its main-window-independent RECT/sidebar stores
    // still have to happen here before that call.
    if (s.FloatingWindow() != nullptr) {
        s.SidebarWidth() = view.right - 3;
        RefreshSeparateWindowViewport(app);
        return;
    }

    const int renderWidth = s.RenderWidth();
    const int renderHeight = s.RenderHeight();
    if (renderWidth <= 0 || renderHeight <= 0)
        return;

    const int availableHeight = view.bottom - 217;
    const int sidebar = s.SidebarWidth();
    const int availableWidth = view.right - sidebar - 9;
    const int fittedWidth = availableHeight * renderWidth / renderHeight;

    if (availableWidth >= fittedWidth) {                // 0x42C8DD
        view.top += 25;
        view.bottom -= 192;
        const int center = availableWidth / 2 + sidebar + 9;
        view.left = center - fittedWidth / 2;
        view.right = center + fittedWidth / 2;
    } else {                                            // 0x42C89C
        const int fittedHeight =
            availableWidth * renderHeight / renderWidth;
        view.left = sidebar + 9;
        view.top = availableHeight / 2 - fittedHeight / 2 + 25;
        view.bottom = fittedHeight / 2 + availableHeight / 2 + 25;
    }

    if (view.left < 0)
        view.left = 0;
    if (view.right < view.left)
        view.right = view.left + 1;
    if (view.top < 0)
        view.top = 0;
    if (view.bottom < view.top)
        view.bottom = view.top + 1;

    D3DRenderer* wrapper = app->Renderer();
    if (wrapper == nullptr)
        return;
    IDirect3DDevice9* device = wrapper->device;
    if (device == nullptr)
        return;

    D3DVIEWPORT9 viewport{};
    if (s.RecordingWindow() != nullptr) {
        viewport.Width = wrapper->multisampleAvailable == 0
            ? static_cast<DWORD>(wrapper->screenWidth)
            : static_cast<DWORD>(renderWidth);
        viewport.Height = wrapper->multisampleAvailable == 0
            ? static_cast<DWORD>(wrapper->screenHeight)
            : static_cast<DWORD>(renderHeight);
    } else {
        viewport.X = static_cast<DWORD>(view.left);
        viewport.Y = static_cast<DWORD>(view.top);
        viewport.Width = static_cast<DWORD>(view.right - view.left);
        viewport.Height = static_cast<DWORD>(view.bottom - view.top);
    }
    viewport.MinZ = 0.0f;
    viewport.MaxZ = 1.0f;
    device->SetViewport(&viewport);                     // 0x42C9E5

    const float aspect = static_cast<float>(renderWidth) /
                         static_cast<float>(renderHeight);
    wrapper->aspectRatio = aspect;
    auto& api = d3dx::Get();
    d3dx::D3DXMATRIXF projection{};
    const float fovRadians =
        s.CameraFov() * 0.01745329238474369f;
    api.perspectiveFovLH(&projection, fovRadians, aspect,
                         1.0f, 100000.0f);
    device->SetTransform(D3DTS_PROJECTION,
        reinterpret_cast<const D3DMATRIX*>(&projection));

    wrapper->viewScale = static_cast<float>(
        static_cast<double>(viewport.Width) * 1.2 / 1280.0);

    const float width = static_cast<float>(view.right - view.left);
    WriteQuad(s.LeftViewportVertices(),
              static_cast<float>(view.right) - width / 3.0f,
              static_cast<float>(view.top),
              static_cast<float>(view.right),
              static_cast<float>(view.top) + width / 3.0f,
              1.0f, 1.0f);
    WriteQuad(s.RightViewportVertices(),
              static_cast<float>(view.right) - 220.0f,
              static_cast<float>(view.top),
              static_cast<float>(view.right) - 60.0f,
              static_cast<float>(view.top) + 120.0f,
              0.625f, 0.9375f);
}

void RefreshSeparateWindowViewport(MMDApp* app) {  // 0x4290F0..0x42976B
    auto& s = *app;
    RECT& view = s.ViewportRect();
    const int renderWidth = s.RenderWidth();
    const int renderHeight = s.RenderHeight();
    if (renderWidth <= 0 || renderHeight <= 0)
        return;

    const bool fullScreen = s.FullscreenMode() != 0;
    HWND target = fullScreen
        ? app->MainWindow()
        : s.FloatingWindow();
    if (target == nullptr || !GetClientRect(target, &view))
        return;

    int clientWidth = view.right;
    int clientHeight = view.bottom;
    int desiredWidth = clientHeight * renderWidth / renderHeight;
    if (fullScreen) {
        if (clientWidth < desiredWidth) {
            const int fitted = clientWidth * renderHeight / renderWidth;
            const int center = clientHeight / 2;
            view.top = center - fitted / 2;
            view.bottom = center + fitted / 2;
        }
    } else {
        const int availableHeight = clientHeight - 57;
        desiredWidth = availableHeight * renderWidth / renderHeight;
        if (clientWidth >= desiredWidth) {
            view.top += 25;
            view.bottom -= 32;
        } else {
            const int fitted = clientWidth * renderHeight / renderWidth;
            const int center = availableHeight / 2;
            view.top = center - fitted / 2 + 25;
            view.bottom = center + fitted / 2 + 25;
        }
    }
    if (clientWidth >= desiredWidth) {
        const int width = desiredWidth;
        const int center = clientWidth / 2;
        view.left = center - width / 2;
        view.right = center + width / 2;
    }

    D3DRenderer* wrapper = app->Renderer();
    if (wrapper == nullptr)
        return;
    IDirect3DDevice9* device = wrapper->device;
    if (device == nullptr)
        return;

    D3DVIEWPORT9 viewport{};
    if (s.RecordingWindow() != nullptr) {
        viewport.Width = wrapper->multisampleAvailable == 0
            ? static_cast<DWORD>(wrapper->screenWidth)
            : static_cast<DWORD>(renderWidth);
        viewport.Height = wrapper->multisampleAvailable == 0
            ? static_cast<DWORD>(wrapper->screenHeight)
            : static_cast<DWORD>(renderHeight);
    } else {
        viewport.X = static_cast<DWORD>(view.left);
        viewport.Y = static_cast<DWORD>(view.top);
        viewport.Width = static_cast<DWORD>(view.right - view.left);
        viewport.Height = static_cast<DWORD>(view.bottom - view.top);
    }
    viewport.MinZ = 0.0f;
    viewport.MaxZ = 1.0f;
    device->SetViewport(&viewport);

    const float aspect = static_cast<float>(renderWidth) /
                         static_cast<float>(renderHeight);
    wrapper->aspectRatio = aspect;
    auto& api = d3dx::Get();
    d3dx::D3DXMATRIXF projection{};
    api.perspectiveFovLH(&projection,
        s.CameraFov() * 0.01745329238474369f,
        aspect, 1.0f, 100000.0f);
    device->SetTransform(D3DTS_PROJECTION,
        reinterpret_cast<const D3DMATRIX*>(&projection));
    wrapper->viewScale = static_cast<float>(
        static_cast<double>(view.right - view.left) * 1.2 / 1280.0);

    if (s.WaveEnabled() != 0)
        TimelineDrawTicks(s.TimelineStartFrame(),
                          s.SidebarWidth());
    if (s.PictureBackgroundEnabled() != 0)
        PicBgOverlayRefresh(app);

    const float width = static_cast<float>(view.right - view.left);
    WriteQuad(s.LeftViewportVertices(),
              static_cast<float>(view.right) - width / 3.0f,
              static_cast<float>(view.top),
              static_cast<float>(view.right),
              static_cast<float>(view.top) + width / 3.0f,
              1.0f, 1.0f);
    WriteQuad(s.RightViewportVertices(),
              static_cast<float>(view.right) - 220.0f,
              static_cast<float>(view.top),
              static_cast<float>(view.right) - 60.0f,
              static_cast<float>(view.top) + 120.0f,
              0.625f, 0.9375f);
}

}  // namespace mikudancestudio
