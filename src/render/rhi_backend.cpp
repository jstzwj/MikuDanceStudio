#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_d3d11.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
namespace mikudancestudio::rhi {
bool BackendAvailable(Backend backend) {
    switch (backend) {
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    case Backend::Vulkan:
        return true;
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D11
    case Backend::D3D11:
        return true;
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    case Backend::D3D12:
        return true;
#endif
    default:
        return false;
    }
}
BackendDevice CreateBackendDevice(const SwapchainDesc& desc, const BackendOptions& options) {
    BackendDevice result;
    switch (options.backend) {
#ifdef MIKUDANCESTUDIO_RHI_D3D11
    case Backend::D3D11: {
        auto native = CreateD3D11Device(
            desc,
            options.driver == BackendDriver::Software ? D3D11Driver::Warp : D3D11Driver::Hardware,
            options.model);
        result.device = std::move(native.device);
        result.swapchain = std::move(native.swapchain);
        result.reason = std::move(native.creationReason);
        result.adapter = std::move(native.adapterName);
        result.nativeError = native.creationError;
        break;
    }
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    case Backend::D3D12: {
        auto native = CreateD3D12Device(
            desc,
            options.driver == BackendDriver::Software ? D3D12Driver::Warp : D3D12Driver::Hardware,
            {options.model, options.debugLayer});
        result.device = std::move(native.device);
        result.swapchain = std::move(native.swapchain);
        result.reason = std::move(native.creationReason);
        result.adapter = std::move(native.adapterName);
        result.nativeError = native.creationError;
        break;
    }
#endif
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    case Backend::Vulkan:
        result.reason = "presentation_not_supported";
        break;
#endif
    default:
        result.reason = "backend_not_built";
        break;
    }
    return result;
}
OffscreenBackendDevice CreateOffscreenBackendDevice(std::uint32_t width, std::uint32_t height,
                                                    const BackendOptions& options) {
    OffscreenBackendDevice result;
    if (options.driver != BackendDriver::Hardware && options.driver != BackendDriver::Software) {
        result.reason = "invalid_driver";
        return result;
    }
    switch (options.backend) {
#ifdef MIKUDANCESTUDIO_RHI_D3D11
    case Backend::D3D11: {
        auto native = CreateD3D11OffscreenDevice(
            width, height,
            options.driver == BackendDriver::Software ? D3D11Driver::Warp : D3D11Driver::Hardware,
            options.model);
        result.device = std::move(native.device);
        result.reason = std::move(native.creationReason);
        result.adapter = std::move(native.adapterName);
        result.nativeError = native.creationError;
        break;
    }
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    case Backend::D3D12: {
        auto native = CreateD3D12OffscreenDevice(
            width, height,
            options.driver == BackendDriver::Software ? D3D12Driver::Warp : D3D12Driver::Hardware,
            {options.model, options.debugLayer});
        result.device = std::move(native.device);
        result.reason = std::move(native.creationReason);
        result.adapter = std::move(native.adapterName);
        result.nativeError = native.creationError;
        break;
    }
#endif
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    case Backend::Vulkan: {
        auto native = CreateVulkanOffscreenDevice(width, height,
                                                  options.driver == BackendDriver::Software
                                                      ? VulkanDriver::Software
                                                      : VulkanDriver::Hardware,
                                                  {options.model, options.debugLayer});
        result.device = std::move(native.device);
        result.reason = std::move(native.creationReason);
        result.adapter = std::move(native.adapterName);
        result.nativeError = native.creationError;
        break;
    }
#endif
    default:
        result.reason = "backend_not_built";
        break;
    }
    return result;
}
bool ReadBackendPresentation(Backend backend, Device& device, RgbaImage& image) {
    switch (backend) {
#ifdef MIKUDANCESTUDIO_RHI_D3D11
    case Backend::D3D11:
        return ReadD3D11Swapchain(device, image);
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    case Backend::D3D12:
        return ReadD3D12Swapchain(device, image);
#endif
    default:
        return false;
    }
}
}
