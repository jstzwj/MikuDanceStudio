#pragma once
#include "rhi_swapchain.hpp"
#include "rhi_model_pipeline.hpp"
#include "rhi_image.hpp"
#include <string>

namespace mikudancestudio::rhi {
enum class BackendDriver {
    Hardware,
    Software
};
struct BackendOptions {
    Backend backend = Backend::D3D11;
    BackendDriver driver = BackendDriver::Hardware;
    ModelOptions model;
};
// Factory owns native API selection. Destruction order is intentional: all
// SceneRenderer owners must detach before the swapchain and device are freed.
struct BackendDevice {
    std::unique_ptr<Device> device;
    std::unique_ptr<Swapchain> swapchain;
    std::string reason, adapter;
    std::int64_t nativeError = 0;
    explicit operator bool() const { return device && swapchain; }
};
bool BackendAvailable(Backend);
BackendDevice CreateBackendDevice(const SwapchainDesc&, const BackendOptions&);
// Explicit acceptance tool; never used by ordinary preview submission.
bool ReadBackendPresentation(Backend, Device&, RgbaImage&);
}
