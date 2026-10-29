#pragma once
#include "rhi_model_replay.hpp"
#include "rhi_render_pass.hpp"
namespace mikudancestudio::rhi {
struct FrameTarget {
    std::uint64_t id = 0;
    RenderTextureDesc description;
    // Explicit cross-frame RGBA8 history. Initialized to this color after
    // allocation/reset/replacement; ordinary targets remain frame-local.
    std::optional<ClearColor> history;
};
struct FrameAttachment {
    std::uint64_t target = 0;
    AttachmentLoad load = AttachmentLoad::Clear;
    AttachmentStore store = AttachmentStore::Store;
    ClearColor clear;
    float clearDepth = 1;
    std::uint8_t clearStencil = 0;
};
struct FramePass {
    std::string name;
    std::optional<FrameAttachment> color, depthStencil;
    std::vector<std::uint64_t> sampled;
    std::vector<ModelReplayDraw> draws;
    std::vector<FrameAttachment> colors;
};
// One CPU snapshot per immutable resource ID. Target IDs are a separate scene
// namespace and name mutable render products, never immutable upload versions.
struct RenderFrame {
    HistoryIdentity source;
    std::vector<FrameTarget> targets;
    std::vector<ModelReplayResource> resources;
    std::vector<FramePass> passes;
    // Optional final RGBA8 render product. Copied to the selected output after
    // all passes; no CPU readback or implicit scaling. Zero preserves legacy use.
    std::uint64_t finalColor = 0;
};
ModelReplayResult ValidateRenderFrame(const RenderFrame&);
// Internal transport for the existing resident resource preparation path.
// Pass dimensions are installed as explicit draw viewports in the transport.
ModelReplayStream FlattenRenderFrame(const RenderFrame&);
}
