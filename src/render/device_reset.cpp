#include "mikudancestudio/rhi_history_replay.hpp"
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
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <unordered_map>
#ifdef MIKUDANCESTUDIO_DIAG
#include <filesystem>
#include <vector>
#endif
#include "mikudancestudio/application_swapchain.hpp"

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/rhi_d3d9.hpp"
#include "mikudancestudio/model_batch_bridge.hpp"

#include "fx_slots.hpp"

namespace mikudancestudio {
namespace {

template <typename ComPtr> void ReleaseSubSlot(ComPtr& slot) {
    if (slot != nullptr) {
        slot->Release();
        slot = nullptr;
    }
}

struct ApplicationSurface {
    IDirect3DDevice9* native = nullptr; // retained by adapter/controller
    std::unique_ptr<rhi::Device> adapter;
    std::unique_ptr<rhi::Swapchain> controller;
    bool blocked = false, released = false;
};
std::unordered_map<MMDApp*, ApplicationSurface> surfaces;
void BlockSurface(ApplicationSurface& state) {
    state.blocked = true;
    if (state.adapter && state.adapter->Status().state == rhi::DeviceState::Ready)
        state.adapter->PrepareForReset();
}
void LogReset(MMDApp* app, const char* phase, const char* reason, long hr,
              const rhi::DeviceStatus& status) {
    wchar_t path[32768]{};
    if (!GetEnvironmentVariableW(L"MDS_RHI_SWAPCHAIN_LOG", path, 32768))
        return;
    if (auto* f = _wfopen(path, L"a")) {
        const auto& p = app->Renderer()->presentParameters;
        std::fprintf(
            f,
            "phase=%s reason=%s controller=%d width=%u height=%u color=%u depth=%u samples=%u quality=%u swap=%u interval=%u window=%p hr=0x%08lx device=%llu epoch=%llu state=%u\n",
            phase, reason, ApplicationSwapchainEnabled(), p.BackBufferWidth, p.BackBufferHeight,
            static_cast<unsigned>(p.BackBufferFormat),
            static_cast<unsigned>(p.AutoDepthStencilFormat),
            static_cast<unsigned>(p.MultiSampleType), p.MultiSampleQuality,
            static_cast<unsigned>(p.SwapEffect), p.PresentationInterval, p.hDeviceWindow,
            static_cast<unsigned long>(hr), static_cast<unsigned long long>(status.identity),
            static_cast<unsigned long long>(status.epoch), static_cast<unsigned>(status.state));
        std::fclose(f);
    }
}
bool Describe(const D3DPRESENT_PARAMETERS& p, rhi::SwapchainDesc& out) {
    if ((p.BackBufferFormat != D3DFMT_X8R8G8B8 && p.BackBufferFormat != D3DFMT_A8R8G8B8) ||
        (p.EnableAutoDepthStencil && p.AutoDepthStencilFormat != D3DFMT_D16 &&
         p.AutoDepthStencilFormat != D3DFMT_D24S8 && p.AutoDepthStencilFormat != D3DFMT_D24X8) ||
        (p.SwapEffect != D3DSWAPEFFECT_DISCARD && p.SwapEffect != D3DSWAPEFFECT_COPY) ||
        p.BackBufferCount != 1 || p.Flags || p.FullScreen_RefreshRateInHz ||
        p.MultiSampleType == D3DMULTISAMPLE_NONMASKABLE ||
        (p.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE &&
         p.PresentationInterval != D3DPRESENT_INTERVAL_ONE &&
         p.PresentationInterval != D3DPRESENT_INTERVAL_DEFAULT))
        return false;
    out.window = {rhi::WindowSystem::Win32, reinterpret_cast<std::uintptr_t>(p.hDeviceWindow)};
    out.width = p.BackBufferWidth;
    out.height = p.BackBufferHeight;
    out.windowed = p.Windowed != FALSE;
    out.color =
        p.BackBufferFormat == D3DFMT_X8R8G8B8 ? rhi::SurfaceColor::Rgb8 : rhi::SurfaceColor::Rgba8;
    out.depth = !p.EnableAutoDepthStencil                  ? rhi::SurfaceDepth::None
                : p.AutoDepthStencilFormat == D3DFMT_D16   ? rhi::SurfaceDepth::Depth16
                : p.AutoDepthStencilFormat == D3DFMT_D24X8 ? rhi::SurfaceDepth::Depth24
                                                           : rhi::SurfaceDepth::Depth24Stencil8;
    out.samples =
        p.MultiSampleType == D3DMULTISAMPLE_NONE ? 1 : static_cast<unsigned>(p.MultiSampleType);
    out.quality = p.MultiSampleQuality;
    out.swap =
        p.SwapEffect == D3DSWAPEFFECT_COPY ? rhi::SurfaceSwap::Copy : rhi::SurfaceSwap::Discard;
    out.present = p.PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE
                      ? rhi::PresentMode::Immediate
                      : rhi::PresentMode::VSync;
    return rhi::ValidSwapchainDesc(out);
}
} // namespace

bool ApplicationSwapchainEnabled() {
#ifdef MIKUDANCESTUDIO_RHI_D3D9
    char flag[2]{};
    return GetEnvironmentVariableA("MDS_RHI_SWAPCHAIN_CONTROLLER", flag, 2) == 1 && flag[0] == '1';
#else
    return false;
#endif
}
void ForgetApplicationSwapchain(MMDApp* app) {
    const auto found = surfaces.find(app);
    if (found != surfaces.end()) {
        if (found->second.controller)
            found->second.controller->Retire();
        rhi::ForgetApplicationOverlayCache(found->second.native);
        surfaces.erase(found);
    }
}
bool ApplicationSwapchainCanRender(MMDApp* app) {
    const auto it = surfaces.find(app);
    return it == surfaces.end() || !it->second.blocked;
}
bool PollApplicationSwapchain(MMDApp* app) {
    if (ApplicationSwapchainEnabled() && IsIconic(static_cast<HWND>(app->Hwnd())))
        return false;
    const auto it = surfaces.find(app);
    if (it == surfaces.end())
        return true;
    if (it->second.native != app->Renderer()->device) {
        ForgetApplicationSwapchain(app);
        PostDeviceReset(app);
    } else if (it->second.blocked) {
        const auto health = app->Renderer()->device->TestCooperativeLevel();
        if (health == D3D_OK || health == D3DERR_DEVICENOTRESET)
            PostDeviceReset(app);
    }
    return ApplicationSwapchainCanRender(app);
}

void PostDeviceReset(MMDApp* app) {
    auto* renderer = app->Renderer();
    if (!renderer || !renderer->device)
        return;
    const bool controlled = ApplicationSwapchainEnabled();
    auto found = surfaces.find(app);
    if (found != surfaces.end() && found->second.native != renderer->device) {
        ForgetApplicationSwapchain(app);
        found = surfaces.end();
    }
    ApplicationSurface* lifecycle = nullptr;
    if (controlled || found != surfaces.end()) {
        auto& state = surfaces[app];
        lifecycle = &state;
        if (!state.adapter) {
#ifdef MIKUDANCESTUDIO_RHI_D3D9
            state.native = renderer->device;
            state.adapter = rhi::CreateD3D9Device(state.native);
            if (state.adapter)
                state.controller = rhi::CreateD3D9Swapchain(*state.adapter, state.native);
#endif
        }
        if (controlled && (!state.adapter || !state.controller)) {
            BlockSurface(state);
            LogReset(app, "unsupported_attach", "rebuild", D3DERR_NOTAVAILABLE,
                     state.adapter ? state.adapter->Status() : rhi::DeviceStatus{});
            return;
        }
    }
    rhi::SwapchainDesc requested;
    if (controlled && !Describe(renderer->presentParameters, requested)) {
        BlockSurface(*lifecycle);
        LogReset(app, "unsupported_parameters", "rebuild", D3DERR_INVALIDCALL,
                 lifecycle->adapter->Status());
        return;
    }
    if (controlled && !lifecycle->controller->Query(requested).supported) {
        BlockSurface(*lifecycle);
        LogReset(app, "unsupported_query", "rebuild", D3DERR_NOTAVAILABLE,
                 lifecycle->adapter->Status());
        return;
    }
    // The native reset path is called only after the frame driver has polled
    // the device. An extra poll changes that recovery sequence; only the
    // optional lifecycle controller needs its own health classification.
    const auto cooperative = lifecycle ? renderer->device->TestCooperativeLevel() : D3D_OK;
    const bool resized = lifecycle && lifecycle->controller &&
                         (lifecycle->controller->Description().width !=
                              renderer->presentParameters.BackBufferWidth ||
                          lifecycle->controller->Description().height !=
                              renderer->presentParameters.BackBufferHeight);
    const char* reason = cooperative != D3D_OK ? "temporary_loss"
                         : resized             ? "resize"
                                               : "explicit_rebuild";
    if (lifecycle)
        lifecycle->blocked = true;
    // Lost callbacks and release run once across failed attempts.
    if (!lifecycle || !lifecycle->released) {
        app->CpuNativeCaptureTexturePresent() = false;
        app->CpuNativeBackgroundTexturePresent() = false;
        ReleaseSubSlot(app->CaptureTexture());       // 0x440DC8
        ReleaseSubSlot(app->AviBackgroundTexture()); // 0x440DE0
        ReleaseSubSlot(app->AviBackgroundSurface()); // 0x440DF8
        ReleaseSubSlot(app->CaptureRenderTarget());  // 0x440E10
        ReleaseSubSlot(app->CaptureSystemSurface()); // 0x440E28

        D3DRenderer* r = app->Renderer();
        if (r == nullptr)
            return;
        ReleaseSubSlot(r->captureSurface);      // 0x440E4A
        ReleaseSubSlot(r->depthStencilSurface); // 0x440E72
        ReleaseSubSlot(r->backbufferSurface);   // 0x440E9A
        ReleaseSubSlot(r->shadowSurface);       // 0x440EC2
        ReleaseSubSlot(r->hdrTexture);          // 0x440EEA
        ReleaseSubSlot(r->shadowDepthSurface);  // 0x440F12

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
        rhi::ForgetApplicationOverlayCache(device);
        ResetHistoryReplay();
        InvalidateModelBatchComparison();
        if (lifecycle)
            lifecycle->released = true;
        LogReset(app, "released", reason, D3D_OK,
                 lifecycle ? lifecycle->adapter->Status() : rhi::DeviceStatus{});
    }
    D3DRenderer* r = renderer;
    IDirect3DDevice9* device = r->device;
    HRESULT resetResult = D3D_OK;
    bool resetSucceeded = false;
    if (controlled) {
        const auto result = lifecycle->controller->Rebuild(
            requested,
            cooperative == D3D_OK ? rhi::RebuildReason::Resize : rhi::RebuildReason::TemporaryLoss);
        resetResult = static_cast<HRESULT>(lifecycle->controller->LastNativeError());
        resetSucceeded = result == rhi::RebuildResult::Success;
        if (!resetSucceeded) {
            const char* phase = result == rhi::RebuildResult::RetryLater ? "controller_retry_later"
                                : result == rhi::RebuildResult::ReplacementRequired
                                    ? "controller_replacement_required"
                                : result == rhi::RebuildResult::Busy ? "controller_busy"
                                                                     : "controller_failed";
            LogReset(app, phase, reason, resetResult, lifecycle->adapter->Status());
        }
    } else {
        // A diagnostic may compare legacy Reset while a controller is attached.
        // Keep the attached adapter's epoch valid even for this direct call.
        bool prepared = !lifecycle ||
                        lifecycle->adapter->Status().state == rhi::DeviceState::ResetPending ||
                        lifecycle->adapter->PrepareForReset();
        resetResult = prepared ? device->Reset(&r->presentParameters) : D3DERR_INVALIDCALL;
        resetSucceeded =
            SUCCEEDED(resetResult) && (!lifecycle || lifecycle->adapter->RestoreAfterReset());
    }
    LogReset(app, resetSucceeded ? "native_success" : "native_failed", reason, resetResult,
             lifecycle ? lifecycle->adapter->Status() : rhi::DeviceStatus{});
    if (!resetSucceeded && lifecycle)
        return; // no MME/D3DX reset callback, no draw

    // Embedded MMEffect: second half of interceptor [0x180003d30]; OnResetDevice (0x180058A20)
    // is called immediately after real Reset returns, before InitRenderStates. Original combined order:
    // D3DX OnLost → MME OnLost → Reset → MME OnReset → InitRenderStates →
    // SetRenderState(0xA1) -> D3DX OnReset (2026-09 recheck correction: previously misplaced after
    // D3DX OnResetDevice, contrary to the original). The interceptor tail also updates qword_18006E7C8
    // to d3dpp.hDeviceWindow(+0x20), or, if null, hFocusWindow from GetCreationParameters
    // [real+0x48]. The mme_bridge per-frame SetDrawnWindow setup already covers this cached state,
    // so it need not be duplicated here.
    mme::OnResetDevice(app);
    LogReset(app, "mme_reset", reason, D3D_OK,
             lifecycle ? lifecycle->adapter->Status() : rhi::DeviceStatus{});

    app->CpuRenderFrame().groundAlphaBlend = false;
    InitRenderStates(app); // 0x406E90

    const HWND hwnd = static_cast<HWND>(app->state.hwnd);
    const UINT menuState = GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_ANTIALIASING, 0); // 0x440F7C
    device->SetRenderState(                                                             // 0x440FAD
        static_cast<D3DRENDERSTATETYPE>(0xA1), (menuState & 8) != 0 ? 1 : 0);

