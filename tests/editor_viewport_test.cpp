#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dbghelp.h>
#include "mikudancestudio/editor_scene.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/model_cpu_bridge.hpp"
#include "mikudancestudio/model_input_bridge.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/rhi_scene_preview.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <wrl/client.h>
using namespace mikudancestudio;
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " + #x + \
                                     " " + app->ViewportRenderer().Reason());                      \
    } while (0)
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
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
    const std::string mode = argc > 1 ? argv[1] : "dx11";
    const auto api = mode == "dx11"   ? Backend::D3D11
                     : mode == "dx12" ? Backend::D3D12
                                      : Backend::Vulkan;
    auto app = std::make_unique<MMDApp>();
    std::memset(&app->state, 0, sizeof(app->state));
    g_Block = app.get();
    struct Globals {
        ~Globals() { g_Block = nullptr; }
    } globals;
    struct Window {
        HWND value = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
                                     L"Editor viewport hidden test", WS_OVERLAPPEDWINDOW, 0, 0, 320,
                                     240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~Window() {
            if (value)
                DestroyWindow(value);
        }
    } parent, floating;
    CHECK(parent.value && floating.value && !IsWindowVisible(parent.value) &&
          !IsWindowVisible(floating.value));
    app->MainWindow() = parent.value;
    app->HInstance() = GetModuleHandleW(nullptr);
    app->ViewportRect() = {16, 12, 176, 132};
    app->CameraMode() = 1;
    app->EditMode() = ViewportEditMode::None;
    app->CameraDistance() = -30;
    app->CameraFov() = 45;
    app->CameraParentModel() = -1;
    app->CaptureMode() = ScreenCaptureMode::Disabled;
    app->GroundGridEnabled() = 0;
    app->GroundShadowEnabled() = 0;
    app->state.selfShadowEnabled = 0;
    app->state.blackBackgroundEnabled = 0;
    auto& inputs = app->CpuRenderFrame();
    const render::ModelMatrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    inputs.world = inputs.lightRotation = identity;
    inputs.view.aspect = 4.f / 3;
    inputs.scene.camera.distance = -30;
    inputs.scene.camera.fov = 45;
    inputs.scene.camera.parentModel = -1;
    inputs.scene.lightState = {.6f, .7f, .8f, -.5f, -1, -.5f};
    app->CpuRenderFrameReady() = true;
    CHECK(app->ViewportRenderer().Select(*app, api, false));
    RgbaImage image;
    CHECK(app->ViewportRenderer().Render(*app, &image));
    CHECK(image.width == 160 && image.height == 120 && image.pixels[0] == 255 &&
          image.pixels[3] == 0);
    CHECK(app->ViewportRenderer().OwnsPresentation());
    const auto child = reinterpret_cast<HWND>(app->ViewportRenderer().Window());
    CHECK(GetParent(child) == parent.value && !IsWindowVisible(child));
    CHECK(SendMessageW(child, WM_NCHITTEST, 0, 0) == HTTRANSPARENT);
    CHECK(SendMessageW(child, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
    // Include both a real CPU model packet and live screen-position overlay.
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::ModelMaterialRecord material{};
    model->loadComplete = 1;
    model->vertexCount = model->indexCount = 3;
    model->materialCount = 1;
    model->materials = &material;
    model->comboSelIndex = 0;
    model->toonFlag = 1;
    material.toonReference = 255;
    material.faceVertexCount = 3;
    material.diffuse[0] = material.diffuse[3] = 1;
    material.diffuseMirror[0] = material.diffuseMirror[3] = 1;
    material.ambient[0] = .1f;
    app->ModelSlot(0) = model.get();
    const auto id = app->AllocateCpuGeometryIds();
    auto geometry = std::make_unique<ModelCpuFrame>(id, 3, 0);
    const float vertices[]{-5, 5,  0, 0, 0,  -1, 0, 0, 5, 5,  0, 0,
                           0,  -1, 1, 0, -5, -5, 0, 0, 0, -1, 0, 1};
    std::memcpy(geometry->geometry.MainData(), vertices, sizeof(vertices));
    const std::uint16_t indices[]{0, 1, 2};
    CHECK(geometry->geometry.SetIndices(IndexFormat::UInt16, indices, sizeof(indices)) &&
          geometry->geometry.Publish());
    geometry->ready = true;
    app->CpuModelFrames().emplace(model.get(), std::move(geometry));
    struct ScreenVertex {
        float x, y, z, w;
        std::uint32_t color;
    };
    const ScreenVertex line[]{{28, 24, 0, 1, 0xff00ff00}, {100, 24, 0, 1, 0xff00ff00}};
    app->ViewportRenderer().CaptureOverlay(EditorOverlay::Lines, line, 2, sizeof(ScreenVertex));
    // Prepare the same CPU font atlas as the editor, using only a never-shown
    // legacy device. Its temporary GPU texture is not a source for the RHI.
    {
        Microsoft::WRL::ComPtr<IDirect3D9> legacy;
        legacy.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        Microsoft::WRL::ComPtr<IDirect3DDevice9> device;
        D3DPRESENT_PARAMETERS desc{};
        desc.BackBufferWidth = desc.BackBufferHeight = 64;
        desc.BackBufferFormat = D3DFMT_A8R8G8B8;
        desc.Windowed = TRUE;
        desc.SwapEffect = D3DSWAPEFFECT_DISCARD;
        desc.hDeviceWindow = parent.value;
        CHECK(legacy &&
              SUCCEEDED(legacy->CreateDevice(0, D3DDEVTYPE_HAL, parent.value,
                                             D3DCREATE_SOFTWARE_VERTEXPROCESSING, &desc, &device)));
        auto wrapper = std::make_unique<D3DRenderer>();
        wrapper->device = device.Get();
        app->state.renderer = wrapper.get();
        const bool initialized = InitSceneFontTexture(app.get());
        app->state.renderer = nullptr;
        CHECK(initialized && GetSceneFontRgba(app.get()).pixels);
        app->SceneFontTexture()->Release();
        app->SceneFontTexture() = nullptr;
    }
    struct FontPixels {
        MMDApp* app;
        ~FontPixels() { ForgetSceneFontRgba(app); }
    } fontPixels{app.get()};
    struct TextVertex {
        float x, y, z, w;
        std::uint32_t color;
        float u, v;
    };
    const TextVertex text[]{
        {128, 84, 0, 1, 0xff0000ff, .5f, 0},       {160, 84, 0, 1, 0xff0000ff, .5625f, 0},
        {128, 116, 0, 1, 0xff0000ff, .5f, .0625f}, {128, 116, 0, 1, 0xff0000ff, .5f, .0625f},
        {160, 84, 0, 1, 0xff0000ff, .5625f, 0},    {160, 116, 0, 1, 0xff0000ff, .5625f, .0625f}};
    app->ViewportRenderer().CaptureOverlay(EditorOverlay::Text, text, 6, sizeof(TextVertex));
    app->ViewportRenderer().CaptureOverlay(EditorOverlay::Sprites, text, 6, sizeof(TextVertex));
    CHECK(app->ViewportRenderer().Render(*app, &image));
    const auto red = std::count_if(image.pixels.begin(), image.pixels.end(),
                                   [](auto c) { return c != 255 && c != 0; });
    CHECK(red > 100);
    const auto first = image.pixels;
    material.diffuse[0] = material.diffuseMirror[0] = material.ambient[0] = 0;
    material.diffuse[2] = material.diffuseMirror[2] = 1;
    material.ambient[2] = .1f;
    CHECK(app->ViewportRenderer().Render(*app, &image) && image.pixels != first);
    const auto blue = image.pixels;
    CHECK(app->ViewportRenderer().Render(*app, &image) && image.pixels == blue);
    for (const auto selected : {Backend::D3D11, Backend::D3D12, Backend::Vulkan}) {
        if (!BackendAvailable(selected))
            continue;
        const auto previous = app->ViewportRenderer().Selected();
        const auto oldWindow = app->ViewportRenderer().Window();
        const auto oldDevice = app->ViewportRenderer().GetDevice()->Status().identity;
        CHECK(app->ViewportRenderer().Select(*app, selected, false));
        if (previous != selected) {
            CHECK(app->ViewportRenderer().Window() != oldWindow);
            CHECK(app->ViewportRenderer().GetDevice()->Status().identity != oldDevice);
        }
        CHECK(app->ModelSlot(0) == model.get() && app->CpuModelFrames().count(model.get()));
        CHECK(app->ViewportRenderer().Render(*app, &image));
        CHECK(!IsWindowVisible(parent.value));
    }
    CHECK(app->ViewportRenderer().Select(*app, api, false) &&
          app->ViewportRenderer().Render(*app, &image));
    auto identityBefore = app->ViewportRenderer().GetDevice()->Status().identity;
    CHECK(app->ViewportRenderer().HandleCommand(*app, EditorRecreateCommand));
    CHECK(app->ViewportRenderer().GetDevice()->Status().identity != identityBefore);
    CHECK(app->ViewportRenderer().Render(*app, &image) && image.pixels == blue);
    // Both PMD and PMX read the host's current CPU material values.
    model->physicsMode = 2;
    material.flags = 1;
    CHECK(app->ViewportRenderer().Render(*app, &image));
    // Embedded axis.x is imported as CPU geometry, with the native transform
    // and separate depth clear, rather than borrowing its DX9 mesh.
    mdl::BoneRecord bone{};
    model->boneCount = 1;
    model->boneTable = &bone;
    model->selectedBone = 0;
    std::copy(identity.begin(), identity.end(), bone.matInit);
    app->SelectedModelSlot() = 0;
    app->CameraMode() = 0;
    app->ViewportToolOperation() = ViewportToolAction(9);
    CHECK(app->ViewportRenderer().Render(*app, &image));
    app->CameraMode() = 1;
    app->state.selfShadowEnabled = 1;
    app->SelfShadowMode() = 1;
    app->ShadowDistance() = .01125f;
    material.flags = 1 | 4 | 8;
    material.edgeColor[3] = 1;
    CHECK(app->ViewportRenderer().Render(*app, &image));
    app->SelfShadowMode() = 2;
    CHECK(app->ViewportRenderer().Render(*app, &image));
    app->state.selfShadowEnabled = 0;
    // Compatibility mode must show DX9 rather than a stale RHI image, then resume.
    app->WireframeRenderingEnabled() = 1;
    CHECK(!app->ViewportRenderer().Render(*app) && !app->ViewportRenderer().OwnsPresentation());
    CHECK(app->ViewportRenderer().Selected() == api &&
          app->ViewportRenderer().Reason() == "wireframe");
    app->WireframeRenderingEnabled() = 0;
    CHECK(app->ViewportRenderer().Render(*app, &image));
    const auto view = app->ViewportRect();
    app->ViewportRect().right = view.left;
    CHECK(!app->ViewportRenderer().Render(*app) && !app->ViewportRenderer().OwnsPresentation());
    app->ViewportRect() = view;
    CHECK(app->ViewportRenderer().Render(*app, &image));
    app->ViewportRect().right += 24;
    app->CpuRenderFrame().view.aspect = 184.f / 120;
    CHECK(app->ViewportRenderer().Render(*app, &image) && image.width == 184);
    identityBefore = app->ViewportRenderer().GetDevice()->Status().identity;
    app->FloatingWindow() = floating.value;
    CHECK(app->ViewportRenderer().Render(*app, &image));
    CHECK(app->ViewportRenderer().GetDevice()->Status().identity != identityBefore);
    CHECK(GetParent(reinterpret_cast<HWND>(app->ViewportRenderer().Window())) == floating.value);
    CHECK(!IsWindowVisible(parent.value) && !IsWindowVisible(floating.value));
    app->ViewportRenderer().UnloadScene();
    app->ModelSlot(0) = nullptr;
    app->CpuModelFrames().clear();
    CHECK(app->ViewportRenderer().Render(*app, &image));
    CHECK(image.pixels[0] == 255 && image.pixels[3] == 0);
    CHECK(app->ViewportRenderer().Select(*app, Backend::D3D9, false));
    CHECK(!app->ViewportRenderer().Requested() && !app->ViewportRenderer().Window() &&
          !app->ViewportRenderer().GetDevice());
    CHECK(app->ViewportRenderer().Select(*app, api, false) && app->ViewportRenderer().Render(*app));
    app->ViewportRenderer().Close();
    // Save and reload an isolated profile, without touching the user's Data directory.
    wchar_t temporary[MAX_PATH]{}, filename[MAX_PATH]{};
    CHECK(GetTempPathW(MAX_PATH, temporary) && GetTempFileNameW(temporary, L"mds", 0, filename));
    CHECK(DeleteFileW(filename) && std::filesystem::create_directory(filename));
    struct Directory {
        std::filesystem::path path;
        ~Directory() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } directory{filename};
    wcscpy_s(app->ExeDir(), std::size(app->state.exeDir), filename);
    CHECK(SetMenu(parent.value, CreateMenu()));
    app->ViewportRenderer().InstallMenu(*app);
    CHECK(app->ViewportRenderer().Select(*app, api));
    CHECK(GetMenuState(GetMenu(parent.value), EditorDX9Command, MF_BYCOMMAND) != UINT(-1));
    CHECK(GetMenuState(GetMenu(parent.value),
                       EditorDX9Command + (api == Backend::D3D11   ? 1
                                           : api == Backend::D3D12 ? 2
                                                                   : 3),
                       MF_BYCOMMAND) &
          MF_CHECKED);
    auto restored = std::make_unique<MMDApp>();
    std::memset(&restored->state, 0, sizeof(restored->state));
    restored->MainWindow() = parent.value;
    wcscpy_s(restored->ExeDir(), std::size(restored->state.exeDir), filename);
    restored->ViewportRenderer().InstallMenu(*restored);
    CHECK(restored->ViewportRenderer().Selected() == api);
    CHECK(restored->ViewportRenderer().HandleCommand(*restored, EditorDX9Command));
    auto defaults = std::make_unique<MMDApp>();
    std::memset(&defaults->state, 0, sizeof(defaults->state));
    defaults->MainWindow() = parent.value;
    wcscpy_s(defaults->ExeDir(), std::size(defaults->state.exeDir), filename);
    defaults->ViewportRenderer().InstallMenu(*defaults);
    CHECK(defaults->ViewportRenderer().Selected() == Backend::D3D9);
    app->ExeDir()[0] = 0;
    // Controller switches actual APIs while retaining CPU versions; invalid
    // options exercise a failed creation and rollback to the previous API.
    ScenePreviewController controller;
    SwapchainDesc desc;
    desc.window = {WindowSystem::Win32, reinterpret_cast<std::uintptr_t>(parent.value)};
    RECT surface{};
    CHECK(GetClientRect(parent.value, &surface));
    desc.width = unsigned(surface.right);
    desc.height = unsigned(surface.bottom);
    BackendOptions options;
    options.backend = api;
    CHECK(controller.Open(desc, options));
    CHECK(controller.Resize(0, 0) && controller.State() == PreviewState::Suspended);
    CHECK(controller.Resize(desc.width, desc.height) && controller.State() == PreviewState::Ready);
    std::unique_ptr<Window> active;
    for (auto selected : {Backend::D3D11, Backend::D3D12, Backend::Vulkan}) {
        if (!BackendAvailable(selected))
            continue;
        auto replacement = std::make_unique<Window>();
        auto next = desc;
        next.window.handle = reinterpret_cast<std::uintptr_t>(replacement->value);
        CHECK(GetClientRect(replacement->value, &surface));
        next.width = unsigned(surface.right);
        next.height = unsigned(surface.bottom);
        const auto old = controller.GetDevice()->Status().identity;
        options.backend = selected;
        if (!controller.SwitchBackend(next, options))
            throw std::runtime_error("switch: " + controller.Reason());
        CHECK(controller.Options().backend == selected &&
              controller.GetDevice()->Status().identity != old);
        auto invalid = options;
        invalid.driver = BackendDriver(99);
        CHECK(!controller.SwitchBackend(invalid) && controller.State() == PreviewState::Ready);
        CHECK(controller.Options().backend == selected);
        desc = next;
        active = std::move(replacement);
    }
    CHECK(controller.Close());
    CHECK(!IsWindowVisible(parent.value) && !IsWindowVisible(floating.value));
    std::printf(
        "PASS %s: live PMD/PMX editor data, embedded child/input, shadows, overlay, resize, floating viewport, unload, backend switching and rollback; parents never shown\n",
        mode.c_str());
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
