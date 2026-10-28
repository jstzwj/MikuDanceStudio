#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "mikudancestudio/rhi_scene_preview.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "material_sampling_fixture.hpp"
#include <cstdio>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + " " + controller.Reason());                 \
    } while (0)
struct HiddenWindow {
    HWND hwnd = nullptr;
    HiddenWindow() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MDSHiddenPresentationAcceptance";
        RegisterClassW(&wc);
        hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName,
                               L"MDS hidden acceptance", WS_POPUP, 0, 0, 96, 64, nullptr, nullptr,
                               wc.hInstance, nullptr);
    }
    ~HiddenWindow() {
        if (hwnd)
            DestroyWindow(hwnd);
    }
    bool Resize(unsigned w, unsigned h) {
        return SetWindowPos(hwnd, nullptr, 0, 0, w, h,
                            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE &&
               !IsWindowVisible(hwnd);
    }
};
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const std::string arg = argc > 1 ? argv[1] : "vulkan";
    BackendOptions options;
    options.backend = arg.find("dx11") == 0   ? Backend::D3D11
                      : arg.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = arg.find("debug") != std::string::npos;
    options.materialSampling = MaterialSamplingPolicy::PortableTrilinear;
    HiddenWindow window;
    ScenePreviewController controller;
    CHECK(window.hwnd && !IsWindowVisible(window.hwnd));
    SwapchainDesc desc;
    desc.window = {WindowSystem::Win32, reinterpret_cast<std::uintptr_t>(window.hwnd)};
    desc.width = 96;
    desc.height = 64;
    if (!controller.Open(desc, options)) {
        if (options.debugLayer && options.backend == Backend::Vulkan &&
            (controller.Reason() == "validation_layer_unavailable" ||
             controller.Reason() == "validation_extensions_unavailable")) {
            std::printf("SKIP: %s\n", controller.Reason().c_str());
            return 77;
        }
        CHECK(false);
    }
    auto oracle = CreateOffscreenBackendDevice(96, 64, options);
    CHECK(oracle);
    SceneRenderer offscreen(*oracle.device);
    std::uint64_t stableUploads = 0;
    for (unsigned iteration = 0; iteration < 18; ++iteration) {
        const unsigned width = iteration % 3 == 1 ? 128 : 96, height = iteration % 3 == 1 ? 80 : 64;
        CHECK(window.Resize(width, height));
        CHECK(controller.Resize(width, height));
        auto frame = sampling_fixture::Fixture(2, SamplerFilter::Anisotropic, 16,
                                               SamplerAddress::Wrap, 7.37f, true, true, true);
        for (auto& target : frame.targets)
            if (target.id == 1 || target.id == 2) {
                target.description.width = width;
                target.description.height = height;
            }
        frame.finalColor = 1;
        auto expectedFrame = frame;
        CHECK(ApplyMaterialSampling(expectedFrame, options.materialSampling));
        PreparedModelFrame prepared;
        CompletionToken token;
        AttachmentReadback expected;
        CHECK(offscreen.Prepare(1, expectedFrame, prepared) && offscreen.Submit(prepared, token) &&
              offscreen.ReadTarget(1, 1, expected));
        RgbaImage presented;
        CHECK(controller.Draw(frame, &presented));
        CHECK(presented.width == width && presented.height == height &&
              presented.pixels == expected.bytes);
        CHECK(!IsWindowVisible(window.hwnd));
        if (!stableUploads)
            stableUploads = controller.GetRenderer()->Stats().uploads;
        CHECK(controller.GetRenderer()->Stats().uploads == stableUploads);
        if (iteration == 7) {
            const auto old = controller.GetDevice()->Status().identity;
            CHECK(controller.ReplaceDevice() && controller.GetDevice()->Status().identity != old);
            stableUploads += frame.resources.size();
        }
    }
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan)
        CHECK(QueryVulkanStats(*controller.GetDevice()).validationErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (options.backend == Backend::D3D12)
        CHECK(QueryD3D12Stats(*controller.GetDevice()).debugErrors == 0);
#endif
    CHECK(offscreen.Unload(1) && offscreen.Detach());
    CHECK(controller.Unload() && controller.Close() && !IsWindowVisible(window.hwnd));
    // Exercise an explicitly requested immediate present mode independently.
    desc.present = PresentMode::Immediate;
    desc.width = 96;
    desc.height = 64;
    CHECK(window.Resize(96, 64));
    CHECK(controller.Open(desc, options));
    auto simple = sampling_fixture::Fixture(0, SamplerFilter::Linear, 1, SamplerAddress::Clamp,
                                            .37f, true, false);
    auto stream = FlattenRenderFrame(simple);
    stream.clear = simple.passes.back().color->clear;
    RgbaImage legacy, explicitImage;
    CHECK(controller.Draw(stream, &legacy));
    simple.finalColor = 1;
    CHECK(controller.Draw(simple, &explicitImage));
    CHECK(legacy.pixels == explicitImage.pixels);
    auto frame = sampling_fixture::Fixture(0, SamplerFilter::Linear, 1, SamplerAddress::Clamp, .37f,
                                           true, true);
    frame.finalColor = 1;
    CHECK(controller.Draw(frame) && controller.Close() && !IsWindowVisible(window.hwnd));
    if (options.backend == Backend::Vulkan) {
        auto native = CreateBackendDevice(desc, options);
        CHECK(native);
        CHECK(native.swapchain->Query(desc).supported);
        auto unsupported = desc;
        unsupported.samples = 2;
        CHECK(!native.swapchain->Query(unsupported).supported);
        CHECK(native.swapchain->Rebuild(unsupported, RebuildReason::Resize) ==
              RebuildResult::Unsupported);
        CHECK(native.swapchain->Rebuild(desc, RebuildReason::DeviceReplacement) ==
              RebuildResult::ReplacementRequired);
        SceneRenderer renderer(*native.device);
        PreparedModelFrame prepared;
        CompletionToken token;
        CHECK(renderer.Prepare(1, frame, prepared, FrameOutput::Presentation) &&
              renderer.Submit(prepared, token));
        CHECK(native.swapchain->Rebuild(desc, RebuildReason::TemporaryLoss) == RebuildResult::Busy);
        CHECK(native.device->Present());
        const auto epoch = native.device->Status().epoch, uploads = renderer.Stats().uploads;
        CHECK(window.Resize(128, 64));
        CHECK(native.swapchain->Rebuild(desc, RebuildReason::Resize) == RebuildResult::RetryLater);
        CHECK(native.device->Status().epoch == epoch);
        CHECK(window.Resize(96, 64));
        CHECK(native.swapchain->Rebuild(desc, RebuildReason::TemporaryLoss) ==
              RebuildResult::Success);
        CHECK(native.device->Status().epoch != epoch && !renderer.Submit(prepared, token));
        CHECK(renderer.Prepare(1, frame, prepared, FrameOutput::Presentation) &&
              renderer.Submit(prepared, token) && native.device->Present());
        CHECK(renderer.Stats().uploads == uploads);
        CHECK(QueryVulkanStats(*native.device).validationErrors == 0);
        CHECK(renderer.Unload(1) && renderer.Detach() && native.swapchain->Retire());
        CHECK(native.swapchain->Health() == SurfaceHealth::Removed &&
              !native.device->BeginFrame({}));
    }
    std::printf(
        "%s: hidden presentation, exact swapchain readback, resize, resident reuse, replacement and unload passed\n",
        arg.c_str());
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
