#pragma once
#include "rhi_swapchain.hpp"
#include "rhi_model_pipeline.hpp"
#include "rhi_image.hpp"
#include <string>

namespace mikudancestudio::rhi {
enum class D3D12Driver {
    Hardware,
    Warp
};
enum class D3D12CreationStage {
    Parameters,
    NativeDevice,
    Queue,
    Shaders,
    Targets,
    Ready,
    Compatibility
};
struct D3D12Options {
    ModelOptions model;
    bool debugLayer = false;
};
struct D3D12Stats {
    std::uint64_t submissions = 0, completed = 0, pipelineCreations = 0, resourceUploads = 0;
    std::size_t retiringResources = 0;
    unsigned debugErrors = 0;
};
// Destroy the swapchain and all resource/cache owners before the device.
// The queue, allocators, descriptors and fence are owned by the device.
struct D3D12Bundle {
    std::unique_ptr<Device> device;
    std::unique_ptr<Swapchain> swapchain;
    std::int64_t creationError = 0;
    D3D12CreationStage stage = D3D12CreationStage::Parameters;
    std::string adapterName, creationReason;
    std::uint32_t vendorId = 0, deviceId = 0;
    ModelSupport modelSupport;
    explicit operator bool() const { return device && swapchain; }
};
#ifdef MIKUDANCESTUDIO_RHI_D3D12
D3D12Bundle CreateD3D12Device(const SwapchainDesc&, D3D12Driver, D3D12Options = {});
D3D12Stats QueryD3D12Stats(Device&);
bool ReadD3D12Swapchain(Device&, RgbaImage&);
// Diagnostic for an isolated test process only. D3D12 may share a native device
// between backend instances on one adapter; removal affects those instances.
// This is native object removal, not a driver-wide TDR.
bool RemoveD3D12DeviceForTest(Device&);
#endif
}
