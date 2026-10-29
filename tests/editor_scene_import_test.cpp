#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/model_cpu_bridge.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/editor_scene.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_frame_passes.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
using namespace mikudancestudio;
using namespace mikudancestudio::rhi;
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc < 3)
        return 2;
    const std::string backend = argv[1];
    const auto api = backend == "dx11"   ? Backend::D3D11
                     : backend == "dx12" ? Backend::D3D12
                                         : Backend::Vulkan;
    const auto path = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    auto app = std::make_unique<MMDApp>();
    std::memset(&app->state, 0, sizeof(app->state));
    g_Block = app.get();
    struct Globals {
        ~Globals() { g_Block = nullptr; }
    } globals;
    struct Window {
        HWND value = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
                                     L"Hidden live editor import", WS_OVERLAPPEDWINDOW, 0, 0, 256,
                                     192, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~Window() {
            if (value)
                DestroyWindow(value);
        }
    } window;
    const auto check = [&](bool result, const char* why) {
        if (!result)
            throw std::runtime_error(std::string(why) + ": " + app->ViewportRenderer().Reason());
    };
    check(window.value && !IsWindowVisible(window.value), "hidden parent");
    app->MainWindow() = window.value;
    app->HInstance() = GetModuleHandleW(nullptr);
    app->ViewportRect() = {0, 0, 160, 120};
    app->CameraMode() = 1;
    app->EditMode() = ViewportEditMode::None;
    app->GroundGridEnabled() = app->GroundShadowEnabled() = 0;
    app->CaptureMode() = ScreenCaptureMode::Disabled;
    app->CameraDistance() = -45;
    app->CameraFov() = 45;
    auto& frame = app->CpuRenderFrame();
    const render::ModelMatrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    frame.world = frame.lightRotation = identity;
    frame.view.aspect = 4.f / 3;
    frame.view.viewport = {0, 0, 160, 120};
    frame.scene.camera.distance = -45;
    frame.scene.camera.fov = 45;
    frame.scene.camera.parentModel = -1;
    frame.scene.lightState = {.6f, .7f, .8f, -.5f, -1, -.5f};
    app->CpuRenderFrameReady() = true;
    PhysicsScene physics{};
    check(SceneConstruct(&physics, nullptr), "CPU physics world");
    struct Physics {
        PhysicsScene* value;
        ~Physics() { DisposePhysicsWorld(value); }
    } owner{&physics};
    auto model = std::make_unique<mdl::ModelRecord>();
    ModelInitDefaults(model.get());
    struct Record {
        MMDApp* app;
        mdl::ModelRecord* value;
        ~Record() {
            app->ViewportRenderer().UnloadScene();
            app->ModelSlot(0) = nullptr;
            ModelDispose(value);
        }
    } record{app.get(), model.get()};
    PathResolutionWorkspace paths{};
    check(ModelLoadPMD(model.get(), nullptr, path.c_str(), nullptr, 0, 0, 1, &physics, paths,
                       ModelResourceMode::CpuOnly),
          "CPU import");
    check(!model->vertexBuffer && !model->vertexBuffer2 && !model->indexBuffer,
          "import has no GPU buffers");
    app->ModelSlot(0) = model.get();
    model->comboSelIndex = 0;
    model->toonFlag = 1;
    check(app->ViewportRenderer().Select(*app, api, false), "select backend");
    // Use the host record's final matrices, just as the editor does. This
    // fixture supplies bind-pose matrices without starting another solver.
    for (unsigned n = 0; n < model->boneCount; ++n)
        std::copy(identity.begin(), identity.end(), model->boneTable[n].matInit);
    check(UpdateCpuOnlyModelGeometry(app.get(), model.get(), .01f, false), "publish geometry");
    const auto source = app->CpuModelFrames().at(model.get())->geometry.Snapshot().main.key.id;
    if (argc > 3 && std::string(argv[3]) == "--benchmark") {
        using Clock = std::chrono::steady_clock;
        const unsigned count = argc > 4 ? std::max(3, std::atoi(argv[4])) : 12;
        const unsigned width = 1280, height = 720;
        RECT client{0, 0, LONG(width), LONG(height)};
        AdjustWindowRect(&client, WS_OVERLAPPEDWINDOW, FALSE);
        check(SetWindowPos(window.value, nullptr, 0, 0, client.right - client.left,
                           client.bottom - client.top, SWP_NOACTIVATE | SWP_NOZORDER),
              "resize benchmark");
        app->ViewportRect() = {0, 0, LONG(width), LONG(height)};
        frame.view.aspect = float(width) / height;
        BackendOptions options;
        options.backend = api;
        SwapchainDesc desc;
        desc.window = {WindowSystem::Win32, reinterpret_cast<std::uintptr_t>(window.value)};
        desc.width = width;
        desc.height = height;
        desc.present = PresentMode::Immediate;
        auto device = CreateBackendDevice(desc, options);
        if (!device)
            throw std::runtime_error(device.reason);
        std::printf("BENCH %s adapter=%s vertices=%u materials=%u frames=%u %ux%u\n",
                    backend.c_str(), device.adapter.c_str(), model->vertexCount,
                    model->materialCount, count, width, height);
        std::fflush(stdout);
        SceneRenderer renderer(*device.device);
        EditorSceneProducer producer;
        for (const int mode : {0, 1, 2}) {
            app->state.selfShadowEnabled = mode != 0;
            app->SelfShadowMode() = mode;
            app->ShadowDistance() = .01125f;
            double build = 0, prepare = 0, submit = 0, present = 0;
            SceneRendererStats before;
            for (unsigned n = 0; n < count + 2; ++n) {
                auto start = Clock::now();
                RenderFrame commands;
                std::string reason;
                if (!producer.Build(*app, width, height, commands, reason))
                    throw std::runtime_error(reason);
                auto built = Clock::now();
                check(ApplyMaterialSampling(commands, options.materialSampling), "sampling");
                PreparedModelFrame prepared;
                const auto result =
                    renderer.Prepare(1, commands, prepared, FrameOutput::Presentation);
                if (!result)
                    throw std::runtime_error(result.reason);
                auto ready = Clock::now();
                CompletionToken completion;
                const auto issued = renderer.Submit(prepared, completion);
                if (!issued)
                    throw std::runtime_error(issued.reason);
                auto submitted = Clock::now();
                check(device.device->Present() && renderer.Collect(), "present benchmark");
                producer.Commit();
                auto presented = Clock::now();
                if (n >= 2) {
                    const auto ms = [](auto a, auto b) {
                        return std::chrono::duration<double, std::milli>(b - a).count();
                    };
                    build += ms(start, built);
                    prepare += ms(built, ready);
                    submit += ms(ready, submitted);
                    present += ms(submitted, presented);
                } else
                    before = renderer.Stats();
            }
            const auto stats = renderer.Stats();
            check(stats.uploads == before.uploads &&
                      stats.textureUploads == before.textureUploads,
                  "unchanged model must reuse resident resources");
            std::printf(
                "mode=%d build=%.3f prepare=%.3f submit=%.3f present=%.3f total=%.3f ms steady_uploads=%llu texture_uploads=%llu\n",
                mode, build / count, prepare / count, submit / count, present / count,
                (build + prepare + submit + present) / count, stats.uploads - before.uploads,
                stats.textureUploads - before.textureUploads);
            std::fflush(stdout);
        }
        check(renderer.Detach() && device.swapchain->Retire() && !IsWindowVisible(window.value),
              "benchmark close");
        // Also measure the actual viewport host/controller path, including its
        // frame snapshot, child surface and scene producer. The parent stays
        // hidden and no acceptance readback is requested.
        for (const int mode : {0, 1, 2}) {
            app->state.selfShadowEnabled = mode != 0;
            app->SelfShadowMode() = mode;
            double elapsed = 0;
            for (unsigned n = 0; n < count + 2; ++n) {
                const auto start = Clock::now();
                check(app->ViewportRenderer().Render(*app), "benchmark editor viewport");
                if (n >= 2)
                    elapsed += std::chrono::duration<double, std::milli>(Clock::now() - start)
                                   .count();
                check(!IsWindowVisible(window.value) &&
                          !IsWindowVisible(reinterpret_cast<HWND>(app->ViewportRenderer().Window())),
                      "benchmark viewport remains hidden");
            }
            std::printf("EDITOR_VIEWPORT %s mode=%d average=%.3f ms\n", backend.c_str(), mode,
                        elapsed / count);
            std::fflush(stdout);
        }
        app->ViewportRenderer().Close();
        return 0;
    }
    RgbaImage first, repeated;
    for (int mode : {0, 1, 2}) {
        app->state.selfShadowEnabled = mode != 0;
        app->SelfShadowMode() = mode;
        app->ShadowDistance() = .01125f;
        check(app->ViewportRenderer().Render(*app, &first), "render imported live scene");
        check(app->ViewportRenderer().Render(*app, &repeated) && first.pixels == repeated.pixels,
              "repeat exact output");
        const bool visible = [&] {
            for (std::size_t p = 0; p < first.pixels.size(); p += 4)
                if (first.pixels[p] != 255 || first.pixels[p + 1] != 255 ||
                    first.pixels[p + 2] != 255)
                    return true;
            return false;
        }();
        check(visible, "model coverage");
    }
    app->state.selfShadowEnabled = 0;
    mdl::AccessoryRecord accessory{};
    if (argc > 3) {
        const auto file = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
        wcscpy_s(accessory.sourcePath, file.c_str());
        accessory.parentModel = -1;
        accessory.visible = 1;
        accessory.opacity = accessory.scale = 1;
        accessory.position[0] = 10;
        app->AccessorySlot(0) = &accessory;
        app->AccessoryRenderSplitOrder() = 1;
        check(app->ViewportRenderer().Render(*app, &repeated), "live accessory before model");
        app->AccessoryRenderSplitOrder() = 0;
        check(app->ViewportRenderer().Render(*app, &repeated), "live accessory after model");
    }
    for (auto selected : {Backend::D3D11, Backend::D3D12, Backend::Vulkan}) {
        if (!BackendAvailable(selected))
            continue;
        check(app->ViewportRenderer().Select(*app, selected, false), "switch live imported scene");
        check(app->ViewportRenderer().Render(*app, &repeated), "render after switch");
        check(app->ModelSlot(0) == model.get() &&
                  app->CpuModelFrames().at(model.get())->geometry.Snapshot().main.key.id == source,
              "scene identity retained");
        check(!IsWindowVisible(window.value), "parent remains hidden");
    }
    // Exercise live camera state changes independently of PMM load/evaluation.
    frame.scene.camera.distance = -65;
    check(app->ViewportRenderer().Render(*app, &repeated), "live camera edit");
    frame.scene.camera.distance = -45;
    if (argc > 4) {
        const auto file = std::filesystem::absolute(std::filesystem::u8path(argv[4]));
        wcscpy_s(accessory.sourcePath, file.c_str());
        app->CaptureMode() = ScreenCaptureMode::FullFrame;
        for (unsigned n = 0; n < 4; ++n)
            check(app->ViewportRenderer().Render(*app, &repeated), "live screen.bmp history");
        check(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand),
              "recreate history device");
        check(app->ViewportRenderer().Render(*app, &repeated),
              "screen.bmp after device replacement");
    }
    app->AccessorySlot(0) = nullptr;
    app->ViewportRenderer().Close();
    std::printf(
        "PASS %s %s: imported editor PMD/PMX CPU records, live geometry/materials, two self-shadow modes, exact repeat and cross-API rebuild; no parent shown\n",
        backend.c_str(), path.extension().u8string().c_str());
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
