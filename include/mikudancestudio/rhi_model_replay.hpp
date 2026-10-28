#pragma once
#include "rhi_model_pipeline.hpp"
#include <string>
namespace mikudancestudio::rhi {
// CPU-owned snapshots, never native handles. A version identifies immutable
// bytes within one stream. Publish events make versions visible in draw order.
struct ReplayResourceKey {
    std::uint64_t id = 0, version = 0;
    bool operator<(const ReplayResourceKey& b) const {
        return id < b.id || (id == b.id && version < b.version);
    }
};
enum class ReplayResourceKind {
    Vertices,
    Indices,
    Texture
};
struct ModelReplayResource {
    VertexLayout vertexLayout = VertexLayout::PositionNormalUv;
    ReplayResourceKey key;
    ReplayResourceKind kind = ReplayResourceKind::Vertices;
    IndexFormat indexFormat = IndexFormat::UInt16;
    std::vector<std::uint8_t> bytes;
    ModelTextureData texture;
};
struct ModelReplayDraw {
    ModelPipeline pipeline;
    ReplayResourceKey vertices, indices;
    std::array<ReplayResourceKey, 3> textures{};
    std::uint32_t firstIndex = 0, indexCount = 0, vertexCount = 0;
    // Indexed: vertexCount bounds every index; nonindexed range fields zero.
    // Nonindexed: no index key/range/vertexCount; firstVertex + drawVertexCount
    // address the published vertex snapshot. Counts are vertices, not primitives.
    bool indexed = true;
    std::uint32_t firstVertex = 0, drawVertexCount = 0;
};
enum class ModelReplayOp {
    Clear,
    PublishResource,
    Draw,
    Unsupported
};
struct ModelReplayEvent {
    std::uint64_t sequence = 0;
    ModelReplayOp op = ModelReplayOp::Unsupported;
    ReplayResourceKey resource;
    ModelReplayDraw draw;
    std::string label;
};
struct ModelReplayStream {
    // A clear-only scene has exactly one event and no resources.
    // RGBA8 + D24S8, clear depth=1/stencil=0, one target.
    HistoryDesc target;
    HistoryIdentity source;
    ClearColor clear;
    // Absent: all target pixels known after clear. Native DX9 Clear can cover
    // only its viewport: then pixels outside this rectangle are unspecified.
    // Replay returns a full-size image; compare only this explicitly known area.
    // Every draw viewport must stay inside it; prior contents are not inferred.
    std::optional<ModelViewport> initializedRegion;
    bool complete = false;
    std::vector<ModelReplayResource> resources;
    std::vector<ModelReplayEvent> events;
};
struct ModelReplayResult {
    bool success = false;
    std::uint64_t sequence = 0;
    std::size_t draws = 0;
    std::string reason;
    explicit operator bool() const { return success; }
};
// Validates the entire stream, including events after the requested boundary.
// Does not mutate the device or the stream. No file-format/ABI serialization.
ModelReplayResult ValidateModelReplay(const ModelReplayStream&);
// Append CPU draws to a complete preceding draw history, retaining its single
// initial clear and known region. Suffix resource IDs get a disjoint namespace;
// immutable versions and publish/draw order remain unchanged. Scene/frame and
// target must agree. Failure leaves output untouched, including aliasing inputs.
ModelReplayResult ComposeModelReplay(const ModelReplayStream& prefix,
                                     const ModelReplayStream& suffix, ModelReplayStream& output);
}