    if (r->effect != nullptr) {
        // ID3DXEffect::OnResetDevice（vtable 槽 70 / +0x230，对应原版
        // PostDeviceReset x64 0x7FF7CB4C6398 的 call [vtbl+0x230]）。
        const HRESULT effectResult = fx::OnResetDevice(r->effect);
        if (lifecycle && FAILED(effectResult)) {
            lifecycle->released = false;
            LogReset(app, "effect_restore_failed", reason, effectResult,
                     lifecycle->adapter->Status());
            return;
        }
    }

    if (app->FullscreenMode() != 0) {
        RefreshSeparateWindowViewport(app); // 0x4290F0
    } else {
        RefreshMainWindowViewport(app); // 0x42C810
    }

    if (lifecycle) {
        lifecycle->released = false;
        lifecycle->blocked = false;
    }
    LogReset(app, "restored", reason, resetResult,
             lifecycle ? lifecycle->adapter->Status() : rhi::DeviceStatus{});
    app->state.recentFile0[0] = 0; // 0x440FE2
    app->state.recentFile1[0] = 0;
    app->state.recentFile2[0] = 0;
}

#ifdef MIKUDANCESTUDIO_DIAG
namespace {
bool ReadApplicationImage(IDirect3DDevice9* device, std::vector<std::uint32_t>& pixels) {
    IDirect3DSurface9 *back = nullptr, *resolved = nullptr, *cpu = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
        return false;
    D3DSURFACE_DESC d{};
    back->GetDesc(&d);
    bool ok = SUCCEEDED(device->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE,
                                                   0, FALSE, &resolved, nullptr)) &&
              SUCCEEDED(device->StretchRect(back, nullptr, resolved, nullptr, D3DTEXF_NONE)) &&
              SUCCEEDED(device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format,
                                                            D3DPOOL_SYSTEMMEM, &cpu, nullptr)) &&
              SUCCEEDED(device->GetRenderTargetData(resolved, cpu));
    D3DLOCKED_RECT lock{};
    if (ok && SUCCEEDED(cpu->LockRect(&lock, nullptr, D3DLOCK_READONLY))) {
        pixels.resize(static_cast<std::size_t>(d.Width) * d.Height);
        for (unsigned y = 0; y < d.Height; ++y)
            for (unsigned x = 0; x < d.Width; ++x)
                pixels[y * d.Width + x] = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const char*>(lock.pBits) + y * lock.Pitch)[x];
        cpu->UnlockRect();
    } else
        ok = false;
    if (cpu)
        cpu->Release();
    if (resolved)
        resolved->Release();
    back->Release();
    return ok;
}
}
void RunApplicationSwapchainDiagnostic(MMDApp* app) {
    wchar_t root[32768]{};
    if (!GetEnvironmentVariableW(L"MDS_RHI_SWAPCHAIN_TEST_DIR", root, 32768))
        return;
    const std::filesystem::path directory(root);
    auto* request = _wfopen((directory / L"request.txt").c_str(), L"r");
    if (!request)
        return;
    unsigned sequence = 0, width = 0, height = 0;
    const bool parsed = std::fscanf(request, "%u %u %u", &sequence, &width, &height) == 3;
    std::fclose(request);
    static unsigned last = 0;
    if (!parsed || sequence <= last || !width || !height)
        return;
    last = sequence;
    auto* renderer = app->Renderer();
    const auto savedMessage = app->state.messageSeen;
    app->state.messageSeen = 1;
    std::vector<std::uint32_t> reference, candidate, legacyRepeat, controllerRepeat;
    bool valid = true;
    std::uint64_t beforeEpoch = 0, afterEpoch = 0;
    rhi::SubmissionIdentity old{};
    std::uint64_t spriteUploads = 0;
    // Same scene state and frame, no animation/physics step between images.
    for (int path = 0; path < 4; ++path) {
        SetEnvironmentVariableA("MDS_RHI_SWAPCHAIN_CONTROLLER", path >= 2 ? "1" : "0");
        renderer->presentParameters.BackBufferWidth = width;
        renderer->presentParameters.BackBufferHeight = height;
        renderer->screenWidth = width;
        renderer->screenHeight = height;
        if (path >= 2 && surfaces.count(app) && surfaces[app].adapter) {
            const auto status = surfaces[app].adapter->Status();
            beforeEpoch = status.epoch;
            old = {status.identity, status.epoch};
        }
        PostDeviceReset(app);
        valid = valid && ApplicationSwapchainCanRender(app);
        if (!valid)
            break;
        PrepareFrameTextOverlay(app);
        PrepareFrameLineOverlay(app);
        RenderFrameScene(app);
        const auto stats =
            rhi::ApplicationOverlayCacheStats(renderer->device, rhi::OverlayAtlasSlot::Sprite);
        spriteUploads += stats.uploads;
        valid = valid && stats.uploads == 1;
        auto& image = path == 0   ? reference
                      : path == 1 ? legacyRepeat
                      : path == 2 ? candidate
                                  : controllerRepeat;
        valid = ReadApplicationImage(renderer->device, image) && valid;
        if (path >= 2 && surfaces.count(app) && surfaces[app].adapter) {
            const auto status = surfaces[app].adapter->Status();
            afterEpoch = status.epoch;
            if (old.device)
                valid = valid && !rhi::IsCurrentSubmission(status, old) &&
                        afterEpoch == beforeEpoch + 1;
        }
    }
    SetEnvironmentVariableA("MDS_RHI_SWAPCHAIN_CONTROLLER", "1");
    bool failureRecovery = true;
    if (sequence == 1 && surfaces.count(app) && surfaces[app].adapter) {
        IDirect3DSurface9* held = nullptr;
        const auto status = surfaces[app].adapter->Status();
        failureRecovery =
            SUCCEEDED(renderer->device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &held));
        if (held) {
            PostDeviceReset(app);
            failureRecovery =
                failureRecovery && !ApplicationSwapchainCanRender(app) &&
                surfaces[app].adapter->Status().state == rhi::DeviceState::ResetPending &&
                surfaces[app].adapter->Status().epoch == status.epoch;
            held->Release();
            PostDeviceReset(app);
            failureRecovery = failureRecovery && ApplicationSwapchainCanRender(app) &&
                              surfaces[app].adapter->Status().epoch == status.epoch + 1;
        }
        valid = valid && failureRecovery;
    }
    app->state.messageSeen = savedMessage;
    std::size_t differences = 0;
    if (reference.size() != candidate.size() || reference.empty())
        valid = false;
    else
        for (std::size_t i = 0; i < reference.size(); ++i)
            differences += reference[i] != candidate[i];
    auto writeImage = [&](const wchar_t* label, const std::vector<std::uint32_t>& image) {
        const auto file = directory / (std::to_wstring(sequence) + label);
        if (auto* output = _wfopen(file.c_str(), L"wb")) {
            std::fwrite(image.data(), 4, image.size(), output);
            std::fclose(output);
        }
    };
    writeImage(L".legacy.bgra", reference);
    writeImage(L".controller.bgra", candidate);
    writeImage(L".legacy_repeat.bgra", legacyRepeat);
    writeImage(L".controller_repeat.bgra", controllerRepeat);
    auto compare = [](const auto& a, const auto& b) {
        if (a.size() != b.size() || a.empty())
            return static_cast<std::size_t>(-1);
        std::size_t count = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            count += a[i] != b[i];
        return count;
    };
    const auto legacyDifferences = compare(reference, legacyRepeat),
               controllerDifferences = compare(candidate, controllerRepeat);
    differences = compare(legacyRepeat, controllerRepeat);

    if (auto* output = _wfopen((directory / L"results.txt").c_str(), L"a")) {
        std::fprintf(
            output,
            "sequence=%u width=%u height=%u frame=%u pixels=%zu differences=%zu legacyRepeat=%zu controllerRepeat=%zu failureRecovery=%u spriteUploads=%llu beforeEpoch=%llu afterEpoch=%llu status=%s\n",
            sequence, width, height, app->CurrentFrame(), candidate.size(), differences,
            legacyDifferences, controllerDifferences, static_cast<unsigned>(failureRecovery),
            static_cast<unsigned long long>(spriteUploads),
            static_cast<unsigned long long>(beforeEpoch),
            static_cast<unsigned long long>(afterEpoch),
            valid && differences == 0 && legacyDifferences == 0 && controllerDifferences == 0
                ? "PASS"
                : "FAIL");
        std::fclose(output);
    }
}
#endif

} // namespace mikudancestudio
