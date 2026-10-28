#pragma once
#include "mikudancestudio/rhi.hpp"

namespace mikudancestudio::rhi {
// Opaque window token interpreted only by the selected backend. No Win32 types.
enum class WindowSystem {
    None,
    Win32,
    Cocoa,
    Xcb,
    Wayland
};
struct NativeWindow {
    WindowSystem system = WindowSystem::None;
    std::uintptr_t handle = 0;
};
enum class SurfaceColor {
    Rgba8,
    Rgb8
};
enum class SurfaceDepth {
    None,
    Depth16,
    Depth24Stencil8,
    Depth24
};
enum class PresentMode {
    Immediate,
    VSync
};
enum class SurfaceSwap {
    Discard,
    Copy
};
struct SwapchainDesc {
    NativeWindow window;
    std::uint32_t width = 0, height = 0;
    SurfaceColor color = SurfaceColor::Rgba8;
    SurfaceDepth depth = SurfaceDepth::Depth24Stencil8;
    std::uint32_t samples = 1, quality = 0;
    PresentMode present = PresentMode::VSync;
    bool windowed = true;
    SurfaceSwap swap = SurfaceSwap::Discard;
};
inline bool ValidSwapchainDesc(const SwapchainDesc& d) {
    return (d.window.system == WindowSystem::Win32 || d.window.system == WindowSystem::Cocoa ||
            d.window.system == WindowSystem::Xcb || d.window.system == WindowSystem::Wayland) &&
           d.window.handle && d.width && d.height &&
           (d.color == SurfaceColor::Rgba8 || d.color == SurfaceColor::Rgb8) &&
           (d.depth == SurfaceDepth::None || d.depth == SurfaceDepth::Depth16 ||
            d.depth == SurfaceDepth::Depth24Stencil8 || d.depth == SurfaceDepth::Depth24) &&
           (d.present == PresentMode::Immediate || d.present == PresentMode::VSync) &&
           (d.swap == SurfaceSwap::Discard || (d.swap == SurfaceSwap::Copy && d.samples == 1)) &&
           (d.samples == 1 || (d.samples >= 2 && d.samples <= 16)) &&
           (d.samples != 1 || d.quality == 0);
}
enum class SurfaceHealth {
    Ready,
    TemporarilyLost,
    Resettable,
    Removed,
    Unknown,
    Suspended
};
enum class RebuildReason {
    Resize,
    TemporaryLoss,
    DeviceReplacement
};
enum class RebuildResult {
    Success,
    Invalid,
    Unsupported,
    Busy,
    RetryLater,
    ReplacementRequired,
    Failed
};
struct SwapchainSupport {
    bool supported = false, resize = false, reset = false;
    bool controllerOwnsReset = false, applicationOwnsDeviceReplacement = true;
    std::uint32_t qualityLevels = 0;
};
// The controller owns presentation parameters and reset sequencing, not the
// scene, CPU assets or native device lifetime. Device must outlive controller.
class Swapchain {
public:
    virtual ~Swapchain() = default;
    virtual SwapchainDesc Description() const = 0;
    virtual SwapchainSupport Query(const SwapchainDesc&) const = 0;
    virtual SurfaceHealth Health() const = 0;
    // Native diagnostic only; portable callers branch on RebuildResult.
    virtual std::int64_t LastNativeError() const { return 0; }
    virtual RebuildResult Rebuild(const SwapchainDesc&, RebuildReason) = 0;
    // Terminal: stop old submissions before application replaces the device.
    // Release/rebind caches first when possible; keep CPU assets for reupload.
    virtual bool Retire() = 0;
};
}
