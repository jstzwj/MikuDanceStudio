#pragma once
#include "rhi_backend.hpp"
#include "rhi_scene_renderer.hpp"
namespace mikudancestudio::rhi {
enum class PreviewState {
    Closed,
    Ready,
    Suspended,
    Blocked,
    Failed
};
// Single render-thread controller. A platform host owns the actual window and
// forwards its extent; this class owns device replacement and scene lifetime.
// No legacy GPU objects, capture APIs or platform window types enter this layer.
class ScenePreviewController {
public:
    ScenePreviewController() = default;
    ~ScenePreviewController();
    ScenePreviewController(const ScenePreviewController&) = delete;
    ScenePreviewController& operator=(const ScenePreviewController&) = delete;
    bool Open(const SwapchainDesc&, const BackendOptions&);
    bool Resize(std::uint32_t width, std::uint32_t height);
    bool ReplaceDevice();
    // Rebuild GPU ownership while retaining CPU versions. On creation failure,
    // restore the previous backend and report false with a usable controller.
    bool SwitchBackend(const BackendOptions&);
    // Cross-API changes use a fresh native window so presentation ownership
    // never overlaps. Create first, then retire the old surface; failed
    // creation leaves the current renderer usable.
    bool SwitchBackend(const SwapchainDesc&, const BackendOptions&);
    bool Draw(const ModelReplayStream&, RgbaImage* acceptanceImage = nullptr);
    // Caller builds the final target at the current surface extent. Shadow
    // target dimensions and camera inputs are preserved, with no CPU readback.
    bool Draw(const RenderFrame&, RgbaImage* acceptanceImage = nullptr);
    bool Unload();
    bool Close();
    void Block(const char* reason);
    void Resume();
    PreviewState State() const { return state; }
    const std::string& Reason() const { return reason; }
    const std::string& Adapter() const { return backend.adapter; }
    Device* GetDevice() const { return backend.device.get(); }
    SceneRenderer* GetRenderer() const { return renderer.get(); }
    const BackendOptions& Options() const { return options; }

private:
    bool Fail(const char*);
    bool ReadyToDraw();
    bool SuspendUnavailableSurface();
    bool FinishDraw(RgbaImage*);
    BackendDevice backend;
    std::unique_ptr<SceneRenderer> renderer;
    PreparedModelFrame prepared;
    bool hasPrepared = false, hasScene = false;
    BackendOptions options;
    SwapchainDesc description;
    PreviewState state = PreviewState::Closed;
    std::string reason;
};
}
