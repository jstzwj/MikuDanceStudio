#include "screen_texture_fixture.hpp"
using namespace mikudancestudio::rhi;
int main(int argc, char** argv) try {
    const std::string api = argc > 1 ? argv[1] : "vulkan";
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = api.find("debug") != std::string::npos;
    if (api.find("measured") != std::string::npos)
        options.model.compatibility = ModelCompatibility::Dx9Measured;
    auto native = CreateOffscreenBackendDevice(96, 64, options);
    if (!native && options.debugLayer &&
        (native.reason == "validation_layer_unavailable" ||
         native.reason == "validation_extensions_unavailable"))
        return 77;
    SCREEN_CHECK(native);
    screen_acceptance::Generated(native, options);
    screen_acceptance::CaptureExtents(*native.device);
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan)
        SCREEN_CHECK(QueryVulkanStats(*native.device).validationErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (options.backend == Backend::D3D12)
        SCREEN_CHECK(QueryD3D12Stats(*native.device).debugErrors == 0);
#endif
    std::cout << api << " screen capture modes, history, repeat, unload and replacement passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
}
