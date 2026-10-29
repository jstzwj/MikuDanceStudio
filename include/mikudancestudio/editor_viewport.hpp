#pragma once
#include "rhi_backend.hpp"
#include <memory>

namespace mikudancestudio {
namespace rhi {
struct SceneRendererStats;
}
class MMDApp;
enum class EditorOverlay {
    Text,
    Lines,
    Sprites
};
constexpr unsigned EditorDX9Command = 0xe140, EditorDX11Command = 0xe141,
                   EditorDX12Command = 0xe142, EditorVulkanCommand = 0xe143,
                   EditorRecreateCommand = 0xe144;
// Application-owned viewport. The legacy editor remains the sole input,
// animation and physics owner; this host only owns rendering and presentation.
class EditorViewport {
public:
    EditorViewport() = default;
    ~EditorViewport() = default;
    EditorViewport(const EditorViewport&) = delete;
    EditorViewport& operator=(const EditorViewport&) = delete;
    bool Requested() const;
    bool OwnsPresentation() const;
    rhi::Backend Selected() const;
    void InstallMenu(MMDApp&);
    bool HandleCommand(MMDApp&, unsigned);
    bool Select(MMDApp&, rhi::Backend, bool persist = true);
    // Frame-level compatibility check before CPU-only geometry preparation.
    // Render performs resource preparation/submission; false selects DX9 for
    // this same prepared frame and hides the previous RHI image.
    bool CanRenderFrame(MMDApp&);
    void UseLegacyFrame(MMDApp&, const char* reason);
    bool Render(MMDApp&, rhi::RgbaImage* acceptance = nullptr);
    void CaptureOverlay(EditorOverlay, const void*, std::size_t vertices, unsigned stride);
    void ClearOverlay(EditorOverlay);
    void UnloadScene();
    void Close();
    const std::string& Reason() const;
    // Non-owning observation for hidden-window lifecycle tests.
    rhi::Device* GetDevice() const;
    rhi::SceneRendererStats Statistics() const;
    std::uintptr_t Window() const;

private:
    struct Impl;
    // Type-erased destruction keeps an unused viewport independent of the
    // rendering implementation in standalone CPU/editor tests. Copies of the
    // viewport remain forbidden; Impl still owns and retires all GPU resources.
    std::shared_ptr<Impl> impl;
};
bool EditorRhiRequested();
}
