#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dbghelp.h>
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
#include "sas_exec.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exception) -> LONG {
        std::fprintf(stderr, "exception=%lx address=%p\n",
                     exception->ExceptionRecord->ExceptionCode,
                     exception->ExceptionRecord->ExceptionAddress);
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        void* stack[40]{};
        const auto count = CaptureStackBackTrace(0, 40, stack, nullptr);
        for (unsigned n = 0; n < count; ++n) {
            alignas(SYMBOL_INFO) char data[sizeof(SYMBOL_INFO) + 1024]{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(data);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 1023;
            DWORD64 delta = 0;
            if (SymFromAddr(GetCurrentProcess(), DWORD64(stack[n]), &delta, symbol))
                std::fprintf(stderr, "%s+%llu\n", symbol->Name, delta);
        }
        return EXCEPTION_EXECUTE_HANDLER;
    });
    CHECK(argc >= 3);
    const std::string mode = argv[1];
    const auto api = mode == "dx11"   ? Backend::D3D11
                     : mode == "dx12" ? Backend::D3D12
                                      : Backend::Vulkan;
    const auto path = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    const bool realStandard = argc > 3 && std::string(argv[3]) == "--mme-standard";
    const bool benchmark = argc > 3 && std::string(argv[3]) == "--benchmark";
    const unsigned count = argc > 4 ? std::max(3, std::atoi(argv[4])) : 12;
    std::filesystem::path rayEffect;
    if (!benchmark && !realStandard && argc > 4) {
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
        HWND value;
        Window(int width, int height) {
            RECT rect{0, 0, width, height};
            AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
            value = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
                                    L"Hidden frame owner", WS_OVERLAPPEDWINDOW, 0, 0,
                                    rect.right - rect.left, rect.bottom - rect.top, nullptr,
                                    nullptr, GetModuleHandleW(nullptr), nullptr);
        }
        bool Resize(int width, int height) {
            RECT client{};
            if (GetClientRect(value, &client) && client.right == width && client.bottom == height)
                return true;
            RECT rect{0, 0, width, height};
            AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
            return SetWindowPos(value, nullptr, 0, 0, rect.right - rect.left,
                                rect.bottom - rect.top, SWP_NOACTIVATE | SWP_NOZORDER) != FALSE;
        }
        ~Window() { DestroyWindow(value); }
    } window(benchmark ? 1280 : 160, benchmark ? 720 : 120);
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
        // Match the real host layout: the parent client must contain the
        // viewport. Otherwise EnsureWindow correctly clips a resize to the
        // old client extent, and FrameDriver can replace the test's viewport.
        const auto view = app->ViewportRect();
        CHECK(window.Resize(view.right, view.bottom));
        const auto clockUpdates = ::mme::g_frameClockUpdates;
        const bool effectsEnabled = MmeHostEffectsEnabled() != 0;
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
        CHECK(!::mme::g_frameClockFrozen);
        if (effectsEnabled)
            CHECK(::mme::g_frameClockUpdates == clockUpdates + 1);
    };
    const auto image = [&]() -> const RgbaImage& { return lastImage; };
    const auto legacySurfaceImage = [&](IDirect3DSurface9* source) {
        ComPtr<IDirect3DSurface9> cpu;
        D3DSURFACE_DESC surface{};
        CHECK(SUCCEEDED(source->GetDesc(&surface)));
        CHECK(SUCCEEDED(device->CreateOffscreenPlainSurface(
            surface.Width, surface.Height, surface.Format, D3DPOOL_SYSTEMMEM, &cpu, nullptr)));
        CHECK(SUCCEEDED(device->GetRenderTargetData(source, cpu.Get())));
        D3DLOCKED_RECT locked{};
        CHECK(SUCCEEDED(cpu->LockRect(&locked, nullptr, D3DLOCK_READONLY)));
        std::vector<std::uint8_t> bytes(std::size_t(surface.Width) * surface.Height * 4);
        for (unsigned row = 0; row < surface.Height; ++row)
            std::memcpy(bytes.data() + std::size_t(row) * surface.Width * 4,
                        static_cast<char*>(locked.pBits) + row * locked.Pitch, surface.Width * 4);
        CHECK(SUCCEEDED(cpu->UnlockRect()));
        return bytes;
    };
    const auto legacyImage = [&] {
        ComPtr<IDirect3DSurface9> source;
        CHECK(SUCCEEDED(device->GetRenderTarget(0, &source)));
        return legacySurfaceImage(source.Get());
    };
    if (realStandard) {
        // Native devices are created lazily by the first render, including
        // after selecting DX9 and returning to a newer backend. Cache actual
        // shader capabilities before assigning the effect under test.
        std::map<Backend, bool> shaderSupport;
        for (const auto selected : {Backend::D3D11, Backend::D3D12, Backend::Vulkan}) {
            if (!BackendAvailable(selected))
                continue;
            CHECK(app->ViewportRenderer().Select(*app, selected, false));
            frame(true);
            shaderSupport[selected] = app->ViewportRenderer().GetDevice()->ShaderFeatures().hlsl;
        }
        CHECK(app->ViewportRenderer().Select(*app, api, false));
        ComPtr<ID3DXEffect> hostEffect;
        ComPtr<ID3DXBuffer> errors;
        const auto root = std::filesystem::path(MDS_TEST_SOURCE_DIR);
        CHECK(SUCCEEDED(D3DXCreateEffectFromFileW(
            device.Get(), (root / L"res/assets/skin_effect_sm3.fx").c_str(), nullptr, nullptr, 0,
            nullptr, &hostEffect, &errors)));
        wrapper->effect = hostEffect.Get();
        mikudancestudio::mme::OnDeviceCreated(app.get(), window.value);
        struct EffectOwner {
            MMDApp* app;
            D3DRenderer* renderer;
            ~EffectOwner() {
                mikudancestudio::mme::OnDeviceDestroyed(app);
                renderer->effect = nullptr;
            }
        } effectOwner{app.get(), wrapper.get()};
        CHECK(app->ViewportRenderer().CanRenderFrame(*app));
        CHECK(MmeHostEffectsEnabled());
        const auto effect = (root / L"assets/effects/portable/LegacyStandard.fx").string();
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, effect));
        std::string reason;
        CHECK(CheckPortableMme(reason));
        struct TimelineControl {
            HWND value;
            ~TimelineControl() { DestroyWindow(value); }
        } timeline{CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_DISABLED, 0, 0, 1, 1,
                                   window.value, reinterpret_cast<HMENU>(317),
                                   GetModuleHandleW(nullptr), nullptr)};
        CHECK(timeline.value && !IsWindowVisible(timeline.value));
        SnapshotBytes baseline;
        for (const auto selected :
             {api, Backend::D3D9, Backend::D3D11, Backend::D3D12, Backend::Vulkan}) {
            if (selected != Backend::D3D9 && !BackendAvailable(selected))
                continue;
            CHECK(app->ViewportRenderer().Select(*app, selected, false));
            const bool supported = selected != Backend::D3D9 && shaderSupport.at(selected);
            frame(supported);
            frame(supported);
            if (!supported)
                continue;
            if (baseline.empty())
                baseline = image().pixels;
            if (image().pixels != baseline) {
                unsigned maximum = 0, changed = 0;
                for (std::size_t n = 0; n < baseline.size(); ++n) {
                    const auto delta =
                        unsigned(std::abs(int(image().pixels[n]) - int(baseline[n])));
                    maximum = std::max(maximum, delta);
                    changed += delta != 0;
                }
                std::fprintf(
                    stderr, "real switch backend=%u frame=%d clock=%g,%g,%g,%g changed=%u max=%u\n",
                    unsigned(selected), app->CurrentFrame(), ::mme::g_frameTimeBase,
                    ::mme::g_deltaSeconds, ::mme::g_lastFrameTime, ::mme::g_frameDelta, changed,
                    maximum);
            }
            CHECK(image().pixels == baseline);
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            frame(true);
            CHECK(image().pixels == baseline);
            app->CurrentFrame() = 12;
            frame(true);
            app->CurrentFrame() = 4;
            frame(true); // backwards seek
            CHECK(app->ViewportRenderer().GetDevice()->PrepareForReset());
            frame(false); // same-frame native fallback, single clock/geometry update
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            app->CurrentFrame() = 0;
            frame(true);
            frame(true);
            CHECK(image().pixels == baseline);
        }
        CHECK(!baseline.empty());
        bool coverage = false;
        for (std::size_t p = 0; p < baseline.size(); p += 4)
            coverage |= baseline[p] != 255 || baseline[p + 1] != 255 || baseline[p + 2] != 255;
        CHECK(coverage);
        std::printf(
            "PASS real MME standard parameters: vertices=%u materials=%u, native switches, device rebuild, seek, same-frame fallback, zero DX9 draws on RHI; window never shown\n",
            model->vertexCount, model->materialCount);
        return 0;
    }
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
    std::map<Backend, bool> runtimeShaderSupport;
    for (const auto selected :
         {Backend::D3D11, Backend::D3D12, Backend::Vulkan, Backend::D3D9, api}) {
        if (selected != Backend::D3D9 && !BackendAvailable(selected))
            continue;
        CHECK(app->ViewportRenderer().Select(*app, selected, false));
        frame(selected != Backend::D3D9);
        if (selected != Backend::D3D9)
            runtimeShaderSupport[selected] =
                app->ViewportRenderer().GetDevice()->ShaderFeatures().hlsl;
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
            if (!entry.first.turnId && entry.first.model &&
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
    // Unannotated FX9 is imported from its frozen source, with arbitrary names,
    // reflected constants and native SAS technique selection.
    const auto legacyMaterial =
        (sourceRoot / L"assets/effects/portable/LegacyMaterial.fx").string();
    CHECK(
        ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, legacyMaterial));
    if (!CheckPortableMme(portableReason))
        throw std::runtime_error(portableReason);
    std::vector<MmePortableEffect> legacyEffects;
    CHECK(MmePortableEffects(legacyEffects, portableReason));
    CHECK(std::any_of(legacyEffects.begin(), legacyEffects.end(),
                      [](const auto& e) { return e.legacy && e.profile == "legacy-material-v1"; }));
    frame(runtimeEffects);
    const auto importedImage = image().pixels;
    CHECK(SUCCEEDED(assignedEffect(model.get())->SetFloat("Exposure", .25f)));
    frame(runtimeEffects);
    if (runtimeEffects)
        CHECK(image().pixels != importedImage);
    CHECK(SUCCEEDED(assignedEffect(model.get())->SetFloat("Exposure", 1)));
    frame(runtimeEffects);
    if (runtimeEffects) {
        CHECK(image().pixels == importedImage);
        CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
        frame(true);
        CHECK(image().pixels == importedImage);
        app->ViewportRect().right += 8;
        frame(true);
        CHECK(image().width == 192);
        app->ViewportRect().right -= 8;
        frame(true);
        CHECK(image().pixels == importedImage);
    }
    auto importBaseline = runtimeEffects ? importedImage : SnapshotBytes{};
    for (const auto selected :
         {Backend::D3D9, Backend::D3D11, Backend::D3D12, Backend::Vulkan, api}) {
        if (selected != Backend::D3D9 && !BackendAvailable(selected))
            continue;
        CHECK(app->ViewportRenderer().Select(*app, selected, false));
        if (selected == Backend::D3D9) {
            frame(false);
            continue;
        }
        const bool supported = runtimeShaderSupport.at(selected);
        frame(supported);
        if (supported) {
            if (importBaseline.empty())
                importBaseline = image().pixels;
            CHECK(image().pixels == importBaseline);
        }
    }
    CHECK(
        ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, materialEffect));
    frame(runtimeEffects);
    // Standard semantics use this frame's CPU state and the original clock.
    const auto standardEffect =
        (sourceRoot / L"assets/effects/portable/LegacyStandard.fx").string();
    CHECK(
        ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, standardEffect));
    CHECK(CheckPortableMme(portableReason));
    struct ClockControl {
        HWND value;
        ~ClockControl() { DestroyWindow(value); }
    } clockControl{CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_DISABLED, 0, 0, 1, 1,
                                   window.value, reinterpret_cast<HMENU>(317),
                                   GetModuleHandleW(nullptr), nullptr)};
    CHECK(clockControl.value && !IsWindowVisible(clockControl.value));
    const auto savedFrame = app->CurrentFrame();
    frame(runtimeEffects);
    frame(runtimeEffects); // paused timeline: both elapsed clocks settle at zero
    const auto standardImage = image().pixels;
    for (const auto selected :
         {Backend::D3D9, Backend::D3D11, Backend::D3D12, Backend::Vulkan, api}) {
        if (selected != Backend::D3D9 && !BackendAvailable(selected))
            continue;
        CHECK(app->ViewportRenderer().Select(*app, selected, false));
        const bool supported = selected != Backend::D3D9 && runtimeShaderSupport.at(selected);
        frame(supported);
        if (supported && runtimeEffects)
            CHECK(image().pixels == standardImage);
    }
    app->CurrentFrame() += 6;
    frame(runtimeEffects);
    if (runtimeEffects) {
        std::vector<MmePortableEffect> snapshot;
        CHECK(MmePortableEffects(snapshot, portableReason));
        CHECK(!snapshot.empty() && snapshot[0].clockReady && snapshot[0].clock[3] != 0);
        frame(true);
        CHECK(app->ViewportRenderer().GetDevice()->PrepareForReset());
        frame(false); // clock was already advanced; native fallback must reuse it
        CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
        frame(true);
    }
    app->CurrentFrame() = savedFrame;
    DestroyWindow(clockControl.value);
    clockControl.value = nullptr;
    CHECK(
        ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, materialEffect));
    frame(runtimeEffects);
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
        // Ordinary FX9 scene + a separate material sharing the same live pool
        // render product. No MDSRhiProfile, shader entries or input annotations.
        const auto automaticScene =
            (sourceRoot / L"assets/effects/portable/LegacyScene.fx").string();
        const auto sharedMaterial =
            (sourceRoot / L"assets/effects/portable/LegacySharedMaterial.fx").string();
        CHECK(
            ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, automaticScene));
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     sharedMaterial));
        if (!CheckPortableMme(portableReason))
            throw std::runtime_error(portableReason);
        std::vector<MmePortableEffect> automaticEffects;
        CHECK(MmePortableEffects(automaticEffects, portableReason));
        std::uint64_t sharedIdentity = 0;
        unsigned aliases = 0;
        for (const auto& effect : automaticEffects)
            for (const auto& target : effect.targets)
                if (target.name == "SharedMask") {
                    CHECK(target.identity);
                    if (sharedIdentity)
                        CHECK(target.identity == sharedIdentity);
                    sharedIdentity = target.identity;
                    ++aliases;
                }
        CHECK(aliases >= 2);
        frame(runtimeEffects);
        const auto automaticBaseline = image().pixels;
        if (runtimeEffects) {
            const float changed[] = {.25f, .5f, .5f, 1}, original[] = {.5f, .75f, 1, 1};
            CHECK(SUCCEEDED(assignedEffect(&carrier)->SetFloatArray("maskColor", changed, 4)));
            frame(true);
            CHECK(image().pixels != automaticBaseline);
            CHECK(SUCCEEDED(assignedEffect(&carrier)->SetFloatArray("maskColor", original, 4)));
            frame(true);
            CHECK(image().pixels == automaticBaseline);
            ComPtr<IDirect3DBaseTexture9> sharedTexture;
            CHECK(SUCCEEDED(assignedEffect(&carrier)->GetTexture("SharedMask", &sharedTexture)) &&
                  sharedTexture);
            CHECK(SUCCEEDED(assignedEffect(&carrier)->SetTexture("SharedMask", nullptr)));
            CHECK(!CheckPortableMme(portableReason));
            CHECK(portableReason.rfind("MME_shared_target_unbound:", 0) == 0);
            const auto beforeUnbound = app->ViewportRenderer().Statistics();
            frame(false);
            CHECK(app->ViewportRenderer().Statistics().uploads == beforeUnbound.uploads);
            CHECK(app->ViewportRenderer().Statistics().submissions == beforeUnbound.submissions);
            CHECK(
                SUCCEEDED(assignedEffect(&carrier)->SetTexture("SharedMask", sharedTexture.Get())));
            frame(true);
            CHECK(image().pixels == automaticBaseline);
            const auto oldWidth = app->ViewportRect().right;
            app->ViewportRect().right += 8;
            frame(true);
            app->ViewportRect().right = oldWidth;
            frame(true);
            CHECK(image().pixels == automaticBaseline);
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            frame(true);
            CHECK(image().pixels == automaticBaseline);
        }
        auto automaticSwitchBaseline = runtimeEffects ? automaticBaseline : SnapshotBytes{};
        for (const auto selected :
             {Backend::D3D9, Backend::D3D11, Backend::D3D12, Backend::Vulkan, api}) {
            if (selected != Backend::D3D9 && !BackendAvailable(selected))
                continue;
            CHECK(app->ViewportRenderer().Select(*app, selected, false));
            const bool supported = selected != Backend::D3D9 && runtimeShaderSupport.at(selected);
            frame(supported);
            if (supported) {
                if (automaticSwitchBaseline.empty())
                    automaticSwitchBaseline = image().pixels;
                CHECK(image().pixels == automaticSwitchBaseline);
            }
        }
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     materialEffect));
        // Ordinary FX9 DefaultEffect chain plus two native scene carriers.
        // Read the DX9 render target directly; no window is ever shown.
        const auto originalExtent = app->ViewportRect();
        const auto savedModelOrder = model->comboSelIndex;
        model->comboSelIndex = 1; // Native host order zero denotes background, not a model.
        auto* savedSprites = app->SpriteOverlayVertices();
        const auto savedSpriteCount = app->SpriteOverlayPrimitiveCount();
        const auto savedTextCount = app->TextOverlayPrimitiveCount(),
                   savedLineCount = app->LineOverlayPrimitiveCount();
        app->SpriteOverlayVertices() = nullptr;
        app->SpriteOverlayPrimitiveCount() = app->TextOverlayPrimitiveCount() =
            app->LineOverlayPrimitiveCount() = 0;
        app->ViewportRenderer().ClearOverlay(EditorOverlay::Text);
        app->ViewportRenderer().ClearOverlay(EditorOverlay::Lines);
        app->ViewportRect() = {0, 0, 160, 120};
        ComPtr<IDirect3DSurface9> savedColor, savedDepth, oracleColor, oracleDepth;
        D3DVIEWPORT9 savedViewport{};
        CHECK(SUCCEEDED(device->GetRenderTarget(0, &savedColor)) &&
              SUCCEEDED(device->GetDepthStencilSurface(&savedDepth)) &&
              SUCCEEDED(device->GetViewport(&savedViewport)));
        CHECK(SUCCEEDED(device->CreateRenderTarget(160, 120, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
                                                   0, FALSE, &oracleColor, nullptr)));
        CHECK(SUCCEEDED(device->CreateDepthStencilSurface(
            160, 120, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &oracleDepth, nullptr)));
        D3DVIEWPORT9 oracleViewport{0, 0, 160, 120, 0, 1};
        const auto defaultScene =
            (sourceRoot / L"assets/effects/portable/DefaultEffectScene.fx").string();
        const auto nestedScene = (sourceRoot / L"assets/effects/portable/NestedScene.fx").string();
        const auto leafMaterial =
            (sourceRoot / L"assets/effects/portable/LegacyLeafMaterial.fx").string();
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     leafMaterial));
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, defaultScene));
        const auto oracle = [&](const char* label) {
            const auto width = unsigned(app->ViewportRect().right),
                       height = unsigned(app->ViewportRect().bottom);
            if (oracleViewport.Width != width || oracleViewport.Height != height) {
                // These private oracle surfaces replace the native logical
                // target. Drop the snapshot family just as host target/device
                // replacement does; otherwise its grow-only cache rescales an
                // old larger snapshot when this isolated oracle shrinks again.
                ::mme::g_context->ReleaseMainSnapshotCache();
                CHECK(SUCCEEDED(device->SetDepthStencilSurface(nullptr)) &&
                      SUCCEEDED(device->SetRenderTarget(0, savedColor.Get())));
                oracleColor.Reset();
                oracleDepth.Reset();
                CHECK(SUCCEEDED(device->CreateRenderTarget(width, height, D3DFMT_A8R8G8B8,
                                                           D3DMULTISAMPLE_NONE, 0, FALSE,
                                                           &oracleColor, nullptr)));
                CHECK(SUCCEEDED(device->CreateDepthStencilSurface(width, height, D3DFMT_D24S8,
                                                                  D3DMULTISAMPLE_NONE, 0, FALSE,
                                                                  &oracleDepth, nullptr)));
                oracleViewport = {0, 0, width, height, 0, 1};
            }
            CHECK(app->ViewportRenderer().Select(*app, Backend::D3D9, false));
            CHECK(SUCCEEDED(device->SetRenderTarget(0, oracleColor.Get())) &&
                  SUCCEEDED(device->SetDepthStencilSurface(oracleDepth.Get())) &&
                  SUCCEEDED(device->SetViewport(&oracleViewport)));
            frame(false);
            // A failed native effect is not a valid image oracle: later host
            // draws can fall back through whatever shader state it left behind.
            for (auto* effect : {assignedEffect(model.get()), assignedEffect(&carrier)}) {
                struct Probe {
                    ID3DXEffect* effect;
                    bool found = false, valid = false;
                } probe{effect};
                ::mme::MmeEngineForEachSas(
                    [](void* data, ::mme::SasEffect* sas) {
                        auto& probe = *static_cast<Probe*>(data);
                        if (sas->effect != probe.effect)
                            return true;
                        probe.found = true;
                        probe.valid = !sas->runFailed;
                        if (!probe.valid)
                            std::fprintf(stderr, "Native oracle effect failed: %s\n",
                                         sas->log.c_str());
                        return false;
                    },
                    &probe);
                CHECK(probe.found && probe.valid);
            }
            const auto bgra = legacyImage();
            CHECK(bgra.size() == width * height * 4);
            std::map<std::string, std::vector<std::uint8_t>> nativeInputs;
            if (std::string(label).find("scripted") != std::string::npos) {
                // Keep both inputs for failure diagnostics before RHI preparation
                // can replace the native effect's viewport-relative resources.
                for (const auto* name : {"Capture", "Gbuffer5RT"}) {
                    ComPtr<IDirect3DBaseTexture9> texture;
                    ComPtr<IDirect3DTexture9> image;
                    ComPtr<IDirect3DSurface9> surface;
                    CHECK(SUCCEEDED(assignedEffect(&carrier)->GetTexture(name, &texture)) &&
                          SUCCEEDED(texture.As(&image)) &&
                          SUCCEEDED(image->GetSurfaceLevel(0, &surface)));
                    auto bytes = legacySurfaceImage(surface.Get());
                    CHECK(bytes.size() == bgra.size());
                    nativeInputs.emplace(name, std::move(bytes));
                }
            }
            CHECK(app->ViewportRenderer().Select(*app, api, false));
            frame(runtimeEffects);
            if (runtimeEffects) {
                const auto& rgba = image();
                CHECK(rgba.width == width && rgba.height == height);
                unsigned maximum = 0;
                for (std::size_t n = 0; n < bgra.size(); n += 4)
                    for (unsigned c = 0; c < 4; ++c)
                        maximum = std::max(maximum,
                                           unsigned(std::abs(int(rgba.pixels[n + c]) -
                                                             int(bgra[n + (c == 3 ? 3 : 2 - c)]))));
                std::printf("%s %s DX9 max RGBA delta=%u (limit=2)\n", mode.c_str(), label,
                            maximum);
                if (maximum > 2) {
                    unsigned shown = 0;
                    for (std::size_t n = 0; n < bgra.size() && shown < 8; n += 4) {
                        unsigned pixelDelta = 0;
                        for (unsigned c = 0; c < 4; ++c)
                            pixelDelta =
                                std::max(pixelDelta,
                                         unsigned(std::abs(int(rgba.pixels[n + c]) -
                                                           int(bgra[n + (c == 3 ? 3 : 2 - c)]))));
                        if (pixelDelta > 2) {
                            std::printf("pixel %zu,%zu RHI=%u,%u,%u,%u DX9=%u,%u,%u,%u\n",
                                        (n / 4) % width, (n / 4) / width, rgba.pixels[n],
                                        rgba.pixels[n + 1], rgba.pixels[n + 2], rgba.pixels[n + 3],
                                        bgra[n + 2], bgra[n + 1], bgra[n], bgra[n + 3]);
                            for (const auto& input : nativeInputs) {
                                const auto& bytes = input.second;
                                std::printf("  native %s=%u,%u,%u,%u\n", input.first.c_str(),
                                            bytes[n + 2], bytes[n + 1], bytes[n], bytes[n + 3]);
                            }
                            ++shown;
                        }
                    }
                }
                CHECK(maximum <= 2);
            }
        };
        oracle("DefaultEffect two-level mip chain");
        const auto childBaseline = image().pixels;
        MmePortablePrepareParameters();
        std::vector<MmePortableTurn> childTurns;
        CHECK(MmePortableTurns(childTurns, portableReason));
        CHECK(childTurns.size() == 2 && childTurns[0].target.name == "Child" &&
              childTurns[1].target.name == "Leaf");
        CHECK(childTurns[0].target.mipmapped && childTurns[1].target.mipmapped);
        CHECK(childTurns[1].target.width == 64 && childTurns[1].target.height == 32);
        {
            RenderFrame background;
            background.targets = {{1,
                                   {160, 120, TextureFormat::Rgba8,
                                    TextureAccess::ColorAttachment | TextureAccess::CopySource}}};
            background.finalColor = 1;
            FramePass clear;
            clear.color = FrameAttachment{1};
            background.passes = {clear};
            auto child = childTurns.front();
            child.effects.clear();
            std::vector<MmePortableEffect> completed;
            // An invalid host-background draw must not enter child turns.
            CHECK(PrependPortableMmeTurns(background, {child}, {PortableMmeDrawSource{}}, completed,
                                          portableReason));
            for (const auto& pass : background.passes)
                CHECK(pass.draws.empty());
        }
        if (runtimeEffects) {
            frame(true);
            CHECK(image().pixels == childBaseline);
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            frame(true);
            CHECK(image().pixels == childBaseline);
            app->ViewportRect().right += 8;
            frame(true);
            app->ViewportRect().right -= 8;
            frame(true);
            CHECK(image().pixels == childBaseline);
        }
        // The native target's ordered rows remain the sole selection rule.
        auto* parentBinding = ::mme::MmeResolveModelEffectBinding(
            ::mme::g_context->modelRegistry.at(reinterpret_cast<std::uintptr_t>(&carrier)));
        CHECK(parentBinding && parentBinding->sas);
        auto target =
            std::find_if(parentBinding->sas->resources.begin(), parentBinding->sas->resources.end(),
                         [](const auto& r) { return r.name == "Child"; });
        CHECK(target != parentBinding->sas->resources.end());
        const auto rows = target->defaultEffectMap;
        const auto uploadsBeforeChildFailure = app->ViewportRenderer().Statistics().uploads;
        const auto requestedMips = target->texMips;
        target->texMips = 2; // Partial native chains have no portable generation contract.
        CHECK(!MmePortableTurns(childTurns, portableReason));
        CHECK(portableReason == "MME_target_format_or_mips_not_supported");
        frame(false);
        CHECK(!runtimeEffects || app->ViewportRenderer().Reason() == "MME");
        CHECK(app->ViewportRenderer().Statistics().uploads == uploadsBeforeChildFailure);
        target->texMips = requestedMips;
        target->defaultEffectMap = {{"self", "hide"}, {"*.pm*", nestedScene}, {"*", "hide"}};
        CHECK(!MmePortableTurns(childTurns, portableReason));
        CHECK(portableReason == "MME_offscreen_scene_carrier_not_supported");
        frame(false); // Child selection must not invent a native scene carrier.
        CHECK(app->ViewportRenderer().Statistics().uploads == uploadsBeforeChildFailure);
        target->defaultEffectMap = {
            {"self", "hide"},
            {"*.pm*", (sourceRoot / L"res/assets/skin_effect_sm3.fx").string()},
            {"*", "hide"}};
        frame(false); // One unsupported descendant rejects the whole frame before upload.
        CHECK(app->ViewportRenderer().Statistics().uploads == uploadsBeforeChildFailure);
        target->defaultEffectMap = {{"self", "hide"}};
        oracle("unlisted object"); // A native offscreen lookup miss swallows its draw.
        if (runtimeEffects)
            CHECK(image().pixels != childBaseline); // The child geometry must affect the image.
        target->defaultEffectMap = {{"self", "hide"}, {"*.pm*", "none"}, {"*", "hide"}};
        frame(false); // Native host-shader forwarding is still whole-frame fallback.
        CHECK(app->ViewportRenderer().Reason() == "MME_offscreen_host_pipeline_not_supported" ||
              !runtimeEffects);
        for (const auto* assignment : {"hide", "main_default"}) {
            target->defaultEffectMap = {{"self", "hide"}, {"*.pm*", assignment}, {"*", "hide"}};
            oracle(assignment);
        }
        target->defaultEffectMap = rows;
        oracle("restored child assignments");
        if (runtimeEffects)
            CHECK(image().pixels == childBaseline);
        {
            mdl::AccessoryRecord outer{};
            CHECK(
                LoadAccessoryObject(app.get(), &outer, (path.parent_path() / L"bound.x").c_str()));
            outer.parentModel = -1;
            outer.visible = 1;
            outer.opacity = outer.scale = 1;
            outer.order = 1;
            app->AccessorySlot(1) = &outer;
            struct RemoveOuter {
                MMDApp* app;
                mdl::AccessoryRecord* object;
                ~RemoveOuter() {
                    app->AccessorySlot(1) = nullptr;
                    DisposeAccessory(object);
                }
            } removeOuter{app.get(), &outer};
            CHECK(mikudancestudio::mme::PrepareEffectAssignments(app.get()));
            CHECK(
                ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&outer), -1, nestedScene));
            oracle("nested scene reverse-enter forward-resume");
            const auto nestedBaseline = image().pixels;
            carrier.order = 2;
            oracle("reversed scene order");
            if (runtimeEffects)
                CHECK(image().pixels != nestedBaseline);
            carrier.order = 0;
            oracle("restored scene order");
            if (runtimeEffects)
                CHECK(image().pixels == nestedBaseline);
            for (const auto selected : {Backend::D3D11, Backend::D3D12, Backend::Vulkan, api}) {
                if (!BackendAvailable(selected))
                    continue;
                CHECK(app->ViewportRenderer().Select(*app, selected, false));
                const bool supported = runtimeShaderSupport.at(selected);
                frame(supported);
                if (supported && runtimeEffects)
                    CHECK(image().pixels == nestedBaseline);
            }
            CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&outer), -1, ""));
            app->AccessorySlot(1) = nullptr;
            CHECK(mikudancestudio::mme::PrepareEffectAssignments(app.get()));
        }
        oracle("nested scene removal");
        if (runtimeEffects)
            CHECK(image().pixels == childBaseline);
        const auto mrtMaterial =
            (sourceRoot / L"assets/effects/portable/LegacyMrtMaterial.fx").string();
        const auto mrtScene = (sourceRoot / L"assets/effects/portable/LegacyMrtScene.fx").string();
        CHECK(
            ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, mrtMaterial));
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, mrtScene));
        const auto mrtEditMode = app->EditMode();
        const auto beforeMrtOutline = app->ViewportRenderer().Statistics();
        frame(false); // Native outline has a different vertex layout and no MRT shader yet.
        CHECK(app->ViewportRenderer().Statistics().uploads == beforeMrtOutline.uploads &&
              app->ViewportRenderer().Statistics().submissions == beforeMrtOutline.submissions);
        app->EditMode() = ViewportEditMode::Bone; // A supported material-only editing frame.
        oracle("ordinary FX9 MRT/HDR relative targets");
        const auto mrtBaseline = image().pixels;
        std::vector<MmePortableEffect> mrtEffects;
        CHECK(MmePortableEffects(mrtEffects, portableReason));
        const auto mrt = std::find_if(mrtEffects.begin(), mrtEffects.end(),
                                      [](const auto& e) { return e.post; });
        CHECK(mrt != mrtEffects.end() && mrt->profile == "legacy-scene-v1" &&
              mrt->passes[0].colors.size() == 2);
        const auto half = std::find_if(mrt->targets.begin(), mrt->targets.end(),
                                       [](const auto& t) { return t.name == "HalfA"; });
        CHECK(half != mrt->targets.end() && half->relative &&
              half->viewportRatio == (std::array<float, 2>{.5f, .5f}) &&
              half->format == MmePortableFormat::Rgba16Float);
        if (runtimeEffects) {
            frame(true);
            CHECK(image().pixels == mrtBaseline);
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            frame(true);
            CHECK(image().pixels == mrtBaseline);
        }
        app->ViewportRect() = {0, 0, 163, 123};
        oracle("ordinary MRT odd viewport resize");
        app->ViewportRect() = {0, 0, 160, 120};
        oracle("ordinary MRT viewport restore");
        if (runtimeEffects)
            CHECK(image().pixels == mrtBaseline);
        for (const auto selected : {Backend::D3D11, Backend::D3D12, Backend::Vulkan, api}) {
            if (!BackendAvailable(selected))
                continue;
            CHECK(app->ViewportRenderer().Select(*app, selected, false));
            const bool supported = runtimeShaderSupport.at(selected);
            frame(supported);
            if (supported && runtimeEffects)
                CHECK(image().pixels == mrtBaseline);
        }
        // A single-output material cannot fill two capture attachments. The
        // whole frame falls back before any RHI upload, then recovers in place.
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     leafMaterial));
        const auto beforeMrtMismatch = app->ViewportRenderer().Statistics();
        frame(false);
        CHECK(app->ViewportRenderer().Statistics().uploads == beforeMrtMismatch.uploads &&
              app->ViewportRenderer().Statistics().submissions == beforeMrtMismatch.submissions);
        CHECK(
            ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1, mrtMaterial));
        frame(runtimeEffects);
        if (runtimeEffects)
            CHECK(image().pixels == mrtBaseline);
        const auto scriptedMaterial =
            (sourceRoot / L"assets/effects/portable/LegacyMaterialMap.fx").string();
        const auto scriptedScene =
            (sourceRoot / L"assets/effects/portable/LegacyMaterialMapScene.fx").string();
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     scriptedMaterial));
        CHECK(
            ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, scriptedScene));
        oracle("scripted material two MRT groups");
        const auto scriptedBaseline = image().pixels;
        app->ViewportRect() = {0, 0, 163, 123};
        oracle("scripted material odd viewport resize");
        app->ViewportRect() = {0, 0, 160, 120};
        oracle("scripted material viewport restore");
        if (runtimeEffects) {
            CHECK(image().pixels == scriptedBaseline);
            CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
            frame(true);
            CHECK(image().pixels == scriptedBaseline);
        }
        for (const auto selected : {Backend::D3D11, Backend::D3D12, Backend::Vulkan, api}) {
            if (!BackendAvailable(selected))
                continue;
            CHECK(app->ViewportRenderer().Select(*app, selected, false));
            const bool supported = runtimeShaderSupport.at(selected);
            frame(supported);
            if (supported && runtimeEffects)
                CHECK(image().pixels == scriptedBaseline);
        }
        // Removing the scene publisher removes the only initialization of the
        // shared MRT targets. Preparation must reject before RHI submission.
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, ""));
        const auto beforeUninitialized = app->ViewportRenderer().Statistics();
        frame(false);
        CHECK(app->ViewportRenderer().Statistics().uploads == beforeUninitialized.uploads &&
              app->ViewportRenderer().Statistics().submissions == beforeUninitialized.submissions);
        CHECK(
            ::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(&carrier), -1, scriptedScene));
        frame(runtimeEffects);
        if (runtimeEffects)
            CHECK(image().pixels == scriptedBaseline);
        app->EditMode() = mrtEditMode;
        CHECK(SUCCEEDED(device->SetRenderTarget(0, savedColor.Get())) &&
              SUCCEEDED(device->SetDepthStencilSurface(savedDepth.Get())) &&
              SUCCEEDED(device->SetViewport(&savedViewport)));
        app->ViewportRect() = originalExtent;
        model->comboSelIndex = savedModelOrder;
        app->SpriteOverlayVertices() = savedSprites;
        app->SpriteOverlayPrimitiveCount() = savedSpriteCount;
        app->TextOverlayPrimitiveCount() = savedTextCount;
        app->LineOverlayPrimitiveCount() = savedLineCount;
        CHECK(::mme::MmeAssignEffect(reinterpret_cast<std::uintptr_t>(model.get()), -1,
                                     materialEffect));
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
            CHECK(portableReason.rfind("MME_legacy_", 0) == 0);
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
