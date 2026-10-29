#pragma once
#include "rhi_backend.hpp"
#include <cstdint>

namespace mikudancestudio {
class MMDApp;
// Explicit acceptance overload; the normal driver never requests readback.
void RenderFrameScene(MMDApp*, rhi::RgbaImage* acceptance);
// Per-application cumulative counters at the real scene envelope. CPU updates
// are counted once, independently of whether the RHI attempt needs fallback.
struct FrameSceneStats {
    std::uint64_t preparations = 0, geometryUpdates = 0;
    std::uint64_t rhiFrames = 0, rhiFallbacks = 0;
    std::uint64_t legacyScenes = 0, legacyPasses = 0, fallbackGeometryUploads = 0;
    rhi::Backend lastBackend = rhi::Backend::D3D9;
    bool rendered = false;
};
}
