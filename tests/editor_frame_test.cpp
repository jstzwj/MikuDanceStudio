#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/model_cpu_bridge.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "pump_input.hpp"
#include "mme_host_api.h"
#include "mme_portable_api.h"
#include "mikudancestudio/mme_portable.hpp"
#include "effect_engine.h"
#include "emm_manager.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "model_data.h"
#include "material_bind.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

using namespace mikudancestudio;
using namespace mikudancestudio::rhi;
using Microsoft::WRL::ComPtr;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: " #x "\n", __LINE__);                                   \
            throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x);                \
        }                                                                                          \
    } while (false)

namespace {
unsigned blockedDialogs = 0;
LRESULT CALLBACK BlockTestDialog(int code, WPARAM window, LPARAM data) {
    wchar_t name[32]{};
    if (code == HCBT_CREATEWND && GetClassNameW(reinterpret_cast<HWND>(window), name, 32) &&
        !std::wcscmp(name, L"#32770")) {
        ++blockedDialogs;
        std::fputs("Unexpected dialog blocked before creation\n", stderr);
        return 1;
    }
    return CallNextHookEx(nullptr, code, window, data);
}
}

int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    CHECK(argc >= 3);
    const std::string mode = argv[1];
    const auto api = mode == "dx11"   ? Backend::D3D11
                     : mode == "dx12" ? Backend::D3D12
                                      : Backend::Vulkan;
    const auto path = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    const bool benchmark = argc > 3 && std::string(argv[3]) == "--benchmark";
    const unsigned count = argc > 4 ? std::max(3, std::atoi(argv[4])) : 12;
    std::filesystem::path rayEffect;
    if (!benchmark && argc > 4) {
        CHECK(argc == 6 && std::string(argv[4]) == "--ray-effect");
        rayEffect = std::filesystem::absolute(std::filesystem::u8path(argv[5]));
        CHECK(std::filesystem::is_regular_file(rayEffect));
    }
    struct DialogGuard {
        HHOOK hook = SetWindowsHookExW(WH_CBT, BlockTestDialog, nullptr, GetCurrentThreadId());
        ~DialogGuard() {
            if (hook)
                UnhookWindowsHookEx(hook);
        }
    } dialogGuard;
    CHECK(dialogGuard.hook);
    auto app = std::make_unique<MMDApp>();
    std::memset(&app->state, 0, sizeof(app->state));
    g_Block = app.get();
    struct Globals {
        ~Globals() { g_Block = nullptr; }
    } globals;
    struct Window {
        HWND value = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
                                     L"Hidden frame owner", WS_OVERLAPPEDWINDOW, 0, 0, 1400, 900,
                                     nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~Window() { DestroyWindow(value); }
    } window;
    CHECK(window.value && !IsWindowVisible(window.value));
    app->MainWindow() = window.value;
    app->HInstance() = GetModuleHandleW(nullptr);
    app->ViewportRect() = {0, 0, benchmark ? 1280 : 160, benchmark ? 720 : 120};
    app->CameraMode() = 1;
    app->EditMode() = ViewportEditMode::None;
    app->CameraParentModel() = -1;
    app->CameraDistance() = -45;
    app->CameraFov() = 45;
    app->CaptureMode() = ScreenCaptureMode::Disabled;
    app->LightDirection()[0] = -.5f;
    app->LightDirection()[1] = -1;
    app->LightDirection()[2] = -.5f;
    for (unsigned i = 0; i < 3; ++i)
        app->LightColor()[i] = .7f;
    app->SceneLight().Type = D3DLIGHT_DIRECTIONAL;
    app->SceneLight().Diffuse = app->SceneLight().Specular = {.7f, .7f, .7f, 1};
    app->AviRecordFps() = 30;
    app->DeltaTime() = 1.f / 60;
    ComPtr<IDirect3D9> legacy;
    legacy.Attach(Direct3DCreate9(D3D_SDK_VERSION));
    CHECK(legacy);
    ComPtr<IDirect3DDevice9> device;
    D3DPRESENT_PARAMETERS desc{};
    desc.BackBufferWidth = 1280;
    desc.BackBufferHeight = 720;
    desc.BackBufferCount = 1;
    desc.BackBufferFormat = D3DFMT_X8R8G8B8;
    desc.Windowed = TRUE;
    desc.hDeviceWindow = window.value;
    desc.SwapEffect = D3DSWAPEFFECT_DISCARD;
    desc.EnableAutoDepthStencil = TRUE;
    desc.AutoDepthStencilFormat = D3DFMT_D24S8;
    desc.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    const auto created = legacy->CreateDevice(0, D3DDEVTYPE_HAL, window.value,
                                              D3DCREATE_SOFTWARE_VERTEXPROCESSING, &desc, &device);
    if (FAILED(created)) {
        D3DDISPLAYMODE current{};
        legacy->GetAdapterDisplayMode(0, &current);
        std::fprintf(stderr, "Hidden DX9 initialization HRESULT=%08lx; no frame executed\n",
                     static_cast<unsigned long>(created));
        std::fprintf(stderr, "display=%ux%u format=%u check=%08lx\n", current.Width, current.Height,
                     unsigned(current.Format),
                     static_cast<unsigned long>(legacy->CheckDeviceType(
                         0, D3DDEVTYPE_HAL, current.Format, desc.BackBufferFormat, TRUE)));
        return 1;
    }
    auto wrapper = std::make_unique<D3DRenderer>();
    std::memset(wrapper.get(), 0, sizeof(*wrapper));
    wrapper->device = device.Get();
    wrapper->presentParameters = desc;
    wrapper->aspectRatio = 4.f / 3;
    wrapper->viewScale = 1;
    wrapper->d3dInitialized = 1;
    app->state.renderer = wrapper.get();
    InitRenderStates(app.get());
    // Native overlay resources remain preparation dependencies in this batch.
    ComPtr<IDirect3DVertexBuffer9> sprites, text, lines;
    CHECK(SUCCEEDED(device->CreateVertexBuffer(0x445c0, 0, 0, D3DPOOL_MANAGED, &sprites, nullptr)));
    CHECK(SUCCEEDED(device->CreateVertexBuffer(0x6d60, 0, 0, D3DPOOL_MANAGED, &text, nullptr)));
    CHECK(SUCCEEDED(device->CreateVertexBuffer(0x445c0, 0, 0, D3DPOOL_MANAGED, &lines, nullptr)));
    app->SpriteOverlayVertices() = sprites.Get();
    app->OverlayVertices() = text.Get();
    wrapper->lineVertexBuffer = lines.Get();
    CHECK(InitSceneFontTexture(app.get()));
    struct Font {
        MMDApp* app;
        ~Font() {
            if (app->SceneFontTexture())
                app->SceneFontTexture()->Release();
            ForgetSceneFontRgba(app);
        }
    } font{app.get()};
    PhysicsScene physics{};
    CHECK(SceneConstruct(&physics, nullptr));
    app->Physics() = &physics;
    struct Physics {
        PhysicsScene* p;
        ~Physics() { DisposePhysicsWorld(p); }
    } owner{&physics};
    auto model = std::make_unique<mdl::ModelRecord>();
    ModelInitDefaults(model.get());
    struct Record {
        MMDApp* app;
        mdl::ModelRecord* model;
        ~Record() {
            app->ViewportRenderer().Close();
            app->ModelSlot(0) = nullptr;
            if (auto*& capture = app->CaptureTexture()) {
                capture->Release();
                capture = nullptr;
            }
            if (auto*& backbuffer = app->Renderer()->backbufferSurface) {
                backbuffer->Release();
                backbuffer = nullptr;
            }
            ModelDispose(model);
        }
    } record{app.get(), model.get()};
    PathResolutionWorkspace paths{};
    CHECK(ModelLoadPMD(model.get(), nullptr, path.c_str(), nullptr, 0, 0, 1, &physics, paths,
                       ModelResourceMode::CpuOnly));
    app->ModelSlot(0) = model.get();
    model->comboSelIndex = 0;
    model->toonFlag = 1;
    const render::ModelMatrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (unsigned n = 0; n < model->boneCount; ++n)
        std::copy(identity.begin(), identity.end(), model->boneTable[n].matInit);
    // Native buffers deliberately start with sentinel bytes. A successful RHI
    // frame must not lock/upload them; fallback must overwrite them with this
    // frame's already prepared CPU output, rather than last frame's geometry.
    const unsigned stride = 32 + 16 * (model->physicsMode == 2 ? model->pmxAdditionalUvCount : 0);
    ComPtr<IDirect3DVertexBuffer9> main, edge;
    ComPtr<IDirect3DIndexBuffer9> indices;
    CHECK(SUCCEEDED(device->CreateVertexBuffer(stride * model->vertexCount, 0, 0, D3DPOOL_MANAGED,
                                               &main, nullptr)));
    CHECK(SUCCEEDED(device->CreateVertexBuffer(16 * model->vertexCount, 0, 0, D3DPOOL_MANAGED,
                                               &edge, nullptr)));
    const bool wide = model->physicsMode == 2 && model->pmxVertexIndexSize >= 4;
    CHECK(SUCCEEDED(device->CreateIndexBuffer((wide ? 4 : 2) * model->indexCount, 0,
                                              wide ? D3DFMT_INDEX32 : D3DFMT_INDEX16,
                                              D3DPOOL_MANAGED, &indices, nullptr)));
    void* mapped = nullptr;
    CHECK(SUCCEEDED(main->Lock(0, 0, &mapped, 0)));
    std::memset(mapped, 0xa5, stride * model->vertexCount);
    CHECK(SUCCEEDED(main->Unlock()));
    CHECK(SUCCEEDED(indices->Lock(0, 0, &mapped, 0)));
    for (unsigned n = 0; n < model->indexCount; ++n) {
        const auto value = std::uint32_t(model->indices[n]);
        std::memcpy(static_cast<char*>(mapped) + n * (wide ? 4 : 2), &value, wide ? 4 : 2);
    }
    CHECK(SUCCEEDED(indices->Unlock()));
    main->AddRef();
    edge->AddRef();
    indices->AddRef(); // ModelDispose owns these references.
    model->vertexBuffer = main.Get();
    model->vertexBuffer2 = edge.Get();
    model->indexBuffer = indices.Get();
    CHECK(app->ViewportRenderer().Select(*app, api, false));
    app->CpuRenderFrame().view.viewport = {0, 0, unsigned(app->ViewportRect().right),
                                           unsigned(app->ViewportRect().bottom)};
    D3DMATRIX projection{};
    const float y = 1.f / std::tan(45.f * 3.14159265f / 360.f);
    projection.m[0][0] = y / wrapper->aspectRatio;
    projection.m[1][1] = y;
    projection.m[2][2] = 10000.f / 9999.5f;
    projection.m[2][3] = 1;
    projection.m[3][2] = -.5f * projection.m[2][2];
    CHECK(SUCCEEDED(device->SetTransform(D3DTS_PROJECTION, &projection)));
    RgbaImage lastImage;
    const auto frame = [&](bool expectRhi, bool driver = false) {
        const auto before = app->FrameRenderingStats();
        const auto updates = app->CpuModelFrames().at(model.get())->frames;
        const auto catchups = app->state.recordedFrameCount;
        app->MessageSeen() = 1;
#ifdef MIKUDANCESTUDIO_DIAG
        const auto indexed = mikudancestudio::mme::ProbeIndexedDrawBridge().calls;
        const auto triangles = mikudancestudio::mme::ProbeTriangleDrawBridge().calls;
        const auto lineDraws = mikudancestudio::mme::ProbeLineDrawBridge().calls;
#endif
        if (driver)
            FrameDriver(app.get());
        else
            RenderFrameScene(app.get(), expectRhi ? &lastImage : nullptr);
        const auto after = app->FrameRenderingStats();
        if (after.lastBackend != (expectRhi ? app->ViewportRenderer().Selected() : Backend::D3D9)) {
            std::fprintf(stderr, "frame %llu expectedRhi=%d reason=%s cpuReady=%d\n",
                         after.preparations, expectRhi, app->ViewportRenderer().Reason().c_str(),
                         app->CpuModelFrames().at(model.get())->ready);
            std::string mmeReason;
            CheckPortableMme(mmeReason);
            std::fprintf(stderr, "MME portability: %s\n", mmeReason.c_str());
        }
        CHECK(after.rendered && after.preparations == before.preparations + 1);
        if (driver)
            CHECK(app->state.recordedFrameCount == catchups + 1);
        if (app->ModelSlot(0)) {
            CHECK(after.geometryUpdates == before.geometryUpdates + 1);
            CHECK(app->CpuModelFrames().at(model.get())->frames == updates + 1);
        } else
            CHECK(after.geometryUpdates == before.geometryUpdates);
        CHECK(after.lastBackend ==
              (expectRhi ? app->ViewportRenderer().Selected() : Backend::D3D9));
        if (expectRhi) {
            CHECK(after.rhiFrames == before.rhiFrames + 1 &&
                  after.legacyScenes == before.legacyScenes &&
                  after.legacyPasses == before.legacyPasses);
#ifdef MIKUDANCESTUDIO_DIAG
            CHECK(indexed == mikudancestudio::mme::ProbeIndexedDrawBridge().calls &&
                  triangles == mikudancestudio::mme::ProbeTriangleDrawBridge().calls &&
                  lineDraws == mikudancestudio::mme::ProbeLineDrawBridge().calls);
#endif
        } else
            CHECK(after.legacyScenes == before.legacyScenes + 1 &&
                  after.legacyPasses > before.legacyPasses);
        CHECK(!IsWindowVisible(window.value));
    };
    const auto image = [&]() -> const RgbaImage& { return lastImage; };
    const auto legacyImage = [&] {
        ComPtr<IDirect3DSurface9> source, cpu;
        CHECK(SUCCEEDED(device->GetRenderTarget(0, &source)));
        D3DSURFACE_DESC surface{};
        CHECK(SUCCEEDED(source->GetDesc(&surface)));
        CHECK(SUCCEEDED(device->CreateOffscreenPlainSurface(
            surface.Width, surface.Height, surface.Format, D3DPOOL_SYSTEMMEM, &cpu, nullptr)));
        CHECK(SUCCEEDED(device->GetRenderTargetData(source.Get(), cpu.Get())));
        D3DLOCKED_RECT locked{};
        CHECK(SUCCEEDED(cpu->LockRect(&locked, nullptr, D3DLOCK_READONLY)));
        std::vector<std::uint8_t> bytes(std::size_t(surface.Width) * surface.Height * 4);
        for (unsigned row = 0; row < surface.Height; ++row)
            std::memcpy(bytes.data() + std::size_t(row) * surface.Width * 4,
                        static_cast<char*>(locked.pBits) + row * locked.Pitch, surface.Width * 4);
        CHECK(SUCCEEDED(cpu->UnlockRect()));
        return bytes;
    };
    // An unused native capture from the previous owner must not survive a
    // successful RHI handover, even with capture disabled for this frame.
    CHECK(SUCCEEDED(device->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                                          D3DPOOL_DEFAULT, &app->CaptureTexture(), nullptr)));
    app->CpuNativeCaptureTexturePresent() = true;
    frame(true);
    CHECK(!app->CaptureTexture() && !app->CpuNativeCaptureTexturePresent());
    const auto first = image();
    frame(true);
    CHECK(image().pixels == first.pixels);
    CHECK(SUCCEEDED(main->Lock(0, 0, &mapped, D3DLOCK_READONLY)));
    CHECK(static_cast<unsigned char*>(mapped)[0] == 0xa5);
    CHECK(SUCCEEDED(main->Unlock()));
    if (benchmark) {
        using Clock = std::chrono::steady_clock;
        std::printf(
            "FRAME_DRIVER %s vertices=%u materials=%u %ldx%ld frames=%u; no readback in timing\n",
            mode.c_str(), model->vertexCount, model->materialCount, app->ViewportRect().right,
            app->ViewportRect().bottom, count);
        for (bool animated : {false, true}) {
            app->DeltaTime() = animated ? 1.f / 60 : 0;
            std::vector<double> times;
            SceneRendererStats before;
            VulkanStats vkBefore;
            D3D12Stats dxBefore;
            for (unsigned n = 0; n < count + 3; ++n) {
                if (n == 3) {
                    before = app->ViewportRenderer().Statistics();
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
                    vkBefore = QueryVulkanStats(*app->ViewportRenderer().GetDevice());
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
                    dxBefore = QueryD3D12Stats(*app->ViewportRenderer().GetDevice());
#endif
                }
                if (animated) {
                    for (unsigned b = 0; b < model->boneCount; ++b)
                        model->boneTable[b].matInit[12] = std::sin(n * .15f);
                    if (model->physicsMode == 2 && model->morphCount)
                        model->morphs[0].value = (n % 7) / 7.f;
                }
                const auto start = Clock::now();
                frame(true, true);
                const auto ms =
                    std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                if (n >= 3)
                    times.push_back(ms);
            }
            std::sort(times.begin(), times.end());
            double total = 0;
            for (double t : times)
                total += t;
            std::printf("%s average=%.3f p50=%.3f p95=%.3f ms legacy_passes=%llu\n",
                        animated ? "generated_bone_animation" : "static", total / times.size(),
                        times[times.size() / 2],
                        times[std::min(times.size() - 1, times.size() * 95 / 100)],
                        app->FrameRenderingStats().legacyPasses);
            const auto stats = app->ViewportRenderer().Statistics();
            std::printf("buffers created=%llu reused=%llu slot_waits=%llu resident=%zu pooled=%zu "
                        "retiring=%zu bytes=%zu/%zu/%zu\n",
                        stats.bufferCreations - before.bufferCreations,
                        stats.bufferReuses - before.bufferReuses,
                        stats.bufferWaits - before.bufferWaits, stats.residentResources,
                        stats.pooledBuffers, stats.retiringResources, stats.residentBufferBytes,
                        stats.pooledBufferBytes, stats.retiringBufferBytes);
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
            if (api == Backend::Vulkan) {
                const auto vk = QueryVulkanStats(*app->ViewportRenderer().GetDevice());
                std::printf(
                    "vulkan allocations=%llu timeline_waits=%llu idle_waits=%llu "
                    "frame_waits=%llu transfer_waits=%llu busy_uploads=%llu retiring=%zu\n",
                    vk.bufferAllocations - vkBefore.bufferAllocations,
                    vk.timelineWaits - vkBefore.timelineWaits, vk.idleWaits - vkBefore.idleWaits,
                    vk.frameWaits - vkBefore.frameWaits, vk.transferWaits - vkBefore.transferWaits,
                    vk.uploadBusyRejects - vkBefore.uploadBusyRejects, vk.retiringResources);
            }
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
            if (api == Backend::D3D12) {
                const auto dx = QueryD3D12Stats(*app->ViewportRenderer().GetDevice());
                std::printf(
                    "dx12 default_allocations=%llu upload_allocations=%llu upload_reuses=%llu\n",
                    dx.bufferAllocations - dxBefore.bufferAllocations,
                    dx.uploadBufferAllocations - dxBefore.uploadBufferAllocations,
                    dx.uploadBufferReuses - dxBefore.uploadBufferReuses);
            }
#endif
            std::fflush(stdout);
        }
        return 0;
    }
    if (argc > 3) {
        mdl::AccessoryRecord screen{};
        const auto file = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
        wcscpy_s(screen.sourcePath, file.c_str());
        screen.parentModel = -1;
        screen.visible = 1;
        screen.opacity = 1;
        screen.scale = 3;
        screen.position[0] = 10;
        struct Accessory {
            MMDApp* app;
            ~Accessory() { app->AccessorySlot(0) = nullptr; }
        } accessory{app.get()};
        app->AccessorySlot(0) = &screen;
        app->CaptureMode() = ScreenCaptureMode::FullFrame;
        frame(true);
        const auto freshHistory = image().pixels;
        frame(true);
        CHECK(image().pixels != freshHistory); // screen.bmp really reads history
        // The CPU-only accessory is removed for the native frame. History
        // invalidation must still follow ownership, not consumer presence.
        app->AccessorySlot(0) = nullptr;
        app->WireframeRenderingEnabled() = 1;
        frame(false);
        CHECK(app->CaptureTexture() && app->CpuNativeCaptureTexturePresent());
        app->WireframeRenderingEnabled() = 0;
        app->AccessorySlot(0) = &screen;
        frame(true);
        CHECK(image().pixels == freshHistory);
        CHECK(!app->CaptureTexture() && !app->CpuNativeCaptureTexturePresent());
        frame(true);
        CHECK(image().pixels != freshHistory);
        app->CaptureMode() = ScreenCaptureMode::Disabled;
    }
    for (int shadow : {1, 2}) {
        app->state.selfShadowEnabled = 1;
        app->SelfShadowMode() = shadow;
        app->ShadowDistance() = .01125f;
        frame(true);
        const auto shadowImage = image().pixels;
        frame(true);
        CHECK(image().pixels == shadowImage);
    }
    app->state.selfShadowEnabled = 0;
    frame(true);
    // Live camera edits and frame identity are consumed before selection.
    const float yaw = app->CameraYaw();
    app->MouseX() = 20;
    app->PreviousMouseX() = 5;
    app->MouseY() = 10;
    app->PreviousMouseY() = 2;
    app->RightMouseButtonState() = 3;
    app->ViewportInputActive() = 1;
    app->state.coordinateSystem = 2;
    ConsumeRightButtonDrag(app.get()); // synthetic input; no desktop events
    app->RightMouseButtonState() = 0;
    app->ViewportInputActive() = 0;
    CHECK(std::abs(app->CameraYaw() - (yaw - .15f)) < 1.e-6f);
    app->CurrentFrame() = 12;
    frame(true);
    CHECK(image().pixels != first.pixels && app->CpuRenderFrame().scene.frame == 12);
    app->PlaybackActive() = 1;
    app->PlaybackStartSeconds() = app->PlaybackCursorSeconds() = .4f;
    InitModelTrackCursors(model.get(), .4f, app->PlaybackPhysicsMode());
    app->PlaybackEndSeconds() = 1;
    app->MilliToSec() = .001f;
    app->TimeNowLow() = 50;
    app->AccessoryTrackActive(254) = 1;
    app->AccessoryTrackCursor(254) = 7;
    frame(true, true);
    CHECK(std::abs(app->PlaybackCursorSeconds() - .45f) < 1.e-6f);
    CHECK(!app->AccessoryTrackActive(254) && !app->AccessoryTrackCursor(254));
    CHECK(app->ViewportRect().right == 160 && HeapValidate(GetProcessHeap(), 0, nullptr));
    app->PlaybackActive() = 0;
    const auto fallbackBuffers = [&] {
        CHECK(SUCCEEDED(main->Lock(0, 0, &mapped, D3DLOCK_READONLY)));
        const auto& cpu = app->CpuModelFrames().at(model.get())->geometry;
        CHECK(std::memcmp(mapped, cpu.MainData(), cpu.MainSize()) == 0);
        CHECK(SUCCEEDED(main->Unlock()));
    };
    // Preparation failure after CPU updates: same frame, one publication.
    model->displayState = 1;
    app->CameraYaw() = -.2f;
    frame(false);
    CHECK(!app->ViewportRenderer().OwnsPresentation() &&
          app->ViewportRenderer().Reason() == "model_display_override");
    fallbackBuffers();
    model->displayState = 0;
    frame(true);
    // Compatibility selection happens before CPU-only preparation.
    app->WireframeRenderingEnabled() = 1;
    frame(false);
    app->WireframeRenderingEnabled() = 0;
    frame(true);
    // Submission failure uses the current geometry and hides the old child.
    CHECK(app->ViewportRenderer().GetDevice()->PrepareForReset());
    model->boneTable[0].matInit[12] += 3;
    frame(false);
    CHECK(!app->ViewportRenderer().OwnsPresentation());
    fallbackBuffers();
    const auto failedFrame = legacyImage();
    CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
    frame(true);
    CHECK(app->ViewportRenderer().Select(*app, Backend::D3D9, false));
    frame(false);
    CHECK(legacyImage() == failedFrame); // strict same-state native frame comparison
    CHECK(app->ViewportRenderer().Select(*app, api, false));
    frame(true);
    // If fallback upload also fails, the driver must not declare/present a
    // completed frame using old native geometry. A later valid frame recovers.
    const auto beforeUploadFailure = app->FrameRenderingStats();
    const auto beforeFailedUpdates = app->CpuModelFrames().at(model.get())->frames;
    model->displayState = 1;
    const auto nativeMain = model->vertexBuffer;
    model->vertexBuffer = nullptr;
    app->MessageSeen() = 1;
    FrameDriver(app.get());
    model->vertexBuffer = nativeMain;
    model->displayState = 0;
    CHECK(!app->FrameRenderingStats().rendered && !app->ViewportRenderer().OwnsPresentation());
    CHECK(app->FrameRenderingStats().legacyScenes == beforeUploadFailure.legacyScenes);
    CHECK(app->CpuModelFrames().at(model.get())->frames == beforeFailedUpdates + 1);
    frame(true);
    for (const auto selected :
         {Backend::D3D11, Backend::D3D12, Backend::Vulkan, Backend::D3D9, api}) {
        if (selected != Backend::D3D9 && !BackendAvailable(selected))
            continue;
        CHECK(app->ViewportRenderer().Select(*app, selected, false));
        frame(selected != Backend::D3D9);
    }
    app->ViewportRect().right += 24;
    frame(true);
    if (image().width != 184)
        std::fprintf(stderr, "resize image=%ux%u view=%ld,%ld,%ld,%ld reason=%s\n", image().width,
                     image().height, app->ViewportRect().left, app->ViewportRect().top,
                     app->ViewportRect().right, app->ViewportRect().bottom,
                     app->ViewportRenderer().Reason().c_str());
    CHECK(image().width == 184);
    app->ViewportRenderer().UnloadScene();
    app->ModelSlot(0) = nullptr;
    frame(true);
    CHECK(image().pixels[0] == 255);
    app->ModelSlot(0) = model.get();
    frame(true);
    // Production driver retains one render/update boundary and runs its own
    // normal catch-up/physics/text/line tail, rather than a second frame path.
    frame(true, true);
    CHECK(app->FrameRenderingStats().fallbackGeometryUploads >= 2);
    // Actual MME assignments enter the shared frame only when the complete
    // profile is supported. The host window remains hidden throughout.
    ComPtr<ID3DXEffect> standard;
    ComPtr<ID3DXBuffer> compileErrors;
    const auto sourceRoot = std::filesystem::path(MDS_TEST_SOURCE_DIR);
    CHECK(SUCCEEDED(D3DXCreateEffectFromFileW(
        device.Get(), (sourceRoot / L"res/assets/skin_effect_sm3.fx").c_str(), nullptr, nullptr, 0,
        nullptr, &standard, &compileErrors)));
    wrapper->effect = standard.Get();
    mikudancestudio::mme::OnDeviceCreated(app.get(), window.value);
    struct MmeCleanup {
        MMDApp* app;
        D3DRenderer* renderer;
        ~MmeCleanup() {
            mikudancestudio::mme::OnDeviceDestroyed(app);
            renderer->effect = nullptr;
        }
    } mmeCleanup{app.get(), wrapper.get()};
    CHECK(app->ViewportRenderer().CanRenderFrame(*app));
    CHECK(MmeHostEffectsEnabled());
    const auto materialEffect = (sourceRoot / L"assets/effects/portable/MaterialTint.fx").string();
    CHECK(
        ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, materialEffect));
    std::string portableReason;
    if (!CheckPortableMme(portableReason))
        throw std::runtime_error(portableReason);
    const auto assignedEffect = [&](const void* object) -> ID3DXEffect* {
        for (const auto& entry : ::mme::g_ownerManager->bindings)
            if (entry.first.model &&
                entry.first.model->objectId() == reinterpret_cast<std::uintptr_t>(object) &&
                entry.first.subset == -1)
                return entry.second->effect;
        return nullptr;
    };
    const bool runtimeEffects = app->ViewportRenderer().GetDevice()->ShaderFeatures().hlsl;
    frame(runtimeEffects);
    if (runtimeEffects) {
        const auto portableImage = image();
        frame(true);
        CHECK(image().pixels == portableImage.pixels);
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), 0, ""));
        frame(true);
        CHECK(image().pixels != portableImage.pixels);
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), 0,
                                     materialEffect));
        frame(true);
        CHECK(image().pixels == portableImage.pixels);
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     materialEffect));
        const float tint[] = {.25f, .5f, 1, 1}, originalTint[] = {.75f, 1, .5f, 1};
        CHECK(assignedEffect(model.get()));
        CHECK(SUCCEEDED(assignedEffect(model.get())->SetFloatArray("tint", tint, 4)));
        frame(true);
        CHECK(image().pixels != portableImage.pixels);
        CHECK(SUCCEEDED(assignedEffect(model.get())->SetFloatArray("tint", originalTint, 4)));
        frame(true);
        CHECK(image().pixels == portableImage.pixels);
        CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
        frame(true);
        CHECK(image().pixels == portableImage.pixels);
    }
    if (std::filesystem::exists(path.parent_path() / L"bound.x")) {
        mdl::AccessoryRecord carrier{};
        CHECK(LoadAccessoryObject(app.get(), &carrier, (path.parent_path() / L"bound.x").c_str()));
        carrier.parentModel = -1;
        carrier.visible = 1;
        carrier.opacity = 1;
        carrier.scale = 1;
        app->AccessorySlot(0) = &carrier;
        struct RemoveCarrier {
            MMDApp* app;
            mdl::AccessoryRecord* record;
            ~RemoveCarrier() {
                app->AccessorySlot(0) = nullptr;
                DisposeAccessory(record);
            }
        } removeCarrier{app.get(), &carrier};
        CHECK(mikudancestudio::mme::PrepareEffectAssignments(app.get()));
        const auto postPath = (sourceRoot / L"assets/effects/portable/TwoStage.fx").string();
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, postPath));
        if (!CheckPortableMme(portableReason))
            throw std::runtime_error(portableReason);
        frame(runtimeEffects);
        if (runtimeEffects) {
            CHECK(image().pixels[0] == 159 && image().pixels[1] == 207 &&
                  image().pixels[2] == 255 && image().pixels[3] == 32);
            const auto graded = image().pixels;
            frame(true);
            CHECK(image().pixels == graded);
            const float add[] = {.25f, .0625f, 0, .125f};
            CHECK(assignedEffect(&carrier));
            CHECK(SUCCEEDED(assignedEffect(&carrier)->SetFloatArray("addColor", add, 4)));
            frame(true);
            CHECK(image().pixels[0] == 191);
            const auto savedWidth = app->ViewportRect().right;
            app->ViewportRect().right += 8;
            frame(true);
            CHECK(image().pixels[0] == 191);
            app->ViewportRect().right = savedWidth;
            const float originalAdd[] = {.125f, .0625f, 0, .125f};
            CHECK(SUCCEEDED(assignedEffect(&carrier)->SetFloatArray("addColor", originalAdd, 4)));
            frame(true);
            CHECK(image().pixels == graded);
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            frame(true);
            CHECK(image().pixels == graded);
        }
        const auto controlsPath = (sourceRoot / L"assets/effects/portable/ControlPost.fx").string();
        const auto controlMaterial =
            (sourceRoot / L"assets/effects/portable/ControlMaterial.fx").string();
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, controlsPath));
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     controlMaterial));
        if (!CheckPortableMme(portableReason))
            throw std::runtime_error(portableReason);
        frame(runtimeEffects);
        const auto controlBaseline = image().pixels;
        std::vector<MmePortableEffect> controlEffects;
        CHECK(MmePortableEffects(controlEffects, portableReason));
        const auto exportedControl =
            std::find_if(controlEffects.begin(), controlEffects.end(), [&](const auto& e) {
                return e.object == reinterpret_cast<std::uintptr_t>(&carrier);
            });
        CHECK(exportedControl != controlEffects.end());
        const auto exposure =
            std::find_if(exportedControl->parameters.begin(), exportedControl->parameters.end(),
                         [](const auto& p) { return p.name == "sharedExposure"; });
        CHECK(exposure != exportedControl->parameters.end() && exposure->shared &&
              exposure->type == MmePortableParameter::Type::Float && exposure->value.size() == 4);
        carrier.position[0] = 2;
        carrier.position[1] = 4;
        frame(runtimeEffects);
        float position[4]{}, world[16]{};
        CHECK(SUCCEEDED(assignedEffect(&carrier)->GetFloatArray("controlPosition", position, 4)));
        CHECK(SUCCEEDED(assignedEffect(&carrier)->GetFloatArray("controlWorld", world, 16)));
        CHECK(position[0] == 2 && position[1] == 4 && world[12] == 2 && world[13] == 4);
        if (runtimeEffects)
            CHECK(image().pixels != controlBaseline);
        CHECK(SUCCEEDED(assignedEffect(model.get())->SetFloat("sharedExposure", .25f)));
        float shared = 0;
        CHECK(SUCCEEDED(assignedEffect(&carrier)->GetFloat("sharedExposure", &shared)) &&
              shared == .25f);
        frame(runtimeEffects);
        CHECK(SUCCEEDED(assignedEffect(&carrier)->GetFloat("sharedExposure", &shared)) &&
              shared == .25f);
        carrier.position[0] = carrier.position[1] = 0;
        CHECK(SUCCEEDED(assignedEffect(&carrier)->SetFloat("sharedExposure", 1)));
        frame(runtimeEffects);
        if (runtimeEffects) {
            CHECK(image().pixels == controlBaseline);
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            frame(true);
            CHECK(image().pixels == controlBaseline);
        }
        const auto includedControls =
            (sourceRoot / L"assets/effects/portable/IncludedControlPost.fx").string();
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1,
                                     includedControls));
        CHECK(CheckPortableMme(portableReason));
        frame(runtimeEffects);
        if (runtimeEffects)
            CHECK(image().pixels == controlBaseline);
        const auto revisionInclude = path.parent_path() / L"portable-revision.fxsub";
        const auto revisionEffect = path.parent_path() / L"portable-revision.fx";
        std::filesystem::copy_file(sourceRoot / L"assets/effects/portable/ControlPost.fx",
                                   revisionInclude,
                                   std::filesystem::copy_options::overwrite_existing);
        {
            std::ofstream root(revisionEffect);
            root << "#include \"portable-revision.fxsub\"\n";
        }
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1,
                                     revisionEffect.string()));
        {
            std::ofstream edited(revisionInclude);
            edited << "#error changed after effect load\n";
        }
        CHECK(CheckPortableMme(portableReason));
        frame(runtimeEffects);
        if (runtimeEffects) {
            CHECK(image().pixels == controlBaseline);
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            frame(true);
            CHECK(image().pixels == controlBaseline);
        }
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     materialEffect));
        const auto sceneScript = (sourceRoot / L"assets/effects/portable/SceneScript.fx").string();
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, sceneScript));
        CHECK(SUCCEEDED(assignedEffect(&carrier)->SetInt("iteration", 77)));
        if (!CheckPortableMme(portableReason))
            throw std::runtime_error(portableReason);
        int untouchedIndex = -1;
        CHECK(SUCCEEDED(assignedEffect(&carrier)->GetInt("iteration", &untouchedIndex)) &&
              untouchedIndex == 77); // Eligibility cannot execute the script.
        frame(runtimeEffects);
        const auto scriptBaseline = image().pixels;
        int scriptIndex = -1;
        CHECK(SUCCEEDED(assignedEffect(&carrier)->GetInt("iteration", &scriptIndex)) &&
              scriptIndex == 1);
        if (runtimeEffects)
            CHECK(scriptBaseline[0] >= 174 && scriptBaseline[0] <= 176);
        carrier.visible = 0;
        frame(runtimeEffects);
        if (runtimeEffects)
            CHECK(image().pixels[0] == 255);
        carrier.visible = 1;
        frame(runtimeEffects);
        if (runtimeEffects)
            CHECK(image().pixels == scriptBaseline);
        CHECK(SUCCEEDED(assignedEffect(&carrier)->SetInt("iterations", 3)));
        frame(runtimeEffects);
        CHECK(SUCCEEDED(assignedEffect(&carrier)->GetInt("iteration", &scriptIndex)) &&
              scriptIndex == 2);
        if (runtimeEffects)
            CHECK(image().pixels[0] >= 190 && image().pixels[0] <= 192);
        CHECK(SUCCEEDED(assignedEffect(&carrier)->SetInt("iterations", 0)));
        CHECK(CheckPortableMme(portableReason));
        frame(runtimeEffects);
        if (runtimeEffects)
            CHECK(image().pixels[0] == 255);
        CHECK(SUCCEEDED(assignedEffect(&carrier)->SetInt("iterations", -4097)));
        CHECK(!CheckPortableMme(portableReason));
        const auto beforeScriptFallback = app->ViewportRenderer().Statistics();
        frame(false);
        CHECK(app->ViewportRenderer().Statistics().submissions == beforeScriptFallback.submissions);
        CHECK(SUCCEEDED(assignedEffect(&carrier)->SetInt("iterations", 2)));
        CHECK(CheckPortableMme(portableReason));
        frame(runtimeEffects);
        if (runtimeEffects) {
            CHECK(image().pixels == scriptBaseline);
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            frame(true);
            CHECK(image().pixels == scriptBaseline);
        }
        // Keep the live effect/pool and control object across backend ownership
        // changes. Normal native frames must still execute zero DX9 scene draws.
        auto switchBaseline = runtimeEffects ? scriptBaseline : SnapshotBytes{};
        for (const auto selected :
             {Backend::D3D9, Backend::D3D11, Backend::D3D12, Backend::Vulkan, api}) {
            if (selected != Backend::D3D9 && !BackendAvailable(selected))
                continue;
            CHECK(app->ViewportRenderer().Select(*app, selected, false));
            if (selected == Backend::D3D9) {
                frame(false);
                continue;
            }
            const bool shaderSupport =
                selected != Backend::Vulkan || (sizeof(void*) == 8 && runtimeEffects);
            frame(shaderSupport);
            CHECK(SUCCEEDED(assignedEffect(&carrier)->GetFloat("sharedExposure", &shared)) &&
                  shared == 1);
            if (shaderSupport) {
                CHECK(app->ViewportRenderer().GetDevice()->ShaderFeatures().hlsl);
                if (switchBaseline.empty())
                    switchBaseline = image().pixels;
                CHECK(image().pixels == switchBaseline);
            }
        }
        const auto hdrPath = (sourceRoot / L"assets/effects/portable/MrtHdr.fx").string();
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, hdrPath));
        if (!CheckPortableMme(portableReason))
            throw std::runtime_error(portableReason);
        std::vector<MmePortableEffect> effects;
        CHECK(MmePortableEffects(effects, portableReason));
        const auto hdr = std::find_if(effects.begin(), effects.end(),
                                      [](const auto& e) { return e.profile == "mrt-hdr-v1"; });
        CHECK(hdr != effects.end() && hdr->passes.size() == 4 && hdr->targets.size() == 6);
        CHECK(hdr->passes[0].colors.size() == 2 && hdr->passes[1].sampled.size() == 2);
        frame(runtimeEffects);
        auto* hdrFx = assignedEffect(&carrier);
        const auto layer = hdrFx->GetParameterByName(nullptr, "LayerA");
        const auto format = hdrFx->GetAnnotationByName(layer, "Format");
        CHECK(format && SUCCEEDED(hdrFx->SetString(format, "A32B32G32R32F")));
        CHECK(!CheckPortableMme(portableReason));
        CHECK(portableReason == "MME_target_format_or_extent_not_supported");
        const auto beforeUnsupported = app->ViewportRenderer().Statistics();
        frame(false);
        CHECK(app->ViewportRenderer().Statistics().uploads == beforeUnsupported.uploads);
        CHECK(app->ViewportRenderer().Statistics().submissions == beforeUnsupported.submissions);
        CHECK(SUCCEEDED(hdrFx->SetString(format, "A16B16G16R16F")));
        CHECK(CheckPortableMme(portableReason));
        frame(runtimeEffects);
        if (runtimeEffects) {
            const auto original = image().pixels;
            CHECK(original[2] > 200 && original[3] == 32);
            frame(true);
            CHECK(image().pixels == original);
            const float multiply[] = {.25f, .5f, .75f, .5f};
            CHECK(SUCCEEDED(assignedEffect(&carrier)->SetFloatArray("multiplyColor", multiply, 4)));
            frame(true);
            CHECK(image().pixels != original);
            const auto width = app->ViewportRect().right;
            app->ViewportRect().right += 8;
            frame(true);
            app->ViewportRect().right = width;
            const float reset[] = {.5f, .75f, 1, .5f};
            CHECK(SUCCEEDED(assignedEffect(&carrier)->SetFloatArray("multiplyColor", reset, 4)));
            frame(true);
            CHECK(image().pixels == original);
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            frame(true);
            CHECK(image().pixels == original);
        }
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, ""));
        app->AccessorySlot(0) = nullptr;
        CHECK(mikudancestudio::mme::PrepareEffectAssignments(app.get()));
    }
    // A shader with no portable opt-in must still use the same-frame DX9 path.
    const auto unsupported = (sourceRoot / L"res/assets/skin_effect_sm3.fx").string();
    CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, unsupported));
    CHECK(!CheckPortableMme(portableReason));
    frame(false);
    CHECK(app->ViewportRenderer().Reason() == "MME");
    CHECK(
        ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, materialEffect));
    frame(runtimeEffects);
    if (runtimeEffects) {
        const auto brokenPath =
            std::filesystem::temp_directory_path() /
            ("mds-broken-portable-" + std::to_string(GetCurrentProcessId()) + ".fx");
        struct TemporaryEffect {
            std::filesystem::path p;
            ~TemporaryEffect() {
                std::error_code ec;
                std::filesystem::remove(p, ec);
            }
        } temporaryEffect{brokenPath};
        std::ifstream source(materialEffect, std::ios::binary);
        std::string text(std::istreambuf_iterator<char>(source), {});
        const auto at = text.find("baseTexture.Sample(baseSampler,o.uv)");
        CHECK(at != std::string::npos);
        text.replace(at, std::strlen("baseTexture.Sample(baseSampler,o.uv)"),
                     "UnknownPortableFunction(o.uv)");
        std::ofstream(brokenPath, std::ios::binary) << text;
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     brokenPath.string()));
        CHECK(CheckPortableMme(portableReason));
        const auto before = app->ViewportRenderer().Statistics();
        frame(false);
        CHECK(app->ViewportRenderer().Reason().rfind("MME_shader:", 0) == 0);
        CHECK(app->ViewportRenderer().Statistics().uploads == before.uploads);
        frame(false); // failed-program cache; no repeated compiler work
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     materialEffect));
        frame(true); // same device, no explicit recreation required
    }
    if (!rayEffect.empty()) {
        const auto rayRoot = rayEffect.parent_path();
        const auto portablePixels = image().pixels;
        const auto rayFallback = [&] {
            const auto before = app->ViewportRenderer().Statistics();
            CHECK(!CheckPortableMme(portableReason));
            CHECK(portableReason == "MME_profile_not_supported");
            frame(false);
            CHECK(app->ViewportRenderer().Reason() == "MME");
            CHECK(!app->ViewportRenderer().OwnsPresentation());
            CHECK(app->ViewportRenderer().Statistics().uploads == before.uploads);
            CHECK(app->ViewportRenderer().Statistics().submissions == before.submissions);
        };
        // Even one unsupported subset makes the entire frame use DX9.
        for (const auto& relative : {L"Main/main.fx", L"Outline/Fixed Color/outline.fx"}) {
            const auto effect = (rayRoot / relative).string();
            CHECK(std::filesystem::is_regular_file(rayRoot / relative));
            CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), 0, effect));
            rayFallback();
            CHECK(
                ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, effect));
            rayFallback();
            CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                         materialEffect));
            if (!CheckPortableMme(portableReason))
                throw std::runtime_error("Ray material replacement: " + portableReason);
            frame(runtimeEffects);
            if (runtimeEffects)
                CHECK(image().pixels == portablePixels);
        }
        {
            mdl::AccessoryRecord carrier{};
            CHECK(LoadAccessoryObject(app.get(), &carrier, (rayRoot / L"ray.x").c_str()));
            carrier.parentModel = -1;
            carrier.visible = 1;
            carrier.opacity = carrier.scale = 1;
            app->AccessorySlot(0) = &carrier;
            struct RemoveRay {
                MMDApp* app;
                mdl::AccessoryRecord* carrier;
                ~RemoveRay() {
                    app->AccessorySlot(0) = nullptr;
                    DisposeAccessory(carrier);
                }
            } removeRay{app.get(), &carrier};
            CHECK(mikudancestudio::mme::PrepareEffectAssignments(app.get()));
            CHECK(assignedEffect(&carrier)); // actual ray.x -> ray.fx auto assignment
            rayFallback();                   // supported material mixed with Ray's scene script
            rayFallback();                   // repeated frames still update only once
            app->ViewportRect().right += 8;
            rayFallback();
            app->ViewportRect().right -= 8;
            const auto other = api == Backend::D3D11 ? Backend::D3D12 : Backend::D3D11;
            if (BackendAvailable(other)) {
                CHECK(app->ViewportRenderer().Select(*app, other, false));
                rayFallback();
                CHECK(app->ViewportRenderer().Select(*app, api, false));
            }
            app->ViewportRenderer().UnloadScene();
            rayFallback();
            CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, ""));
            app->AccessorySlot(0) = nullptr;
            CHECK(mikudancestudio::mme::PrepareEffectAssignments(app.get()));
        }
        // DX9 retains derived child bindings in its cache. They must not
        // prevent recovery after their owning scene effect is removed.
        CHECK(std::any_of(::mme::g_ownerManager->bindings.begin(),
                          ::mme::g_ownerManager->bindings.end(),
                          [](const auto& entry) { return entry.first.turnId != 0; }));
        CHECK(CheckPortableMme(portableReason));
        frame(runtimeEffects);
        if (runtimeEffects)
            CHECK(image().pixels == portablePixels);
        else {
            // A missing runtime shader compiler can still render ordinary
            // frames after both Ray and the custom material are removed.
            CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, ""));
            frame(true);
        }
        std::printf(
            "Ray material/subset/outline/scene fallback, repeat, resize, switch, unload and recovery passed\n");
    }
    CHECK(!blockedDialogs);
    std::printf(
        "PASS %s %s: real frame owner, zero DX9 draws on RHI frames, once-only geometry, same-frame fallback, history handover, device recovery, switching, resize, unload and driver; window never shown\n",
        mode.c_str(), path.extension().u8string().c_str());
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
