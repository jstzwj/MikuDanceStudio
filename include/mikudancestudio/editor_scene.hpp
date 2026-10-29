#pragma once
#include "editor_viewport.hpp"
#include "rhi_frame_passes.hpp"
namespace mikudancestudio {
// Live CPU records, including final editor IK/physics results. No replay of a
// saved project and no native buffer locks or render-target readback.
class EditorSceneProducer {
public:
    EditorSceneProducer();
    ~EditorSceneProducer();
    bool Build(MMDApp&, unsigned width, unsigned height, rhi::RenderFrame&, std::string&);
    void Capture(EditorOverlay, const void*, std::size_t vertices, unsigned stride);
    void Clear(EditorOverlay);
    void Commit();
    void ResetHistory();
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
