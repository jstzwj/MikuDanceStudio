#include "mikudancestudio/model_cpu_preview.hpp"
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
#define NOMINMAX
#include <Windows.h>
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/model_cpu_bridge.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/scene_preview_ui.hpp"
#include "mikudancestudio/render_scene_frame.hpp"
#include "mikudancestudio/rhi_scene_preview.hpp"
#include <algorithm>
#include <fstream>
#include <string>
namespace mikudancestudio {
namespace {
bool Flag(const char* name) {
    char v[2]{};
    return GetEnvironmentVariableA(name, v, 2) == 1 && v[0] == '1';
}
unsigned& PreviewBackends() {
    static unsigned selected = []() {
        char v[16]{};
        GetEnvironmentVariableA("MDS_RHI_CPU_SCENE_PREVIEW", v, 16);
        return std::string(v) == "dx11"   ? 1u
               : std::string(v) == "dx12" ? 2u
               : std::string(v) == "both" ? 3u
                                          : 0u;
    }();
    return selected;
}
bool menuSelection = false;
render::CpuSceneFrameAssembler assembler;
rhi::ModelReplayStream frame;
bool frameReady = false, expectedDraw = false, expectedEmptyMesh = false, previewGate = false;
std::string blockedReason = "waiting_for_scene";
std::uint64_t frameSerial = 0;
void Report(const char* backend, const char* operation, const char* status, const char* reason,
            rhi::Device* device = nullptr, rhi::SceneRenderer* renderer = nullptr,
            std::size_t different = 0) {
#ifdef MIKUDANCESTUDIO_DIAG
    char directory[MAX_PATH]{};
    if (!GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, MAX_PATH))
        return;
    const auto path =
        std::string(directory) + "/cpu_resident." + std::to_string(GetCurrentProcessId()) + ".csv";
    const bool fresh = GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES;
    std::ofstream out(path, std::ios::app);
    if (fresh)
        out << "serial,frame,backend,operation,status,reason,draws,resources,cpuBytes,identities,uploads,textureUploads,resident,retiring,device,epoch,differentPixels,process\n";
    const auto a = assembler.Stats();
    const auto s = renderer ? renderer->Stats() : rhi::SceneRendererStats{};
    const auto d = device ? device->Status() : rhi::DeviceStatus{};
    out << frameSerial << "," << frame.source.frame << "," << backend << "," << operation << ","
        << status << "," << reason << "," << a.draws << "," << a.resources << "," << a.residentBytes
        << "," << a.identityCount << "," << s.uploads << "," << s.textureUploads << ","
        << s.residentResources << "," << s.retiringResources << "," << d.identity << "," << d.epoch
        << "," << different << "," << GetCurrentProcessId() << "\n";
#endif
}
struct PreviewWindow {
    HWND handle = nullptr, main = nullptr, status = nullptr;
    unsigned closeCommand = 0;
    bool closed = false, replace = false;
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<PreviewWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            if (message == WM_CLOSE) {
                self->closed = true;
                ShowWindow(window, SW_HIDE);
                PostMessageW(self->main, WM_COMMAND, self->closeCommand, 0);
                return 0;
            }
            if (message == WM_KEYDOWN && w == VK_F5)
                self->replace = true;
            if (message == WM_APP + 0x421) {
                self->replace = true;
                InvalidateRect(self->main, nullptr, FALSE);
                return 0;
            }
            if (message == WM_SIZE || message == WM_KEYDOWN)
                InvalidateRect(self->main, nullptr, FALSE);
            if (message == WM_SIZE && self->status)
                MoveWindow(self->status, 16, 16, std::max(1, int(LOWORD(l)) - 32), 100, TRUE);
            if (message == WM_PAINT) {
                PAINTSTRUCT p;
                BeginPaint(window, &p);
                EndPaint(window, &p);
                return 0;
            }
        }
        return DefWindowProcW(window, message, w, l);
    }
    bool Create(HWND owner, const wchar_t* title, unsigned width, unsigned height,
                unsigned command) {
        static const bool registered = []() {
            WNDCLASSW c{};
            c.lpfnWndProc = Procedure;
            c.hInstance = GetModuleHandleW(nullptr);
            c.lpszClassName = L"MDS_CPU_RHI_PREVIEW";
            c.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            c.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
            return RegisterClassW(&c) != 0;
        }();
        if (!registered)
            return false;
        main = owner;
        closeCommand = command;
        RECT rect{0, 0, LONG(width), LONG(height)};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        handle =
            CreateWindowExW(0, L"MDS_CPU_RHI_PREVIEW", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, owner,
                            nullptr, GetModuleHandleW(nullptr), this);
        if (handle)
            status = CreateWindowExW(0, L"STATIC", L"Waiting for a supported CPU scene frame",
                                     WS_CHILD | SS_LEFT, 16, 16, LONG(width) - 32, 100, handle,
                                     nullptr, GetModuleHandleW(nullptr), nullptr);
        return handle != nullptr;
    }
    void Status(const std::string& reason) {
        std::string message = reason;
        if (reason == "cpu_scene_frame_unavailable" || reason == "waiting_for_scene")
            message = "Load a PMM scene to start preview.";
        else if (reason == "MME_not_supported")
            message = "This preview does not support MME effects.";
        else if (reason == "pmx_live_inputs_not_validated")
            message = "PMX models are not supported by this preview yet.";
        else if (reason == "cpu_accessory_screen_source_native_only")
            message = "Dynamic screen textures are not supported yet.";
        else if (reason == "background_not_supported")
            message = "Background pictures and AVI are not supported yet.";
        else if (reason == "wireframe_not_supported")
            message = "Wireframe mode is not supported yet.";
        else if (reason == "self_shadow_composition_not_supported")
            message = "Self-shadow composition is not supported yet.";
        const auto text = "Preview paused: " + message +
                          "\r\nSingle sample / PMD / no MME. F5 retries device creation.";
        SetWindowTextA(status, text.c_str());
        ShowWindow(status, SW_SHOWNOACTIVATE);
        ShowWindow(handle, SW_SHOWNOACTIVATE);
    }
    ~PreviewWindow() {
        if (handle)
            DestroyWindow(handle);
    }
};
struct ResidentPreview {
    bool dx12 = false;
    PreviewWindow window;
    rhi::ScenePreviewController controller;
    const char* Name() const { return dx12 ? "dx12" : "dx11"; }
    rhi::Device* Device() { return controller.GetDevice(); }
    rhi::SceneRenderer* Renderer() { return controller.GetRenderer(); }
    bool Initialize(MMDApp* app, bool twelveBackend) {
        dx12 = twelveBackend;
        // Window framing is CPU application state, not a DX9 device property.
        const auto viewport = app->CpuRenderFrame().view.viewport;
        const unsigned width = std::max(64u, viewport.width),
                       height = std::max(64u, viewport.height);
        if (!window.Create(app->MainWindow(),
                           dx12 ? L"MikuDanceStudio CPU Preview DX12"
                                : L"MikuDanceStudio CPU Preview DX11",
                           width, height, dx12 ? 0xe122 : 0xe121))
            return false;
        rhi::SwapchainDesc desc;
        desc.window = {rhi::WindowSystem::Win32, reinterpret_cast<std::uintptr_t>(window.handle)};
        desc.width = width;
        desc.height = height;
        desc.present = rhi::PresentMode::Immediate;
        rhi::BackendOptions options;
        options.backend = dx12 ? rhi::Backend::D3D12 : rhi::Backend::D3D11;
        options.model = CpuDiagnosticModelOptions();
        const bool okay = controller.Open(desc, options);
        Report(Name(), "create", okay ? "PASS" : "FAIL",
               okay ? "backend_factory" : controller.Reason().c_str(), Device(), Renderer());
        if (!okay)
            window.Status(controller.Reason());
        return true; // failed window remains reviewable/retryable
    }
    bool Render(const rhi::RgbaImage* reference) {
        if (window.closed)
            return reference == nullptr;
        if (window.replace) {
            window.replace = false;
            const bool okay = controller.ReplaceDevice();
            Report(Name(), "replace", okay ? "PASS" : "FAIL",
                   okay ? "old_submission_rejected_cpu_resources_retained"
                        : controller.Reason().c_str(),
                   Device(), Renderer());
            if (!okay) {
                window.Status(controller.Reason());
                return false;
            }
        }
        if (!frameReady) {
            controller.Block(blockedReason.c_str());
            if (menuSelection)
                window.Status(blockedReason);
            else
                ShowWindow(window.handle, SW_HIDE);
            return false;
        }
        controller.Resume();
        if (!previewGate) {
            if (menuSelection)
                window.Status("awaiting_optional_native_audit");
            else
                ShowWindow(window.handle, SW_HIDE);
            return false;
        }
        if (controller.State() == rhi::PreviewState::Failed) {
            window.Status(controller.Reason());
            return false;
        }
        RECT rect{};
        if (!GetClientRect(window.handle, &rect)) {
            window.Status("invalid_client_window");
            return false;
        }
        const auto before = Device()->Status();
        const unsigned width = IsIconic(window.handle) ? 0
                               : reference             ? frame.target.width
                                                       : unsigned(std::max(0L, rect.right));
        const unsigned height = IsIconic(window.handle) ? 0
                                : reference             ? frame.target.height
                                                        : unsigned(std::max(0L, rect.bottom));
        rhi::RgbaImage image;
        if (!controller.Resize(width, height) ||
            !controller.Draw(frame, reference ? &image : nullptr)) {
            window.Status(controller.Reason());
            Report(Name(), "present", "FAIL", controller.Reason().c_str(), Device(), Renderer());
            return false;
        }
        if (controller.State() == rhi::PreviewState::Suspended) {
            Report(Name(), "suspend", "PASS", controller.Reason().c_str(), Device(), Renderer());
            return reference == nullptr;
        }
        if (Device()->Status().epoch != before.epoch)
            Report(Name(), "resize", "PASS", "old_submission_rejected_uploads_retained", Device(),
                   Renderer());
        if (reference) {
            const auto region = frame.initializedRegion.value_or(
                rhi::ModelViewport{0, 0, frame.target.width, frame.target.height, 0, 1});
            if (reference->width != region.width || reference->height != region.height ||
                image.width < region.x + region.width || image.height < region.y + region.height)
                return false;
            std::size_t difference = 0;
            for (unsigned y = 0; y < region.height; ++y)
                for (unsigned x = 0; x < region.width; ++x) {
                    const auto a = (std::size_t(y + region.y) * image.width + x + region.x) * 4,
                               b = (std::size_t(y) * region.width + x) * 4;
                    for (unsigned c = 0; c < 4; ++c)
                        if (image.pixels[a + c] != reference->pixels[b + c]) {
                            ++difference;
                            break;
                        }
                }
            Report(Name(), "image", difference ? "FAIL" : "PASS", "native_checkpoint", Device(),
                   Renderer(), difference);
            if (difference) {
                controller.Block("native_checkpoint_image_difference");
                previewGate = false;
                window.Status(controller.Reason());
                return false;
            }
        }
        ShowWindow(window.status, SW_HIDE);
        Report(Name(), "present", "PASS", "resident_cpu_frame", Device(), Renderer());
        if (!IsWindowVisible(window.handle))
            ShowWindow(window.handle, SW_SHOWNOACTIVATE);
        return true;
    }
    void Close() {
        const bool okay = controller.Close();
        Report(Name(), "close", okay ? "PASS" : "FAIL",
               okay ? "gpu_resources_released" : controller.Reason().c_str(), Device(), Renderer());
    }
    ~ResidentPreview() { Close(); }
};
std::unique_ptr<ResidentPreview> resident11, resident12;
void EnsurePreviews(MMDApp* app) {
    const auto create = [&](std::unique_ptr<ResidentPreview>& resident, bool dx12) {
        if (!resident) {
            resident = std::make_unique<ResidentPreview>();
            if (!resident->Initialize(app, dx12))
                resident.reset();
        }
    };
    if (PreviewBackends() & 1)
        create(resident11, false);
    if (PreviewBackends() & 2)
        create(resident12, true);
}
}
bool CpuScenePreviewRequested() {
    return PreviewBackends() != 0;
}
bool CpuSceneFrameRecording() {
    return assembler.Recording();
}
void BeginCpuSceneFrame(MMDApp* app, unsigned flags, unsigned color) {
    frameReady = false;
    expectedDraw = expectedEmptyMesh = false;
    if (!CpuScenePreviewRequested() && !Flag("MDS_RHI_CPU_FULL_SCENE"))
        return;
    if (!CpuScenePreviewRequested() && Flag("MDS_RHI_CPU_LIVE_HISTORY")) {
        // The native recorder is enabled only for requested checkpoints. The
        // assembler is intentionally independent and can run between them too.
        // Avoid material generation when no resident preview consumes a frame.
        if (!CpuSceneNativeCheckpointPending())
            return;
    }
    render::SceneFrameDesc desc;
    const auto& pp = app->Renderer()->presentParameters;
    desc.target.width = pp.BackBufferWidth;
    desc.target.height = pp.BackBufferHeight;
    desc.frame = app->CpuRenderFrame().scene.frame;
    desc.initializedRegion = app->CpuRenderFrame().view.viewport;
    desc.clear = {((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f,
                  (color & 255) / 255.f, ((color >> 24) & 255) / 255.f};
    assembler.Begin(desc);
    ++frameSerial;
    if (!app->CpuRenderFrameReady())
        assembler.Block("cpu_scene_frame_unavailable");
    if (!menuSelection && ((flags & 7) != 7 || pp.MultiSampleType != D3DMULTISAMPLE_NONE))
        assembler.Block("single_sample_color_depth_stencil_required");
    if (mme::EffectsEnabled() && !Flag("MDS_RHI_MODEL_NO_MME"))
        assembler.Block("MME_not_supported");
    if (app->PictureBackgroundEnabled() || app->AviBackgroundEnabled())
        assembler.Block("background_not_supported");
    if (app->WireframeRenderingEnabled())
        assembler.Block("wireframe_not_supported");
    if (app->SelfShadowCompositionEnabled() && app->SelfShadowMode() > 0)
        assembler.Block("self_shadow_composition_not_supported");
    if (app->DepthTextureCompositionEnabled() && app->DepthDeviceEnabled() &&
        app->DepthTextureCallback())
        assembler.Block("depth_callback_composition_not_supported");
}
void AppendCpuSceneFrame(render::SceneDrawReplacement draw) {
    if (!assembler.Recording())
        return;
    if (expectedDraw || expectedEmptyMesh) {
        assembler.Block("cpu_draw_without_submission");
        return;
    }
    assembler.Append(std::move(draw));
    expectedDraw = assembler.Recording();
}
void ObserveCpuSceneDraw(const char* pass) {
    if (!assembler.Recording())
        return;
    if (!expectedDraw) {
        const auto reason = std::string("draw_without_cpu_producer_") + (pass ? pass : "unknown");
        assembler.Block(reason.c_str());
    }
    expectedDraw = false;
}
void ExpectCpuSceneEmptyMesh() {
    if (assembler.Recording()) {
        if (expectedDraw || expectedEmptyMesh)
            assembler.Block("cpu_draw_without_submission");
        else
            expectedEmptyMesh = true;
    }
}
bool ConsumeCpuSceneEmptyMesh() {
    const bool empty = expectedEmptyMesh;
    expectedEmptyMesh = false;
    return empty;
}
void BlockCpuSceneFrame(const char* reason) {
    assembler.Block(reason);
}
void FinishCpuSceneFrame(MMDApp*) {
    if (expectedDraw || expectedEmptyMesh)
        assembler.Block("cpu_draw_without_submission");
    expectedDraw = expectedEmptyMesh = false;
    const auto result = assembler.Finish(frame);
    frameReady = bool(result);
    if (!frameReady)
        blockedReason = result.reason;
    if (!frameReady && result.reason != "frame_not_recording")
        Report("cpu", "assemble", "BLOCKED", result.reason.c_str());
}
bool CopyAssembledCpuSceneFrame(rhi::ModelReplayStream& output) {
    if (!frameReady)
        return false;
    output = frame;
    return true;
}
void PresentCpuSceneFrame(MMDApp* app) {
    if (!CpuScenePreviewRequested())
        return;
    if (menuSelection || (frameReady && !Flag("MDS_RHI_CPU_INPUTS_AUDIT")))
        EnsurePreviews(app);
    if (menuSelection || !Flag("MDS_RHI_CPU_INPUTS_AUDIT"))
        previewGate = true;
    if (resident11)
        resident11->Render(nullptr);
    if (resident12)
        resident12->Render(nullptr);
}
bool ValidateResidentCpuSceneFrame(MMDApp* app, const rhi::RgbaImage& native) {
    if (!CpuScenePreviewRequested())
        return true;
    if (!frameReady)
        return false;
    previewGate = true;
    bool passed = true;
    const auto run = [&](std::unique_ptr<ResidentPreview>& resident, bool dx12) {
        if (!resident) {
            resident = std::make_unique<ResidentPreview>();
            if (!resident->Initialize(app, dx12)) {
                Report(dx12 ? "dx12" : "dx11", "create", "FAIL", "backend_creation");
                resident.reset();
                return false;
            }
        }
        return resident->Render(&native);
    };
    if (PreviewBackends() & 1)
        passed = run(resident11, false) && passed;
    if (PreviewBackends() & 2)
        passed = run(resident12, true) && passed;
    previewGate = passed;
    return passed;
}
void ReleaseCpuSceneFrame() {
    resident11.reset();
    resident12.reset();
    assembler.Reset();
    frame = {};
    frameReady = expectedDraw = expectedEmptyMesh = previewGate = false;
}
void InvalidateCpuScenePreviewGate() {
    previewGate = false;
}

void InstallScenePreviewMenu(MMDApp* app) {
    auto menu = GetMenu(app->MainWindow());
    if (!menu)
        return;
    if (GetMenuState(menu, PreviewDX11Command, MF_BYCOMMAND) == UINT(-1)) {
        auto popup = CreatePopupMenu();
        if (!popup)
            return;
        AppendMenuW(popup, MF_STRING | (rhi::BackendAvailable(rhi::Backend::D3D11) ? 0 : MF_GRAYED),
                    PreviewDX11Command, L"Direct3D 11");
        AppendMenuW(popup, MF_STRING | (rhi::BackendAvailable(rhi::Backend::D3D12) ? 0 : MF_GRAYED),
                    PreviewDX12Command, L"Direct3D 12");
        AppendMenuW(popup, MF_STRING, PreviewCloseCommand, L"Close previews");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), L"Renderer Preview");
    }
    CheckMenuItem(menu, PreviewDX11Command,
                  MF_BYCOMMAND | ((PreviewBackends() & 1) ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, PreviewDX12Command,
                  MF_BYCOMMAND | ((PreviewBackends() & 2) ? MF_CHECKED : MF_UNCHECKED));
    DrawMenuBar(app->MainWindow());
}
bool HandleScenePreviewCommand(MMDApp* app, unsigned command) {
    auto next = PreviewBackends();
    if (command == PreviewDX11Command)
        next = 1;
    else if (command == PreviewDX12Command)
        next = 2;
    else if (command == PreviewCloseCommand)
        next = 0;
    else if (command == 0xe121)
        next &= ~1u;
    else if (command == 0xe122)
        next &= ~2u;
#ifdef MIKUDANCESTUDIO_DIAG
    else if (command == PreviewBothCommand)
        next = 3; // harness only; no product menu
#endif
    else
        return false;
    if ((next & 1) && !rhi::BackendAvailable(rhi::Backend::D3D11))
        return true;
    if ((next & 2) && !rhi::BackendAvailable(rhi::Backend::D3D12))
        return true;
    PreviewBackends() = next;
    menuSelection = true;
    if (!(next & 1))
        resident11.reset();
    if (!(next & 2))
        resident12.reset();
    if (!next) {
        assembler.Reset();
        frame = {};
        frameReady = expectedDraw = expectedEmptyMesh = previewGate = false;
        if (!Flag("MDS_RHI_CPU_GEOMETRY"))
            app->CpuModelFrames().clear();
    }
    if (next) {
        for (unsigned slot = 0; slot < kModelSlotCount; ++slot) {
            auto* model = app->ModelSlot(slot);
            if (model && !app->CpuModelFrames().count(model))
                InitializeModelCpuGeometry(app, model);
        }
        EnsurePreviews(app);
    }
    InstallScenePreviewMenu(app);
    InvalidateRect(app->MainWindow(), nullptr, FALSE);
    return true;
}
}
#endif
