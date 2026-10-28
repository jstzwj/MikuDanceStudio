#pragma once
#include "rhi_model_pipeline.hpp"
#include "rhi_swapchain.hpp"
#include <string>
namespace mikudancestudio::rhi {
enum class VulkanDriver {
    Hardware,
    Software
};
struct VulkanOptions {
    ModelOptions model;
    bool validation = false;
};
struct VulkanStats {
    std::uint64_t submissions = 0, completed = 0, pipelineCreations = 0, resourceUploads = 0;
    unsigned validationErrors = 0;
    std::int32_t lastError = 0;
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
VulkanBundle CreateVulkanOffscreenDevice(std::uint32_t width, std::uint32_t height,
                                         VulkanDriver = VulkanDriver::Hardware, VulkanOptions = {});
VulkanStats QueryVulkanStats(Device&);
bool ReadVulkanPresentation(Device&, RgbaImage&);
#ifdef _WIN32
VulkanBundle CreateVulkanDevice(const SwapchainDesc&, VulkanDriver = VulkanDriver::Hardware,
                                VulkanOptions = {});
#endif
#endif
}
