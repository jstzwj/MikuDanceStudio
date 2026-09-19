// ===========================================================================
// VA 0x00440DB0 - PostDeviceReset (app method, thiscall this = Block)
//
// Releases every D3D resource that lives across a device reset (the overlay
// objects at app+0x9EB80/0x9EB88/0x9EB8C, the background texture at 0x9E3F0
// and its surface at 0x9E3F4, and the render-sub slots 0x1D534/0x1D538/
// 0x1D53C/0x1D548/0x1D550/0x1D554), drops the D3DX object at 0x1D560 into
// OnLostDevice, calls IDirect3DDevice9::Reset with the present parameters
// embedded at render-sub+0x1D4FC (the width/height pair the picture renderer
// and the canvas-size dialog grow), re-runs InitRenderStates (0x406E90),
// reapplies the 0x115-shadow-menu-keyed SetRenderState(0xA1, 0/1), brings the
// 0x1D560 object back through OnResetDevice and finally refreshes the
// viewport (0x4290F0 when app+0xA0274 fullscreen flag is set, else 0x42C810).
//
// Reached from the render-to-picture command (0x114), the canvas-size dialog
// (0xD4), the AVI record starters (0x45E820/0x464760) and the frame driver's
// device-lost recovery.  The former PostDeviceReset stub twin in
// src/app/late_ports.cpp was removed; this is the single definition now.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include "fx_slots.hpp"

namespace mikudancestudio {
namespace {

template <typename ComPtr>
void ReleaseSubSlot(ComPtr& slot) {
    if (slot != nullptr) {
        slot->Release();
        slot = nullptr;
    }
}

}  // namespace

void PostDeviceReset(MMDApp* app) {
    ReleaseSubSlot(app->CaptureTexture());                        // 0x440DC8
    ReleaseSubSlot(app->AviBackgroundTexture());                  // 0x440DE0
    ReleaseSubSlot(app->AviBackgroundSurface());                  // 0x440DF8
    ReleaseSubSlot(app->CaptureRenderTarget());                   // 0x440E10
    ReleaseSubSlot(app->CaptureSystemSurface());                  // 0x440E28

    D3DRenderer* r = app->Renderer();
    if (r == nullptr)
        return;
    ReleaseSubSlot(r->captureSurface);                            // 0x440E4A
    ReleaseSubSlot(r->depthStencilSurface);                       // 0x440E72
    ReleaseSubSlot(r->backbufferSurface);                         // 0x440E9A
    ReleaseSubSlot(r->shadowSurface);                             // 0x440EC2
    ReleaseSubSlot(r->hdrTexture);                                // 0x440EEA
    ReleaseSubSlot(r->shadowDepthSurface);                        // 0x440F12

    if (r->effect != nullptr) {
        // ID3DXEffect::OnLostDevice（vtable 槽 69 / +0x228，对应原版
        // PostDeviceReset x64 0x7FF7CB4C6308 的 call [vtbl+0x228]）。
        fx::OnLostDevice(r->effect);
    }

    // 内置 MMEffect：原版 MMHack.dll 设备包装 vtable 槽 16（0x80）的 Reset
    // 拦截器 [0x180003d30] 前半。MMD 侧 Reset（x64 sub_7FF7CB4C6160 @
    // 0x7FF7CB4C6323，call [dev+0x80]）进入拦截器后先调
    // OnLostDevice(0x180058970，打印 "Resetting MME...")——前提是
    // OnLostDevice 与 OnResetDevice 两个导出都在 MMHack DllMain 解析成功
    //（0x18000b3f3/0x18000b418 赋值 0x18006e828/0x18006e830，缺一不通知）。
    mme::OnLostDevice(app);

    IDirect3DDevice9* device = r->device;
    if (device == nullptr)
        return;
    device->Reset(&r->presentParameters);                          // 0x440F57

    // 内置 MMEffect：拦截器 [0x180003d30] 后半——OnResetDevice(0x180058A20)
    // 紧贴真 Reset 返回、InitRenderStates 之前调用。原版合成顺序：
    // D3DX OnLost → MME OnLost → Reset → MME OnReset → InitRenderStates →
    // SetRenderState(0xA1) → D3DX OnReset（2026-09 二次核验修正：此前误置于
    // D3DX OnResetDevice 之后，与原版不符）。拦截器尾部另把 qword_18006E7C8
    // 更新为 d3dpp.hDeviceWindow(+0x20)（空则 GetCreationParameters
    // [real+0x48] 的 hFocusWindow）；该缓存语义已由 mme_bridge 帧级
    // SetDrawnWindow 预置覆盖，无需在此重复。
    mme::OnResetDevice(app);

    InitRenderStates(app);                                        // 0x406E90

    const HWND hwnd = static_cast<HWND>(app->state.hwnd);
    const UINT menuState = GetMenuState(GetMenu(hwnd), 0x115, 0);// 0x440F7C
    device->SetRenderState(                                       // 0x440FAD
        static_cast<D3DRENDERSTATETYPE>(0xA1),
        (menuState & 8) != 0 ? 1 : 0);

    if (r->effect != nullptr) {
        // ID3DXEffect::OnResetDevice（vtable 槽 70 / +0x230，对应原版
        // PostDeviceReset x64 0x7FF7CB4C6398 的 call [vtbl+0x230]）。
        fx::OnResetDevice(r->effect);
    }

    if (app->FullscreenMode() != 0) {
        RefreshSeparateWindowViewport(app);                                           // 0x4290F0
    } else {
        RefreshMainWindowViewport(app);                                           // 0x42C810
    }

    app->state.recentFile0[0] = 0;                                 // 0x440FE2
    app->state.recentFile1[0] = 0;
    app->state.recentFile2[0] = 0;
}

}  // namespace mikudancestudio
