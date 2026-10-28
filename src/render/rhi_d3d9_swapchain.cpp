#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include "mikudancestudio/rhi_d3d9.hpp"
namespace mikudancestudio::rhi {
namespace {
D3DFORMAT Depth(SurfaceDepth d) {
    return d == SurfaceDepth::Depth16   ? D3DFMT_D16
           : d == SurfaceDepth::Depth24 ? D3DFMT_D24X8
                                        : D3DFMT_D24S8;
}
D3DFORMAT Color(SurfaceColor c) {
    return c == SurfaceColor::Rgb8 ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8;
}
D3DPRESENT_PARAMETERS Parameters(const SwapchainDesc& d) {
    D3DPRESENT_PARAMETERS p{};
    p.hDeviceWindow = reinterpret_cast<HWND>(d.window.handle);
    p.BackBufferWidth = d.width;
    p.BackBufferHeight = d.height;
    p.BackBufferFormat = Color(d.color);
    p.BackBufferCount = 1;
    p.Windowed = d.windowed;
    p.SwapEffect = d.swap == SurfaceSwap::Copy ? D3DSWAPEFFECT_COPY : D3DSWAPEFFECT_DISCARD;
    p.EnableAutoDepthStencil = d.depth != SurfaceDepth::None;
    p.AutoDepthStencilFormat = Depth(d.depth);
    p.MultiSampleType =
        d.samples == 1 ? D3DMULTISAMPLE_NONE : static_cast<D3DMULTISAMPLE_TYPE>(d.samples);
    p.MultiSampleQuality = d.quality;
    p.PresentationInterval = d.present == PresentMode::Immediate ? D3DPRESENT_INTERVAL_IMMEDIATE
                                                                 : D3DPRESENT_INTERVAL_ONE;
    return p;
}
class D3D9Swapchain final : public Swapchain {
    Device& device;
    IDirect3DDevice9* native;
    SwapchainDesc desc;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    bool retired = false;
    long lastError = 0;

public:
    D3D9Swapchain(Device& d, IDirect3DDevice9* n, SwapchainDesc initial,
                  D3DDEVICE_CREATION_PARAMETERS c)
        : device(d), native(n), desc(initial), creation(c) {
        native->AddRef();
    }
    ~D3D9Swapchain() override { native->Release(); }
    SwapchainDesc Description() const override { return desc; }
    std::int64_t LastNativeError() const override { return lastError; }
    SurfaceHealth Health() const override {
        if (retired)
            return SurfaceHealth::Removed;
        auto hr = native->TestCooperativeLevel();
        if (hr == D3D_OK)
            return SurfaceHealth::Ready;
        if (hr == D3DERR_DEVICELOST)
            return SurfaceHealth::TemporarilyLost;
        if (hr == D3DERR_DEVICENOTRESET)
            return SurfaceHealth::Resettable;
        if (hr == D3DERR_DRIVERINTERNALERROR)
            return SurfaceHealth::Removed;
        return SurfaceHealth::Unknown;
    }
    SwapchainSupport Query(const SwapchainDesc& d) const override {
        SwapchainSupport out;
        if (!ValidSwapchainDesc(d) || d.window.system != WindowSystem::Win32 || !d.windowed ||
            !IsWindow(reinterpret_cast<HWND>(d.window.handle)))
            return out;
        IDirect3D9* api = nullptr;
        if (FAILED(native->GetDirect3D(&api)))
            return out;
        D3DDISPLAYMODE mode{};
        D3DCAPS9 caps{};
        bool ok =
            SUCCEEDED(api->GetAdapterDisplayMode(creation.AdapterOrdinal, &mode)) &&
            SUCCEEDED(api->GetDeviceCaps(creation.AdapterOrdinal, creation.DeviceType, &caps)) &&
            (d.present != PresentMode::Immediate ||
             (caps.PresentationIntervals & D3DPRESENT_INTERVAL_IMMEDIATE)) &&
            SUCCEEDED(api->CheckDeviceType(creation.AdapterOrdinal, creation.DeviceType,
                                           mode.Format, Color(d.color), TRUE));
        auto sample =
            d.samples == 1 ? D3DMULTISAMPLE_NONE : static_cast<D3DMULTISAMPLE_TYPE>(d.samples);
        DWORD colors = 0, depths = 0;
        ok = ok &&
             SUCCEEDED(api->CheckDeviceMultiSampleType(creation.AdapterOrdinal, creation.DeviceType,
                                                       Color(d.color), TRUE, sample, &colors));
        if (d.depth != SurfaceDepth::None) {
            ok = ok &&
                 SUCCEEDED(api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType,
                                                  mode.Format, D3DUSAGE_DEPTHSTENCIL,
                                                  D3DRTYPE_SURFACE, Depth(d.depth))) &&
                 SUCCEEDED(api->CheckDepthStencilMatch(creation.AdapterOrdinal, creation.DeviceType,
                                                       mode.Format, Color(d.color),
                                                       Depth(d.depth))) &&
                 SUCCEEDED(api->CheckDeviceMultiSampleType(creation.AdapterOrdinal,
                                                           creation.DeviceType, Depth(d.depth),
                                                           TRUE, sample, &depths));
            if (depths < colors)
                colors = depths;
        }
        api->Release();
        out.qualityLevels = d.samples == 1 ? 1 : colors;
        out.supported = ok && d.quality < out.qualityLevels;
        out.resize = out.reset = out.controllerOwnsReset = out.supported;
        return out;
    }
    RebuildResult Rebuild(const SwapchainDesc& next, RebuildReason reason) override {
        if (retired)
            return RebuildResult::ReplacementRequired;
        if (reason == RebuildReason::DeviceReplacement) {
            return Retire() ? RebuildResult::ReplacementRequired : RebuildResult::Busy;
        }
        if (reason != RebuildReason::Resize && reason != RebuildReason::TemporaryLoss)
            return RebuildResult::Invalid;
        if (!ValidSwapchainDesc(next))
            return RebuildResult::Invalid;
        // Loss must stop submissions even if capability queries cannot run.
        const auto before = Health();
        if (before == SurfaceHealth::TemporarilyLost || before == SurfaceHealth::Removed ||
            before == SurfaceHealth::Unknown) {
            if (device.Status().state == DeviceState::Ready && !device.PrepareForReset())
                return RebuildResult::Busy;
            if (before == SurfaceHealth::TemporarilyLost)
                return RebuildResult::RetryLater;
            if (before == SurfaceHealth::Removed)
                return RebuildResult::ReplacementRequired;
            return RebuildResult::Failed;
        }
        if (!Query(next).supported)
            return RebuildResult::Unsupported;
        if (device.Status().state == DeviceState::Ready && !device.PrepareForReset())
            return RebuildResult::Busy;
        if (device.Status().state != DeviceState::ResetPending)
            return RebuildResult::Failed;
        const auto health = Health();
        if (health == SurfaceHealth::TemporarilyLost)
            return RebuildResult::RetryLater;
        if (health == SurfaceHealth::Removed)
            return RebuildResult::ReplacementRequired;
        if (health != SurfaceHealth::Ready && health != SurfaceHealth::Resettable)
            return RebuildResult::Failed;
        auto p = Parameters(next);
        auto hr = native->Reset(&p);
        lastError = hr;
        if (FAILED(hr)) {
            if (hr == D3DERR_DEVICELOST)
                return RebuildResult::RetryLater;
            if (hr == D3DERR_DRIVERINTERNALERROR)
                return RebuildResult::ReplacementRequired;
            return RebuildResult::Failed; // remain pending, no epoch publication
        }
        if (!device.RestoreAfterReset())
            return RebuildResult::Failed;
        desc = next;
        return RebuildResult::Success;
    }
    bool Retire() override {
        if (retired)
            return true;
        if (device.Status().state == DeviceState::Ready && !device.PrepareForReset())
            return false;
        if (device.Status().state != DeviceState::ResetPending)
            return false;
        retired = true;
        return true;
    }
};
}
std::unique_ptr<Swapchain> CreateD3D9Swapchain(Device& d, IDirect3DDevice9* native) {
    if (!native || d.BackendType() != Backend::D3D9 || d.Status().state != DeviceState::Ready)
        return {};
    D3DDEVICE_CREATION_PARAMETERS c{};
    IDirect3DSwapChain9* chain = nullptr;
    if (FAILED(native->GetCreationParameters(&c)) || FAILED(native->GetSwapChain(0, &chain)))
        return {};
    D3DPRESENT_PARAMETERS p{};
    auto hr = chain->GetPresentParameters(&p);
    chain->Release();
    if (FAILED(hr) ||
        (p.BackBufferFormat != D3DFMT_A8R8G8B8 && p.BackBufferFormat != D3DFMT_X8R8G8B8) ||
        (p.SwapEffect != D3DSWAPEFFECT_DISCARD && p.SwapEffect != D3DSWAPEFFECT_COPY) ||
        p.BackBufferCount != 1 || p.Flags != 0 || p.MultiSampleType == D3DMULTISAMPLE_NONMASKABLE ||
        (p.PresentationInterval != D3DPRESENT_INTERVAL_DEFAULT &&
         p.PresentationInterval != D3DPRESENT_INTERVAL_ONE &&
         p.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE) ||
        (p.EnableAutoDepthStencil && p.AutoDepthStencilFormat != D3DFMT_D16 &&
         p.AutoDepthStencilFormat != D3DFMT_D24S8 && p.AutoDepthStencilFormat != D3DFMT_D24X8))
        return {};
    SwapchainDesc desc;
    desc.window = {WindowSystem::Win32, reinterpret_cast<std::uintptr_t>(p.hDeviceWindow)};
    desc.color = p.BackBufferFormat == D3DFMT_X8R8G8B8 ? SurfaceColor::Rgb8 : SurfaceColor::Rgba8;
    desc.swap = p.SwapEffect == D3DSWAPEFFECT_COPY ? SurfaceSwap::Copy : SurfaceSwap::Discard;
    desc.width = p.BackBufferWidth;
    desc.height = p.BackBufferHeight;
    desc.windowed = p.Windowed != FALSE;
    desc.depth = !p.EnableAutoDepthStencil                  ? SurfaceDepth::None
                 : p.AutoDepthStencilFormat == D3DFMT_D16   ? SurfaceDepth::Depth16
                 : p.AutoDepthStencilFormat == D3DFMT_D24X8 ? SurfaceDepth::Depth24
                                                            : SurfaceDepth::Depth24Stencil8;
    desc.samples = p.MultiSampleType == D3DMULTISAMPLE_NONE
                       ? 1
                       : static_cast<std::uint32_t>(p.MultiSampleType);
    desc.quality = p.MultiSampleQuality;
    desc.present = p.PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE ? PresentMode::Immediate
                                                                           : PresentMode::VSync;
    auto out = std::make_unique<D3D9Swapchain>(d, native, desc, c);
    if (!out->Query(desc).supported)
        return {};
    return out;
}
}
