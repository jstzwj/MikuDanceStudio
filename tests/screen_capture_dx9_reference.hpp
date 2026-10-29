#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include "mikudancestudio/rhi_image.hpp"
#include "mikudancestudio/screen_capture.hpp"
#include <stdexcept>
#include <cstring>
namespace screen_reference {
using Microsoft::WRL::ComPtr;
inline void Check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("DX9 screen reference HRESULT=" + std::to_string(unsigned(hr)));
}
// Native StretchRect oracle, no Present, no visible window, no editor state.
class Capture {
    HWND window_ = nullptr;
    ComPtr<IDirect3D9> api_;
    ComPtr<IDirect3DDevice9> device_;

public:
    Capture() {
        window_ =
            CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"screen oracle", 0, 0,
                            0, 1, 1, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        api_.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!api_ || !window_)
            throw std::runtime_error("DX9 screen reference initialization");
        D3DPRESENT_PARAMETERS p{};
        p.Windowed = TRUE;
        p.SwapEffect = D3DSWAPEFFECT_DISCARD;
        p.BackBufferWidth = p.BackBufferHeight = 1;
        p.BackBufferFormat = D3DFMT_A8R8G8B8;
        p.hDeviceWindow = window_;
        Check(api_->CreateDevice(0, D3DDEVTYPE_HAL, window_,
                                 D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &p,
                                 &device_));
    }
    ~Capture() {
        device_.Reset();
        api_.Reset();
        if (window_)
            DestroyWindow(window_);
    }
    mikudancestudio::rhi::RgbaImage Copy(const mikudancestudio::rhi::RgbaImage& image,
                                         mikudancestudio::render::ScreenCaptureKind mode,
                                         unsigned size = 1024) {
        ComPtr<IDirect3DSurface9> upload, source, target, readback;
        Check(device_->CreateOffscreenPlainSurface(image.width, image.height, D3DFMT_A8R8G8B8,
                                                   D3DPOOL_SYSTEMMEM, &upload, nullptr));
        D3DLOCKED_RECT lock{};
        Check(upload->LockRect(&lock, nullptr, 0));
        for (unsigned y = 0; y < image.height; ++y)
            for (unsigned x = 0; x < image.width; ++x) {
                const auto* s = image.pixels.data() + (y * image.width + x) * 4;
                auto* d = static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch + x * 4;
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[3];
            }
        Check(upload->UnlockRect());
        Check(device_->CreateRenderTarget(image.width, image.height, D3DFMT_A8R8G8B8,
                                          D3DMULTISAMPLE_NONE, 0, FALSE, &source, nullptr));
        Check(device_->UpdateSurface(upload.Get(), nullptr, source.Get(), nullptr));
        Check(device_->CreateRenderTarget(size, size, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0,
                                          FALSE, &target, nullptr));
        const auto r = mikudancestudio::render::ScreenSourceRect(
            {0, 0, int(image.width), int(image.height)}, mode);
        const RECT rect{r.left, r.top, r.right, r.bottom};
        Check(device_->StretchRect(source.Get(), &rect, target.Get(), nullptr, D3DTEXF_LINEAR));
        Check(device_->CreateOffscreenPlainSurface(size, size, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                   &readback, nullptr));
        Check(device_->GetRenderTargetData(target.Get(), readback.Get()));
        Check(readback->LockRect(&lock, nullptr, D3DLOCK_READONLY));
        mikudancestudio::rhi::RgbaImage result{size, size};
        result.pixels.resize(std::size_t(size) * size * 4);
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x) {
                const auto* s =
                    static_cast<const unsigned char*>(lock.pBits) + y * lock.Pitch + x * 4;
                auto* d = result.pixels.data() + (y * size + x) * 4;
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = 255;
            }
        Check(readback->UnlockRect());
        return result;
    }
};
}
