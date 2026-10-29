#pragma once
#include "rhi_backend.hpp"
#include <memory>

namespace mikudancestudio {
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
    EditorViewport();
    ~EditorViewport();
    bool Requested() const;
    bool OwnsPresentation() const;
    rhi::Backend Selected() const;
    void InstallMenu(MMDApp&);
    bool HandleCommand(MMDApp&, unsigned);
    bool Select(MMDApp&, rhi::Backend, bool persist = true);
    bool Render(MMDApp&, rhi::RgbaImage* acceptance = nullptr);
    void CaptureOverlay(EditorOverlay, const void*, std::size_t vertices, unsigned stride);
    void ClearOverlay(EditorOverlay);
    void UnloadScene();
    void Close();
    const std::string& Reason() const;
    // Non-owning observation for hidden-window lifecycle tests.
    rhi::Device* GetDevice() const;
    std::uintptr_t Window() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
bool EditorRhiRequested();
}
