#include "mikudancestudio/render_model_geometry.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>

namespace mikudancestudio::render {
namespace {
bool Changed(const rhi::ModelReplayResource& resource, const std::vector<std::uint8_t>& bytes) {
    return !resource.key.version || resource.bytes != bytes;
}
void Replace(rhi::ModelReplayResource& resource, std::vector<std::uint8_t> bytes) {
    if (Changed(resource, bytes)) {
        ++resource.key.version;
        resource.bytes = std::move(bytes);
        resource.bytes.Seal();
    }
}
}
ModelGeometry::ModelGeometry(std::uint64_t firstId, std::uint32_t count, unsigned uv) {
    if (!firstId || firstId > std::numeric_limits<std::uint64_t>::max() - 2 || !count || uv > 4 ||
        std::uint64_t(count) * (32 + 16 * uv) > 0xffffffffu)
        throw std::invalid_argument("invalid CPU geometry description");
    snapshot.vertexCount = count;
    snapshot.additionalUvCount = uv;
    snapshot.main.key.id = firstId;
    snapshot.edge.key.id = firstId + 1;
    snapshot.indices.key.id = firstId + 2;
    snapshot.edge.vertexLayout = rhi::VertexLayout::PositionColor;
    snapshot.indices.kind = rhi::ReplayResourceKind::Indices;
    switch (uv) {
    case 0:
        main.emplace<0>(count);
        break;
    case 1:
        main.emplace<1>(count);
        break;
    case 2:
        main.emplace<2>(count);
        break;
    case 3:
        main.emplace<3>(count);
        break;
    case 4:
        main.emplace<4>(count);
        break;
    }
    edges.resize(count);
}
void* ModelGeometry::MainData() {
    return std::visit([](auto& v) -> void* { return v.data(); }, main);
}
const void* ModelGeometry::MainData() const {
    return std::visit([](const auto& v) -> const void* { return v.data(); }, main);
}
std::size_t ModelGeometry::MainSize() const {
    return std::size_t(snapshot.vertexCount) * (32 + 16 * snapshot.additionalUvCount);
}
bool ModelGeometry::SetIndices(rhi::IndexFormat format, const void* data, std::size_t size) {
    const unsigned stride = format == rhi::IndexFormat::UInt16   ? 2
                            : format == rhi::IndexFormat::UInt32 ? 4
                                                                 : 0;
    if (!stride || !data || !size || size % stride || size > 0xffffffffu ||
        (snapshot.indices.key.version && snapshot.indices.indexFormat != format))
        return false;
    auto* begin = static_cast<const std::uint8_t*>(data);
    for (std::size_t n = 0; n < size; n += stride) {
        std::uint32_t index = 0;
        std::memcpy(&index, begin + n, stride);
        if (index >= snapshot.vertexCount)
            return false;
    }
    std::vector<std::uint8_t> bytes(begin, begin + size);
    if (Changed(snapshot.indices, bytes) &&
        snapshot.indices.key.version == std::numeric_limits<std::uint64_t>::max())
        return false;
    snapshot.indices.indexFormat = format;
    Replace(snapshot.indices, std::move(bytes));
    return true;
}
bool ModelGeometry::Publish() {
    if (!snapshot.indices.key.version || edges.size() != snapshot.vertexCount)
        return false;
    std::vector<std::uint8_t> vertices(std::size_t(snapshot.vertexCount) * 32),
        outline(std::size_t(snapshot.vertexCount) * 16);
    const auto* source = static_cast<const std::uint8_t*>(MainData());
    for (std::size_t i = 0; i < snapshot.vertexCount; ++i) {
        std::memcpy(vertices.data() + i * 32, source + i * (32 + 16 * snapshot.additionalUvCount),
                    32);
        auto* out = outline.data() + i * 16;
        std::memcpy(out, edges[i].position, 12);
        const auto argb = edges[i].diffuse;
        out[12] = static_cast<std::uint8_t>(argb >> 16);
        out[13] = static_cast<std::uint8_t>(argb >> 8);
        out[14] = static_cast<std::uint8_t>(argb);
        out[15] = static_cast<std::uint8_t>(argb >> 24);
    }
    for (const auto* r : {&snapshot.main, &snapshot.edge})
        if (r->key.version == std::numeric_limits<std::uint64_t>::max())
            return false;
    Replace(snapshot.main, std::move(vertices));
    Replace(snapshot.edge, std::move(outline));
    return true;
}
rhi::ModelReplayResult AppendModelGeometry(rhi::ModelReplayStream& stream,
                                           const ModelGeometrySnapshot& geometry,
                                           rhi::ModelReplayDraw draw, bool edge) {
    rhi::ModelReplayResult result;
    auto fail = [&](const char* why) {
        result.reason = why;
        return result;
    };
    if (geometry.additionalUvCount)
        return fail("unsupported_additional_uv_layout");
    const auto& vertices = edge ? geometry.edge : geometry.main;
    if (!geometry.vertexCount || !vertices.key.version || !geometry.indices.key.version)
        return fail("unpublished_cpu_geometry");
    if (!draw.indexed || draw.pipeline.vertexLayout != vertices.vertexLayout)
        return fail("geometry_draw_layout_mismatch");
    if (stream.events.empty() || stream.events.front().op != rhi::ModelReplayOp::Clear)
        return fail("missing_initial_clear");
    // Check ID/version collisions before touching the destination stream.
    for (const auto* resource : {&vertices, &geometry.indices})
        for (const auto& prior : stream.resources)
            if (prior.key.id == resource->key.id &&
                (prior.key.version != resource->key.version || prior.kind != resource->kind ||
                 prior.vertexLayout != resource->vertexLayout ||
                 prior.indexFormat != resource->indexFormat || prior.bytes != resource->bytes))
                return fail("geometry_resource_collision");
    for (const auto* resource : {&vertices, &geometry.indices}) {
        bool published = false;
        for (const auto& event : stream.events)
            if (event.op == rhi::ModelReplayOp::PublishResource &&
                event.resource.id == resource->key.id &&
                event.resource.version == resource->key.version)
                published = true;
        if (!published) {
            bool present = false;
            for (const auto& prior : stream.resources)
                if (prior.key.id == resource->key.id)
                    present = true;
            if (!present)
                stream.resources.push_back(*resource);
            rhi::ModelReplayEvent event;
            event.sequence = stream.events.size() + 1;
            event.op = rhi::ModelReplayOp::PublishResource;
            event.resource = resource->key;
            stream.events.push_back(std::move(event));
        }
    }
    draw.vertices = vertices.key;
    draw.indices = geometry.indices.key;
    draw.vertexCount = geometry.vertexCount;
    rhi::ModelReplayEvent event;
    event.sequence = stream.events.size() + 1;
    event.op = rhi::ModelReplayOp::Draw;
    event.draw = std::move(draw);
    stream.events.push_back(std::move(event));
    result.success = true;
    result.reason = "cpu_geometry_appended";
    result.draws = 1;
    return result;
}
}
