#pragma once
#include "rhi_model_pipeline.hpp"
#include "rhi_swapchain.hpp"
#include "vulkan_surface.hpp"
#include <string>
namespace mikudancestudio::rhi {
enum class VulkanDriver {
    Hardware,
    Software
};
struct VulkanOptions {
    ModelOptions model;
    bool validation = false;
    std::string loaderPath;
    std::shared_ptr<const VulkanSurfaceSource> surface;
};
struct VulkanStats {
    std::uint64_t submissions = 0, completed = 0, pipelineCreations = 0, resourceUploads = 0;
    // Native buffer allocations include frame uniforms and transfer/readback
    // staging. Wait counters count only boundaries observed still pending.
    std::uint64_t bufferAllocations = 0, bufferUploadBytes = 0, timelineWaits = 0, idleWaits = 0,
                  frameWaits = 0, transferWaits = 0, uploadBusyRejects = 0;
    std::size_t retiringResources = 0, peakRetiringResources = 0;
    unsigned validationErrors = 0;
    std::int32_t lastError = 0;
    std::uint64_t presentations = 0, readbacks = 0;
};
struct VulkanBundle {
    std::unique_ptr<Device> device;
    std::unique_ptr<Swapchain> swapchain;
    std::string adapterName, creationReason;
    std::int32_t creationError = 0;
    explicit operator bool() const { return bool(device); }
};
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
// Vulkan 1.3 graphics queue, no surface/swapchain/window extensions or HWND.
// Native device loss requires replacement; logical resets retain uploads.
// Upload rejects a buffer still used by an unfinished submission; retry after
// its completion. Destroy invalidates the handle immediately and defers native
// release to that resource's last use, without waiting for unrelated work.
VulkanBundle CreateVulkanOffscreenDevice(std::uint32_t width, std::uint32_t height,
                                         VulkanDriver = VulkanDriver::Hardware, VulkanOptions = {});
VulkanStats QueryVulkanStats(Device&);
// Deterministic completion tests on an isolated offscreen device only. Host
// release does not submit work or wait; teardown always releases the gate.
bool SetVulkanQueueBlockedForTest(Device&, bool blocked);
bool ReadVulkanPresentation(Device&, RgbaImage&);
VulkanBundle CreateVulkanDevice(const SwapchainDesc&, VulkanDriver = VulkanDriver::Hardware,
                                VulkanOptions = {});
#endif
}
