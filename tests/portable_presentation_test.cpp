#include "native_surface_host.hpp"
#include "rhi_test_platform.hpp"
#include "material_sampling_fixture.hpp"
#include "mikudancestudio/rhi_scene_preview.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <cmath>
using namespace mikudancestudio::rhi;
using namespace mikudancestudio;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + ": " + controller.Reason());                \
    } while (0)
// Independent of diagnostic/lifecycle frames: no image readbacks or explicit
// per-frame drains. Timestamp results are collected only after completion.
int Benchmark(const std::string& argument, const char* output) {
    using Clock = std::chrono::steady_clock;
    ScenePreviewController controller;
    auto host =
        CreateTestSurfaceHost(argument.find("wayland") != std::string::npos ? "wayland" : "native");
    CHECK(host->Resize(640, 480));
    BackendOptions options;
    options.backend = Backend::Vulkan;
    ConfigureVulkanTest(options, argument);
    options.profiling = true;
    std::string reason;
    options.surface = CreateNativeVulkanSurface(
        host->window, host->display, host, [] { return SurfaceHealth::Ready; }, reason);
    SwapchainDesc description;
    description.window = host->window;
    description.width = host->width;
    description.height = host->height;
    description.present = PresentMode::VSync;
    CHECK(controller.Open(description, options));
    const bool software = options.driver == BackendDriver::Software;
    const bool virtualGpu = controller.Adapter().find("Paravirtual") != std::string::npos;
    const bool ci = software || virtualGpu;
    // Fixed before measurement. These bound stalls/regressions in this public
    // fixture; they are not an editor frame-rate or physical GPU qualification.
    const double gpuBudget = ci ? 1000 : 20, cpuBudget = ci ? 1500 : 100;
    const std::uint64_t memoryBudget = (ci ? 512ull : 256ull) * 1024 * 1024;
    std::ofstream report(output);
    report.imbue(std::locale::classic());
    CHECK(report);
    report << "{\"schema\":1,\"adapter\":" << std::quoted(controller.Adapter()) << ",\"profile\":"
           << std::quoted(software     ? "ci-software"
                          : virtualGpu ? "ci-virtual"
                                       : "physical")
           << ",\"width\":640,\"height\":480,\"vsync\":true,\"cases\":[";
    for (unsigned animated = 0; animated < 2; ++animated) {
        CHECK(controller.Unload());
        controller.Resume();
        auto frame = sampling_fixture::Fixture(2, SamplerFilter::Linear, 1, SamplerAddress::Wrap,
                                               3.37f, true, true);
        for (auto& target : frame.targets)
            if (target.id == 1 || target.id == 2) {
                target.description.width = host->width;
                target.description.height = host->height;
            }
        frame.finalColor = 1;
        CHECK(AppendPostProcess(frame, 1, 5, host->width, host->height));
        const auto first = Clock::now();
        CHECK(controller.Draw(frame));
        const auto coldMs = std::chrono::duration<double, std::milli>(Clock::now() - first).count();
        auto& device = *controller.GetDevice();
        const auto drain = [&] {
            CHECK(device.WaitCompletion(device.SignalCompletion(), 5000) &&
                  controller.GetRenderer()->Collect());
        };
        drain();
        const auto before = QueryVulkanStats(device);
        const auto resources = controller.GetRenderer()->Stats();
        std::vector<double> cpu;
        for (unsigned i = 0; i < 90; ++i) {
            if (animated) {
                auto& vertices = frame.resources.front();
                vertices.key.version = i + 2;
                float x = -.95f + .1f * std::sin(i * .1f);
                std::memcpy(vertices.bytes.data(), &x, sizeof(x));
                for (auto& pass : frame.passes)
                    for (auto& draw : pass.draws)
                        if (draw.vertices.id == vertices.key.id)
                            draw.vertices = vertices.key;
            }
            const auto start = Clock::now();
            CHECK(controller.Draw(frame));
            cpu.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            CHECK(host->Pump());
        }
        drain();
        const auto after = QueryVulkanStats(device);
        const auto stats = controller.GetRenderer()->Stats();
        const double gpuMs = (after.gpuNanoseconds - before.gpuNanoseconds) / 90e6;
        const double waitMs = (after.waitNanoseconds - before.waitNanoseconds) / 90e6;
        double sum = 0;
        for (auto value : cpu)
            sum += value;
        std::sort(cpu.begin(), cpu.end());
        const auto p95 = cpu[85];
        std::printf(
            "profile %s: timestamp support=%u, samples=%llu, GPU ns=%llu, Draw p95=%g ms, wait mean=%g ms\n",
            controller.Adapter().c_str(), unsigned(after.gpuTimingSupported),
            static_cast<unsigned long long>(after.gpuFrames - before.gpuFrames),
            static_cast<unsigned long long>(after.gpuNanoseconds - before.gpuNanoseconds), p95,
            waitMs);
        CHECK(after.gpuTimingSupported && after.gpuFrames - before.gpuFrames == 90);
        // The Apple Paravirtual CI driver advertises timestamps and returns
        // available queries, but every duration is zero. Preserve that evidence
        // as unavailable, never as a measured 0 ms or a passed GPU budget.
        const bool observableClock = gpuMs > 0;
        CHECK(observableClock || virtualGpu);
        CHECK(after.presentations - before.presentations == 90 &&
              after.readbacks == before.readbacks);
        CHECK(after.validationErrors == 0 && after.retiringResources == 0);
        CHECK((!observableClock || gpuMs <= gpuBudget) && p95 <= cpuBudget && waitMs <= cpuBudget &&
              after.peakAllocatedBytes <= memoryBudget);
        if (!animated)
            CHECK(stats.uploads == resources.uploads);
        if (animated)
            CHECK(stats.bufferReuses > resources.bufferReuses);
        if (animated)
            report << ',';
        report << "{\"animated\":" << (animated ? "true" : "false")
               << ",\"frames\":90,\"gpu_timestamp_samples\":90,\"gpu_budget_evaluated\":"
               << (observableClock ? "true" : "false") << ",\"gpu_clock_status\":"
               << std::quoted(observableClock ? "measured" : "virtual_driver_zero_intervals")
               << ",\"gpu_mean_ms\":";
        if (observableClock)
            report << gpuMs;
        else
            report << "null";
        report << ",\"cpu_draw_mean_ms\":" << sum / 90 << ",\"cpu_draw_p95_ms\":" << p95
               << ",\"cold_draw_ms\":" << coldMs << ",\"acquire_cpu_ms\":"
               << (after.acquireNanoseconds - before.acquireNanoseconds) / 1e6
               << ",\"present_cpu_ms\":"
               << (after.presentNanoseconds - before.presentNanoseconds) / 1e6
               << ",\"timeline_wait_ms\":" << (after.waitNanoseconds - before.waitNanoseconds) / 1e6
               << ",\"readbacks\":0,\"peak_backend_allocation_bytes\":" << after.peakAllocatedBytes
               << ",\"buffer_creations\":" << stats.bufferCreations - resources.bufferCreations
               << ",\"buffer_reuses\":" << stats.bufferReuses - resources.bufferReuses
               << ",\"upload_bytes\":" << stats.uploadBytes - resources.uploadBytes
               << ",\"budget\":{\"gpu_mean_ms\":" << gpuBudget
               << ",\"cpu_draw_p95_ms\":" << cpuBudget << ",\"timeline_wait_mean_ms\":" << cpuBudget
               << ",\"backend_allocation_bytes\":" << memoryBudget << "}}";
    }
    report << "],\"status\":\"passed\"}\n";
    CHECK(report && controller.Close());
    std::printf("P3 native performance budgets passed: %s\n", output);
    return 0;
}
int main(int argc, char** argv) try {
    if (argc > 1 && std::string(argv[1]).find("benchmark") == 0)
        return Benchmark(argv[1], argc > 2 ? argv[2] : "p3-performance.json");
    const bool cpuScene = argc == 3;
    const char* hostArgument = std::getenv("MDS_P3_PRESENTATION_ARGUMENT");
    const std::string argument = hostArgument            ? hostArgument
                                 : !cpuScene && argc > 1 ? argv[1]
                                                         : "native";
    runtime::SceneRuntime scene;
    render::SceneFrameProducer producer;
    if (cpuScene) {
        format::ModelDocument document;
        const auto path = std::filesystem::path(argv[1]) / "triangle.pmx";
        std::ifstream file(path, std::ios::binary);
        auto read = [](void* state, void* data, std::size_t bytes) {
            auto& source = *static_cast<std::ifstream*>(state);
            source.read(static_cast<char*>(data), bytes);
            return int(source.gcount());
        };
        std::string reason;
        if (!file || !format::ReadModelDocument(read, &file, document) ||
            !scene.LoadModel(document, reason) || !scene.SetPhysicsMode(0, reason))
            throw std::runtime_error("presentation scene import: " + reason);
        render::SceneRenderSources sources;
        sources.models.emplace(0, path);
        FileAssetSource assets(argv[2]);
        format::VmdDocument motion;
        format::VmdBoneKey key;
        key.name[0] = 'B';
        key.frame = 20;
        key.position[0] = 1;
        key.rotation[3] = 1;
        motion.bones.push_back(key);
        std::shared_ptr<const runtime::MotionClip> clip;
        if (!runtime::MotionClip::Create(scene.Resource(0), motion, clip, reason) ||
            !scene.BindMotion(0, clip, reason) || !scene.Seek(0, reason) ||
            !producer.Load(scene, sources, assets, reason) || !scene.StartPlayback(reason))
            throw std::runtime_error("presentation scene prepare: " + reason);
    }
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
        const auto attach = [&](const std::shared_ptr<NativeSurfaceHost>& current) {
            std::weak_ptr<NativeSurfaceHost> weak = current;
            return CreateNativeVulkanSurface(
                current->window, current->display, current,
                [weak] {
                    auto h = weak.lock();
                    return !h || h->removed ? SurfaceHealth::Removed
                           : h->suspended   ? SurfaceHealth::Suspended
                                            : SurfaceHealth::Ready;
                },
                reason);
        };
        options.surface = attach(host);
        CHECK(options.surface);
        SwapchainDesc description;
        description.window = host->window;
        description.width = host->width;
        description.height = host->height;
        description.present = PresentMode::VSync;
        CHECK(controller.Open(description, options));
        std::printf("presentation adapter: %s\n", controller.Adapter().c_str());
        auto oracleOptions = options;
        oracleOptions.surface.reset();
        auto oracle = CreateOffscreenBackendDevice(96, 64, oracleOptions);
        CHECK(oracle);
        SceneRenderer offscreen(*oracle.device);
        for (unsigned i = 0; i < 12; ++i) {
            const auto width = i % 3 == 1 ? 127u : 96u, height = i % 3 == 1 ? 79u : 64u;
            CHECK(host->Resize(width, height) && host->Pump());
            CHECK(host->width == width && host->height == height);
            CHECK(controller.Resize(host->width, host->height) &&
                  controller.State() == PreviewState::Ready);
            RenderFrame frame;
            if (cpuScene) {
                if (i)
                    CHECK(scene.Advance(float(i) / 30, 1.f / 30, reason));
                const auto beforeUpdate = scene.Counters().frames;
                render::SceneRenderSettings settings;
                settings.width = host->width;
                settings.height = host->height;
                settings.shadow.mode = 1;
                settings.shadow.mapSize = 32;
                settings.postProcessCopy = false;
                CHECK(producer.Build(scene, settings, frame, reason) &&
                      scene.Counters().frames == beforeUpdate && scene.Snapshot(0)->frame == i);
            } else
                frame = sampling_fixture::Fixture(2, SamplerFilter::Linear, 1, SamplerAddress::Wrap,
                                                  3.37f, true, true);
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
                  after.presentations == before.presentations + 1 && after.validationErrors == 0 &&
                  !after.gpuTimingSupported && after.gpuFrames == 0);
            auto expectedFrame = frame;
            CHECK(ApplyMaterialSampling(expectedFrame, options.materialSampling));
            PreparedModelFrame prepared;
            CompletionToken token;
            AttachmentReadback expected;
            CHECK(offscreen.Prepare(1, expectedFrame, prepared) &&
                  offscreen.Submit(prepared, token) && offscreen.ReadTarget(1, 5, expected));
            // Separate diagnostic frame: read the acquired swapchain image,
            // including the final GPU blit and BGRA/RGBA conversion.
            RgbaImage actual;
            CHECK(controller.Draw(frame, &actual) && actual.width == host->width &&
                  actual.height == host->height && actual.pixels == expected.bytes);
            CHECK(QueryVulkanStats(*controller.GetDevice()).readbacks == after.readbacks + 1);
            if (i == 3) {
                host->suspended = true;
                CHECK(controller.Resize(0, 0) && controller.State() == PreviewState::Suspended);
                CHECK(controller.Draw(frame) &&
                      QueryVulkanStats(*controller.GetDevice()).presentations ==
                          after.presentations + 1);
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
            if (i == 4 || i == 8 || i == 10) {
                const auto oldIdentity = controller.GetDevice()->Status().identity;
                const auto cpuUpdates = scene.Counters().frames;
                // Allocate while the old handle is still live. Native removal
                // keeps the display/layer lease until Vulkan teardown finishes.
                auto replacement = CreateTestSurfaceHost(
                    argument.find("wayland") != std::string::npos ? "wayland" : "native");
                CHECK(replacement->Resize(width, height));
                if (i == 10)
                    host->Remove();
                else
                    CHECK(LoseVulkanSurfaceForTest(*controller.GetDevice(),
                                                   i == 4 ? VulkanSurfaceLossPoint::Acquire
                                                          : VulkanSurfaceLossPoint::Present));
                CHECK(!controller.Draw(frame) && controller.State() == PreviewState::Failed);
                const auto failed = QueryVulkanStats(*controller.GetDevice());
                CHECK(failed.validationErrors == 0);
                if (i == 10)
                    CHECK(controller.Reason() == "surface_removed_requires_replacement");
                else
                    CHECK(failed.lastError == -1000000000); // VK_ERROR_SURFACE_LOST_KHR
                CHECK(!controller.Draw(frame) &&
                      QueryVulkanStats(*controller.GetDevice()).submissions == failed.submissions);
                host->Remove();
                auto selected = options;
                selected.surface = attach(replacement);
                auto next = description;
                next.window = replacement->window;
                next.width = replacement->width;
                next.height = replacement->height;
                CHECK(controller.SwitchBackend(next, selected) &&
                      controller.GetDevice()->Status().identity != oldIdentity);
                options = selected;
                description = next;
                host = replacement;
                lifetime = host;
                CHECK(controller.Draw(frame, &actual) && actual.pixels == expected.bytes &&
                      scene.Counters().frames == cpuUpdates &&
                      QueryVulkanStats(*controller.GetDevice()).validationErrors == 0);
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
        "native swapchain exact readback/odd resize/suspend/surface-loss/new-window/replacement/unload/no-readback/lifetime passed");
    if (cpuScene)
        std::puts("P2 CPU animation -> shared scene frame -> native presentation passed");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
