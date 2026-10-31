#pragma once
#include "rhi_swapchain.hpp"
#include <functional>
#include <string>

namespace mikudancestudio::rhi {
// The host owns the native window and supplies physical pixel dimensions.
// The lease keeps its display/window/layer alive through device teardown.
// RHI creates and owns VkSurfaceKHR, swapchains and their synchronization.
// No Vulkan or platform SDK types cross this boundary.
using VulkanProcLookup = std::function<void*(const char*)>;
struct VulkanSurfaceSource {
    NativeWindow window;
    std::shared_ptr<void> lease;
    std::vector<std::string> instanceExtensions;
    std::function<std::int32_t(std::uintptr_t, const VulkanProcLookup&, std::uint64_t&)> create;
    std::function<SurfaceHealth()> health;
};
// Win32: handle=HWND. Cocoa: handle=CAMetalLayer*. Xcb: display=connection,
// handle=window ID. Wayland: display=wl_display*, handle=wl_surface*.
// The host processes native events and supplies suspension/removal status.
std::shared_ptr<const VulkanSurfaceSource>
CreateNativeVulkanSurface(NativeWindow, std::uintptr_t display, std::shared_ptr<void> lease,
                          std::function<SurfaceHealth()> health, std::string& reason);
} // namespace mikudancestudio::rhi
