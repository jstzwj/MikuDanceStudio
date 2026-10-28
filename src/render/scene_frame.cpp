#include "mikudancestudio/render_scene_frame.hpp"
#include <map>
#include <tuple>
#include <limits>

namespace mikudancestudio::render {
namespace {
using Identity =
    std::tuple<unsigned, std::uint32_t, std::uint32_t, std::uint64_t, unsigned, std::string, bool>;
Identity ResourceIdentity(const SceneDrawReplacement& draw,
                          const rhi::ModelReplayResource& resource) {
    // Model main/edge/index data share an object namespace across material,
    // shadow and outline passes. Per-material texture builders have local IDs.
    const bool texture = resource.kind == rhi::ReplayResourceKind::Texture;
    if (texture) {
        for (const auto& e : draw.cpu.events)
            if (e.op == rhi::ModelReplayOp::Draw)
                for (unsigned slot = 0; slot < 3; ++slot) {
                    if (e.draw.textures[slot].id == resource.key.id &&
                        e.draw.textures[slot].version == resource.key.version &&
                        !draw.textureIdentities[slot].empty())
                        return {0,
                                0,
                                0,
                                0,
                                unsigned(resource.kind),
                                draw.textureIdentities[slot],
                                resource.texture.srgb};
                }
    }
    const unsigned domain = texture ? 100 + unsigned(draw.family)
                            : draw.family == SceneDrawFamily::AccessoryMaterial ? 2
                            : draw.family == SceneDrawFamily::GroundLines       ? 3
                            : draw.family == SceneDrawFamily::GroundPlane       ? 4
                                                                                : 1;
    return {
        domain, draw.model, texture ? draw.material : 0, resource.key.id, unsigned(resource.kind),
        {},     false};
}
bool SameBytes(const rhi::ModelReplayResource& a, const rhi::ModelReplayResource& b) {
    if (a.kind != b.kind || a.vertexLayout != b.vertexLayout || a.indexFormat != b.indexFormat ||
        a.bytes != b.bytes || a.texture.srgb != b.texture.srgb ||
        a.texture.generateMips != b.texture.generateMips ||
        a.texture.levels.size() != b.texture.levels.size())
        return false;
    for (std::size_t n = 0; n < a.texture.levels.size(); ++n) {
        const auto& x = a.texture.levels[n];
        const auto& y = b.texture.levels[n];
        if (x.width != y.width || x.height != y.height || x.pixels != y.pixels)
            return false;
    }
    return true;
}
bool SameTarget(const rhi::HistoryDesc& a, const rhi::HistoryDesc& b) {
    return a.width == b.width && a.height == b.height && a.samples == b.samples &&
           a.quality == b.quality && a.representation == b.representation &&
           a.resolve == b.resolve && a.color == b.color && a.depth == b.depth &&
           a.stencil == b.stencil && a.samplePattern == b.samplePattern;
}
}
struct CpuSceneFrameAssembler::Impl {
    struct Entry {
        rhi::ModelReplayResource resource;
        bool active = false;
    };
    struct Reference {
        Identity identity;
        rhi::ReplayResourceKey original;
        std::shared_ptr<const rhi::ModelReplayResource> resource;
    };
    struct Fragment {
        SceneDrawReplacement draw;
        std::vector<Reference> resources;
    };
    SceneFrameDesc desc;
    bool recording = false;
    std::string blocked;
    std::vector<Fragment> draws;
    std::map<Identity, std::shared_ptr<const rhi::ModelReplayResource>> staged;
    std::map<Identity, Entry> registry;
    std::uint64_t nextId = 1;
    SceneFrameStats stats;
};
CpuSceneFrameAssembler::CpuSceneFrameAssembler() : impl(std::make_unique<Impl>()) {
}
CpuSceneFrameAssembler::~CpuSceneFrameAssembler() = default;
void CpuSceneFrameAssembler::Begin(const SceneFrameDesc& d) {
    impl->desc = d;
    impl->recording = true;
    impl->blocked.clear();
    impl->draws.clear();
    impl->staged.clear();
    if (!d.scene)
        Block("zero_scene_namespace");
}
bool CpuSceneFrameAssembler::Recording() const {
    return impl->recording && impl->blocked.empty();
}
void CpuSceneFrameAssembler::Block(const char* reason) {
    if (impl->recording && impl->blocked.empty())
        impl->blocked = reason && *reason ? reason : "unsupported_scene_operation";
}
void CpuSceneFrameAssembler::Append(SceneDrawReplacement draw) {
    if (!Recording())
        return;
    if (unsigned(draw.family) > unsigned(SceneDrawFamily::ModelOutline)) {
        Block("unsupported_scene_family");
        return;
    }
    const auto valid = rhi::ValidateModelReplay(draw.cpu);
    if (!valid || valid.draws != 1) {
        Block(!valid ? valid.reason.c_str() : "fragment_must_have_one_draw");
        return;
    }
    if (!SameTarget(draw.cpu.target, impl->desc.target) ||
        draw.cpu.source.frame != impl->desc.frame) {
        Block("fragment_target_or_frame_mismatch");
        return;
    }
    Impl::Fragment fragment;
    const rhi::ModelReplayDraw* command = nullptr;
    for (const auto& e : draw.cpu.events)
        if (e.op == rhi::ModelReplayOp::Draw)
            command = &e.draw;
    for (auto& resource : draw.cpu.resources) {
        const auto uses = [&](rhi::ReplayResourceKey key) {
            return key.id == resource.key.id && key.version == resource.key.version;
        };
        bool used = uses(command->vertices) || uses(command->indices);
        for (const auto& key : command->textures)
            used = used || uses(key);
        if (!used)
            continue;
        const auto identity = ResourceIdentity(draw, resource);
        const auto original = resource.key;
        auto found = impl->staged.find(identity);
        if (found == impl->staged.end())
            found =
                impl->staged
                    .emplace(identity,
                             std::make_shared<const rhi::ModelReplayResource>(std::move(resource)))
                    .first;
        else if (!SameBytes(*found->second, resource)) {
            Block("conflicting_cpu_resource_in_frame");
            return;
        }
        fragment.resources.push_back({identity, original, found->second});
    }
    // Command metadata stays local. Immutable resource bytes are stored once
    // per logical identity across every material/pass of the pending frame.
    draw.cpu.resources.clear();
    fragment.draw = std::move(draw);
    impl->draws.push_back(std::move(fragment));
}
rhi::ModelReplayResult CpuSceneFrameAssembler::Finish(rhi::ModelReplayStream& output) {
    if (!impl->recording)
        return {false, 0, 0, "frame_not_recording"};
    impl->recording = false;
    if (!impl->blocked.empty())
        return {false, 0, 0, impl->blocked};
    rhi::ModelReplayStream result;
    result.target = impl->desc.target;
    result.clear = impl->desc.clear;
    result.source.scene = impl->desc.scene;
    result.source.frame = impl->desc.frame;
    result.initializedRegion = impl->desc.initializedRegion;
    result.complete = true;
    result.events.push_back({1, rhi::ModelReplayOp::Clear});
    // Pending registry is committed only after the entire frame validates.
    auto registry = impl->registry;
    auto nextId = impl->nextId;
    std::map<Identity, rhi::ReplayResourceKey> published;
    std::uint64_t publications = 0;
    for (const auto& pending : impl->draws) {
        const auto& fragment = pending.draw;
        const rhi::ModelReplayDraw* draw = nullptr;
        for (const auto& e : fragment.cpu.events)
            if (e.op == rhi::ModelReplayOp::Draw)
                draw = &e.draw;
        if (!draw)
            return {false, 0, 0, "missing_fragment_draw"};
        std::map<rhi::ReplayResourceKey, rhi::ReplayResourceKey> remap;
        for (const auto& reference : pending.resources) {
            const auto& resource = *reference.resource;
            const auto& identity = reference.identity;
            auto found = registry.find(identity);
            const auto prior = published.find(identity);
            if (prior != published.end()) {
                if (!SameBytes(found->second.resource, resource))
                    return {false, 0, 0, "conflicting_cpu_resource_in_frame"};
                remap.emplace(reference.original, prior->second);
                continue;
            }
            if (found == registry.end()) {
                if (nextId == std::numeric_limits<std::uint64_t>::max())
                    return {false, 0, 0, "resource_identity_exhausted"};
                Impl::Entry entry;
                entry.resource.key = {nextId++, 0};
                found = registry.emplace(identity, std::move(entry)).first;
            }
            auto& entry = found->second;
            auto key = entry.resource.key;
            if (!entry.active || !SameBytes(entry.resource, resource)) {
                if (key.version == std::numeric_limits<std::uint64_t>::max())
                    return {false, 0, 0, "resource_version_exhausted"};
                ++key.version;
                ++publications;
            }
            entry.resource = resource;
            entry.resource.key = key;
            entry.active = true;
            published.emplace(identity, key);
            remap.emplace(reference.original, key);
            result.resources.push_back(entry.resource);
            rhi::ModelReplayEvent e;
            e.sequence = result.events.size() + 1;
            e.op = rhi::ModelReplayOp::PublishResource;
            e.resource = key;
            result.events.push_back(std::move(e));
        }
        const auto mapped = [&](rhi::ReplayResourceKey k) { return k.id ? remap.at(k) : k; };
        rhi::ModelReplayEvent e;
        e.sequence = result.events.size() + 1;
        e.op = rhi::ModelReplayOp::Draw;
        e.label = SceneDrawFamilyName(fragment.family);
        e.draw = *draw;
        e.draw.vertices = mapped(e.draw.vertices);
        e.draw.indices = mapped(e.draw.indices);
        for (auto& t : e.draw.textures)
            t = mapped(t);
        result.events.push_back(std::move(e));
    }
    const auto valid = rhi::ValidateModelReplay(result);
    if (!valid)
        return valid;
    SceneFrameStats stats;
    stats.draws = valid.draws;
    stats.resources = result.resources.size();
    stats.identityCount = registry.size();
    stats.publications = impl->stats.publications + publications;
    for (auto& pair : registry) {
        if (!published.count(pair.first)) {
            auto key = pair.second.resource.key;
            pair.second = {};
            pair.second.resource.key = key;
        } else {
            stats.residentBytes += pair.second.resource.bytes.size();
            for (const auto& level : pair.second.resource.texture.levels)
                stats.residentBytes += level.pixels.size();
        }
    }
    impl->registry = std::move(registry);
    impl->nextId = nextId;
    impl->stats = stats;
    impl->draws.clear();
    impl->staged.clear();
    output = std::move(result);
    return valid;
}
void CpuSceneFrameAssembler::Reset() {
    impl = std::make_unique<Impl>();
}
SceneFrameStats CpuSceneFrameAssembler::Stats() const {
    auto stats = impl->stats;
    for (const auto& pair : impl->staged) {
        stats.stagedResourceBytes += pair.second->bytes.size();
        for (const auto& level : pair.second->texture.levels)
            stats.stagedResourceBytes += level.pixels.size();
    }
    return stats;
}
}
