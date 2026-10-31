#include "native_surface_host.hpp"
#include "rhi_test_platform.hpp"
#include "material_sampling_fixture.hpp"
#include "mikudancestudio/rhi_scene_preview.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include <cstdio>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + ": " + controller.Reason());                \
    } while (0)
int main(int argc, char** argv) try {
    const std::string argument = argc > 1 ? argv[1] : "native";
    std::weak_ptr<NativeSurfaceHost> lifetime;
    {
        auto host = CreateTestSurfaceHost(argument.find("wayland") != std::string::npos ? "wayland"
                                                                                        : "native");
        lifetime = host;
        ScenePreviewController controller;
        BackendOptions options;
        options.backend = Backend::Vulkan;
        ConfigureVulkanTest(options, argument);
        std::string reason;
        options.surface = CreateNativeVulkanSurface(
            host->window, host->display, host,
            [lifetime] {
                auto h = lifetime.lock();
                return !h             ? SurfaceHealth::Removed
                       : h->suspended ? SurfaceHealth::Suspended
                                      : SurfaceHealth::Ready;
            },
            reason);
        CHECK(options.surface);
        SwapchainDesc description;
        description.window = host->window;
        description.width = host->width;
        description.height = host->height;
        description.present = PresentMode::VSync;
        CHECK(controller.Open(description, options));
        std::printf("presentation adapter: %s\n", controller.Adapter().c_str());
        auto oracle = CreateOffscreenBackendDevice(96, 64, options);
        CHECK(oracle);
        SceneRenderer offscreen(*oracle.device);
        for (unsigned i = 0; i < 12; ++i) {
            const auto width = i % 3 == 1 ? 127u : 96u, height = i % 3 == 1 ? 79u : 64u;
            CHECK(host->Resize(width, height) && host->Pump());
            CHECK(host->width == width && host->height == height);
            CHECK(controller.Resize(host->width, host->height) &&
                  controller.State() == PreviewState::Ready);
            auto frame = sampling_fixture::Fixture(2, SamplerFilter::Linear, 1,
                                                   SamplerAddress::Wrap, 3.37f, true, true);
            for (auto& t : frame.targets)
                if (t.id == 1 || t.id == 2) {
                    t.description.width = host->width;
                    t.description.height = host->height;
                }
            frame.finalColor = 1;
            CHECK(AppendPostProcess(frame, 1, 5, host->width, host->height));
            const auto before = QueryVulkanStats(*controller.GetDevice());
            CHECK(controller.Draw(frame) && host->Pump());
            const auto after = QueryVulkanStats(*controller.GetDevice());
            CHECK(after.readbacks == before.readbacks &&
                  after.presentations == before.presentations + 1 && after.validationErrors == 0);
            auto expectedFrame = frame;
            CHECK(ApplyMaterialSampling(expectedFrame, options.materialSampling));
            PreparedModelFrame prepared;
            CompletionToken token;
            AttachmentReadback expected, actual;
            CHECK(offscreen.Prepare(1, expectedFrame, prepared) &&
                  offscreen.Submit(prepared, token) && offscreen.ReadTarget(1, 5, expected));
            CHECK(controller.GetRenderer()->ReadTarget(1, 5, actual) &&
                  actual.bytes == expected.bytes);
            if (i == 3) {
                host->suspended = true;
                CHECK(controller.Resize(0, 0) && controller.State() == PreviewState::Suspended);
                CHECK(controller.Draw(frame) &&
                      QueryVulkanStats(*controller.GetDevice()).presentations ==
                          after.presentations);
                host->suspended = false;
                CHECK(controller.Resize(host->width, host->height) && controller.Draw(frame));
            }
            if (i == 6) {
                const auto identity = controller.GetDevice()->Status().identity;
                CHECK(controller.ReplaceDevice() &&
                      controller.GetDevice()->Status().identity != identity &&
                      controller.Draw(frame));
            }
            if (i == 9) {
                CHECK(controller.Unload());
                controller.Resume();
                CHECK(controller.Draw(frame));
            }
        }
        CHECK(offscreen.Detach() && controller.Close());
        // Surface lease, not the caller's local variable, owns native lifetime.
        host.reset();
        CHECK(!lifetime.expired());
    }
    if (!lifetime.expired())
        throw std::runtime_error("native_surface_lease_leaked");
    std::puts(
        "native presentation/odd resize/suspend/replacement/unload/no-readback/lifetime passed");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
