#include "mikudancestudio/editor_viewport.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/mmd_app.hpp"
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
#include "mikudancestudio/editor_scene.hpp"
#include "mikudancestudio/model_cpu_bridge.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/rhi_scene_preview.hpp"
#include "mme_host_api.h"
#include <algorithm>
#include <filesystem>

namespace mikudancestudio {
namespace {
using namespace rhi;
const wchar_t* Name(Backend api) {
    switch (api) {
    case Backend::D3D11:
        return L"Direct3D 11";
    case Backend::D3D12:
        return L"Direct3D 12";
    case Backend::Vulkan:
        return L"Vulkan";
    default:
        return L"Direct3D 9";
    }
}
const wchar_t* Setting(Backend api) {
    switch (api) {
    case Backend::D3D11:
        return L"dx11";
    case Backend::D3D12:
        return L"dx12";
    case Backend::Vulkan:
        return L"vulkan";
    default:
        return L"dx9";
    }
}
Backend Parse(const wchar_t* text) {
    if (!_wcsicmp(text, L"dx11"))
        return Backend::D3D11;
    if (!_wcsicmp(text, L"dx12"))
        return Backend::D3D12;
    if (!_wcsicmp(text, L"vulkan"))
        return Backend::Vulkan;
    return Backend::D3D9;
}
constexpr unsigned StatusCommand = 0xe145;
std::filesystem::path SettingsPath(MMDApp& app) {
    return std::filesystem::path(app.ExeDir()) / L"Data" / L"renderer.ini";
}
const char* CompatibilityReason(MMDApp& app) {
    if (app.FullscreenMode() || app.RecordingWindow() || app.AviStereoOutput())
        return "recording_or_fullscreen";
    if (app.PictureBackgroundEnabled() || app.AviBackgroundEnabled())
        return "background";
    if (app.WireframeRenderingEnabled())
        return "wireframe";
    if (app.state.stereoActivated)
        return "stereo";
    if (app.state.rigidBodyDisplayEnabled || app.FrameCopyOrPhysicsDialog())
        return "physics_debug";
    if (app.DepthTextureCompositionEnabled() && app.DepthDeviceEnabled() &&
        app.DepthTextureCallback())
        return "depth_camera";
    if (MmeHostHasAssignedEffects())
        return "MME";
    return nullptr;
}
const wchar_t* FriendlyReason(const std::string& reason) {
    if (reason == "MME")
        return L"MME effects";
    if (reason == "background")
        return L"background picture / AVI";
    if (reason == "wireframe")
        return L"wireframe display";
    if (reason == "stereo")
        return L"stereo display";
    if (reason == "physics_debug")
        return L"physics debug display";
    if (reason == "depth_camera")
        return L"depth camera";
    if (reason == "recording_or_fullscreen")
        return L"recording / fullscreen";
    if (reason == "accessory_shadows")
        return L"accessory shadows";
    if (reason == "model_display_override")
        return L"model display override";
    if (reason == "native_diagnostic")
        return L"native rendering diagnostic";
    if (reason == "live_model_geometry_unavailable" || reason == "live_frame_unavailable")
        return L"waiting for scene data";
    return L"renderer unavailable; use Recreate device to retry";
}
}
struct EditorViewport::Impl {
    Backend selected = Backend::D3D9;
    ScenePreviewController controller;
    EditorSceneProducer producer;
    HWND window = nullptr;
    bool loadedSettings = false, presenting = false;
    unsigned width = 0, height = 0;
    std::string reason;
    std::wstring menuStatus;
    ~Impl() { Close(); }
    void Close() {
        presenting = false;
        controller.Close();
        if (window && IsWindow(window))
            DestroyWindow(window);
        window = nullptr;
        width = height = 0;
        reason.clear();
    }
    void Hide(const std::string& why) {
        presenting = false;
        // DX9 will capture its own frames during fallback. Start fresh on
        // return instead of sampling the last RHI frame from before fallback.
        producer.ResetHistory();
        reason = why;
        if (window && IsWindow(window))
            ShowWindow(window, SW_HIDE);
    }
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        if (message == WM_NCHITTEST)
            return HTTRANSPARENT;
        if (message == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (message == WM_ERASEBKGND)
            return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
    bool EnsureWindow(MMDApp& app) {
        const auto parent = app.FloatingWindow() ? app.FloatingWindow() : app.MainWindow();
        const auto view = app.ViewportRect();
        RECT client{};
        if (!parent || !GetClientRect(parent, &client))
            return false;
        const int left = std::max(0L, view.left), top = std::max(0L, view.top);
        const int right = std::min(client.right, view.right),
                  bottom = std::min(client.bottom, view.bottom);
        if (right <= left || bottom <= top || IsIconic(parent)) {
            Hide("window_suspended");
            return true;
        }
        if (right - left > 4096 || bottom - top > 4096) {
            Hide("viewport_extent");
            return false;
        }
        if (window && (!IsWindow(window) || GetParent(window) != parent))
            Close();
        if (!window) {
            static const bool registered = [] {
                WNDCLASSW c{};
                c.lpfnWndProc = Procedure;
                c.hInstance = GetModuleHandleW(nullptr);
                c.lpszClassName = L"MDS_EDITOR_RHI_VIEWPORT";
                return RegisterClassW(&c) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
            }();
            if (!registered)
                return false;
            window =
                CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_NOPARENTNOTIFY, L"MDS_EDITOR_RHI_VIEWPORT",
                                L"", WS_CHILD | WS_CLIPSIBLINGS, left, top, right - left,
                                bottom - top, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!window)
                return false;
        }
        if (reason == "window_suspended")
            reason.clear();
        width = unsigned(right - left);
        height = unsigned(bottom - top);
        SetWindowPos(window, HWND_BOTTOM, left, top, int(width), int(height), SWP_NOACTIVATE);
        return true;
    }
    SwapchainDesc Description() const {
        SwapchainDesc desc;
        desc.window = {WindowSystem::Win32, reinterpret_cast<std::uintptr_t>(window)};
        desc.width = width;
        desc.height = height;
        desc.present = PresentMode::Immediate;
        return desc;
    }
    void Menu(MMDApp& app) {
        auto menu = GetMenu(app.MainWindow());
        if (!menu)
            return;
        if (GetMenuState(menu, EditorDX9Command, MF_BYCOMMAND) == UINT(-1)) {
            auto popup = CreatePopupMenu();
            for (unsigned n = 0; n < 4; ++n) {
                const Backend backend[]{Backend::D3D9, Backend::D3D11, Backend::D3D12,
                                        Backend::Vulkan};
                AppendMenuW(popup,
                            MF_STRING | ((n == 0 || BackendAvailable(backend[n])) ? 0 : MF_GRAYED),
                            EditorDX9Command + n, Name(backend[n]));
            }
            AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(popup, MF_STRING, EditorRecreateCommand, L"Recreate device");
            AppendMenuW(popup, MF_STRING | MF_GRAYED, StatusCommand, L"Using Direct3D 9");
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), L"Renderer");
            menuStatus.clear();
        }
        const unsigned chosen = selected == Backend::D3D11    ? EditorDX11Command
                                : selected == Backend::D3D12  ? EditorDX12Command
                                : selected == Backend::Vulkan ? EditorVulkanCommand
                                                              : EditorDX9Command;
        CheckMenuRadioItem(menu, EditorDX9Command, EditorVulkanCommand, chosen, MF_BYCOMMAND);
        EnableMenuItem(menu, EditorRecreateCommand,
                       MF_BYCOMMAND | (selected == Backend::D3D9 ? MF_GRAYED : MF_ENABLED));
        std::wstring status = L"Using ";
        status += presenting ? Name(selected) : Name(Backend::D3D9);
        if (!reason.empty() && reason != "window_suspended")
            status += L": " + std::wstring(FriendlyReason(reason));
        if (status != menuStatus) {
            ModifyMenuW(menu, StatusCommand, MF_BYCOMMAND | MF_STRING | MF_GRAYED, StatusCommand,
                        status.c_str());
            menuStatus = status;
            DrawMenuBar(app.MainWindow());
        }
    }
};
bool EditorViewport::Requested() const {
    return impl && impl->selected != Backend::D3D9;
}
bool EditorViewport::OwnsPresentation() const {
    return impl && impl->presenting;
}
Backend EditorViewport::Selected() const {
    return impl ? impl->selected : Backend::D3D9;
}
const std::string& EditorViewport::Reason() const {
    static const std::string empty;
    return impl ? impl->reason : empty;
}
Device* EditorViewport::GetDevice() const {
    return impl ? impl->controller.GetDevice() : nullptr;
}
rhi::SceneRendererStats EditorViewport::Statistics() const {
    const auto* renderer = impl ? impl->controller.GetRenderer() : nullptr;
    return renderer ? renderer->Stats() : rhi::SceneRendererStats{};
}
std::uintptr_t EditorViewport::Window() const {
    return impl ? reinterpret_cast<std::uintptr_t>(impl->window) : 0;
}
void EditorViewport::InstallMenu(MMDApp& app) {
    if (!impl)
        impl = std::make_shared<Impl>();
    if (!impl->loadedSettings && app.ExeDir()[0]) {
        impl->loadedSettings = true;
        wchar_t value[32]{};
        GetPrivateProfileStringW(L"Renderer", L"Backend", L"dx9", value, 32,
                                 SettingsPath(app).c_str());
        const auto selected = Parse(value);
        if (selected == Backend::D3D9 || BackendAvailable(selected)) {
            impl->selected = selected;
            if (Requested())
                for (unsigned n = 0; n < kModelSlotCount; ++n)
                    if (auto* model = app.ModelSlot(n); model && !app.CpuModelFrames().count(model))
                        InitializeModelCpuGeometry(&app, model, true);
        }
    }
    impl->Menu(app);
}
bool EditorViewport::Select(MMDApp& app, Backend selected, bool persist) {
    if (!impl)
        impl = std::make_shared<Impl>();
    impl->loadedSettings = true;
    if (selected != Backend::D3D9 && !BackendAvailable(selected)) {
        impl->reason = "backend_not_built";
        impl->Menu(app);
        return false;
    }
    if (selected == Backend::D3D9) {
        impl->Close();
        impl->producer.Reset();
    } else if (impl->controller.GetDevice() && impl->selected != selected) {
        auto options = impl->controller.Options();
        options.backend = selected;
        const auto previous = impl->window;
        impl->window = nullptr;
        if (!impl->EnsureWindow(app)) {
            impl->window = previous;
            impl->reason = "viewport_window_failed";
            impl->Menu(app);
            return false;
        }
        if (!impl->controller.SwitchBackend(impl->Description(), options)) {
            DestroyWindow(impl->window);
            impl->window = previous;
            impl->reason = impl->controller.Reason();
            if (impl->controller.State() == PreviewState::Failed)
                impl->Hide(impl->reason);
            impl->Menu(app);
            return false;
        }
        if (previous && IsWindow(previous))
            DestroyWindow(previous);
        impl->presenting = false;
        // Cross-frame images belong to the old GPU, while immutable CPU assets survive.
        impl->producer.ResetHistory();
    }
    impl->selected = selected;
    impl->reason.clear();
    if (impl->controller.State() == PreviewState::Failed && !impl->controller.GetDevice())
        impl->controller.Close();
    if (Requested())
        for (unsigned n = 0; n < kModelSlotCount; ++n)
            if (auto* model = app.ModelSlot(n); model && !app.CpuModelFrames().count(model))
                InitializeModelCpuGeometry(&app, model, true);
    if (persist && app.ExeDir()[0]) {
        const auto path = SettingsPath(app);
        std::error_code ignored;
        std::filesystem::create_directories(path.parent_path(), ignored);
        WritePrivateProfileStringW(L"Renderer", L"Backend", Setting(selected), path.c_str());
    }
    impl->Menu(app);
    app.MessageSeen() = 1;
    InvalidateRect(app.MainWindow(), &app.ViewportRect(), FALSE);
    return true;
}
bool EditorViewport::HandleCommand(MMDApp& app, unsigned command) {
    if (command >= EditorDX9Command && command <= EditorVulkanCommand) {
        const Backend backend[]{Backend::D3D9, Backend::D3D11, Backend::D3D12, Backend::Vulkan};
        Select(app, backend[command - EditorDX9Command]);
        return true;
    }
    if (command != EditorRecreateCommand)
        return false;
    if (Requested()) {
        if (impl->controller.GetDevice()) {
            const auto identity = impl->controller.GetDevice()->Status().identity;
            if (!impl->controller.ReplaceDevice())
                impl->Hide(impl->controller.Reason());
            else
                impl->reason.clear();
            // Failed creation can restore the old configuration on a new
            // device too; its cross-frame images must be reset in either case.
            if (impl->controller.GetDevice() &&
                impl->controller.GetDevice()->Status().identity != identity)
                impl->producer.ResetHistory();
        } else
            impl->controller.Close();
        app.MessageSeen() = 1;
        InvalidateRect(app.MainWindow(), &app.ViewportRect(), FALSE);
    }
    return true;
}
void EditorViewport::UseLegacyFrame(MMDApp& app, const char* reason) {
    if (impl) {
        impl->Hide(reason ? reason : "");
        impl->Menu(app);
    }
}
bool EditorViewport::CanRenderFrame(MMDApp& app) {
    if (!Requested())
        return false;
    if (const auto why = CompatibilityReason(app)) {
        UseLegacyFrame(app, why);
        return false;
    }
    return true;
}
bool EditorViewport::Render(MMDApp& app, RgbaImage* acceptance) {
    if (!CanRenderFrame(app))
        return false;
    const auto finish = [&](const std::string& why) {
        impl->Hide(why);
        impl->Menu(app);
        return false;
    };
    if (!impl->EnsureWindow(app))
        return finish("viewport_window_failed");
    if (impl->reason == "window_suspended")
        return false;
    if (impl->controller.State() == PreviewState::Failed)
        return finish(impl->controller.Reason());
    if (!impl->controller.GetDevice()) {
        BackendOptions options;
        options.backend = impl->selected;
        if (!impl->controller.Open(impl->Description(), options))
            return finish(impl->controller.Reason());
        impl->producer.ResetHistory();
    }
    if (impl->controller.State() == PreviewState::Failed)
        return finish(impl->controller.Reason());
    impl->controller.Resume();
    // A surface may resume at the same extent after temporary unavailability.
    if (!impl->controller.Resize(impl->width, impl->height))
        return finish(impl->controller.Reason());
    RenderFrame frame;
    std::string reason;
    if (!impl->producer.Build(app, impl->width, impl->height, frame, reason))
        return finish(reason);
    if (!impl->controller.Draw(frame, acceptance))
        return finish(impl->controller.Reason());
    if (impl->controller.State() != PreviewState::Ready)
        return finish("window_suspended");
    impl->producer.Commit();
    impl->presenting = true;
    impl->reason.clear();
    ShowWindow(impl->window, SW_SHOWNOACTIVATE);
    impl->Menu(app);
    return true;
}
void EditorViewport::CaptureOverlay(EditorOverlay kind, const void* data, std::size_t count,
                                    unsigned stride) {
    if (Requested())
        impl->producer.Capture(kind, data, count, stride);
}
void EditorViewport::ClearOverlay(EditorOverlay kind) {
    if (impl)
        impl->producer.Clear(kind);
}
void EditorViewport::UnloadScene() {
    if (!impl)
        return;
    impl->Hide("live_frame_unavailable");
    impl->controller.Unload();
    impl->producer.Reset();
}
void EditorViewport::Close() {
    if (impl) {
        impl->Close();
        impl->producer.Reset();
    }
}
bool EditorRhiRequested() {
    return g_Block && g_Block->ViewportRenderer().Requested();
}
}
#else
namespace mikudancestudio {
struct EditorViewport::Impl {};
bool EditorViewport::Requested() const {
    return false;
}
bool EditorViewport::OwnsPresentation() const {
    return false;
}
rhi::Backend EditorViewport::Selected() const {
    return rhi::Backend::D3D9;
}
void EditorViewport::InstallMenu(MMDApp&) {
}
bool EditorViewport::HandleCommand(MMDApp&, unsigned) {
    return false;
}
bool EditorViewport::Select(MMDApp&, rhi::Backend, bool) {
    return false;
}
bool EditorViewport::Render(MMDApp&, rhi::RgbaImage*) {
    return false;
}
bool EditorViewport::CanRenderFrame(MMDApp&) {
    return false;
}
void EditorViewport::UseLegacyFrame(MMDApp&, const char*) {
}
void EditorViewport::CaptureOverlay(EditorOverlay, const void*, std::size_t, unsigned) {
}
void EditorViewport::ClearOverlay(EditorOverlay) {
}
void EditorViewport::UnloadScene() {
}
void EditorViewport::Close() {
}
const std::string& EditorViewport::Reason() const {
    static const std::string empty;
    return empty;
}
rhi::Device* EditorViewport::GetDevice() const {
    return nullptr;
}
rhi::SceneRendererStats EditorViewport::Statistics() const {
    return {};
}
std::uintptr_t EditorViewport::Window() const {
    return 0;
}
bool EditorRhiRequested() {
    return false;
}
}
#endif
