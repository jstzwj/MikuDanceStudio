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
#include "pump_input.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
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
        const auto indexed = mme::ProbeIndexedDrawBridge().calls;
        const auto triangles = mme::ProbeTriangleDrawBridge().calls;
        const auto lineDraws = mme::ProbeLineDrawBridge().calls;
#endif
        if (driver)
            FrameDriver(app.get());
        else
            RenderFrameScene(app.get(), expectRhi ? &lastImage : nullptr);
        const auto after = app->FrameRenderingStats();
        if (after.lastBackend != (expectRhi ? app->ViewportRenderer().Selected() : Backend::D3D9))
            std::fprintf(stderr, "frame %llu expectedRhi=%d reason=%s cpuReady=%d\n",
                         after.preparations, expectRhi, app->ViewportRenderer().Reason().c_str(),
                         app->CpuModelFrames().at(model.get())->ready);
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
            CHECK(indexed == mme::ProbeIndexedDrawBridge().calls &&
                  triangles == mme::ProbeTriangleDrawBridge().calls &&
                  lineDraws == mme::ProbeLineDrawBridge().calls);
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
            for (unsigned n = 0; n < count + 3; ++n) {
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
    std::printf(
        "PASS %s %s: real frame owner, zero DX9 draws on RHI frames, once-only geometry, same-frame fallback, history handover, device recovery, switching, resize, unload and driver; window never shown\n",
        mode.c_str(), path.extension().u8string().c_str());
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
