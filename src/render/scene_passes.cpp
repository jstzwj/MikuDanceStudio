#include "mikudancestudio/render_scene_passes.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <map>
#include <cstdio>

namespace mikudancestudio::render {
namespace {
ModelMatrix Identity() {
    ModelMatrix m{};
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}
void Multiply(ModelMatrix& out, const ModelMatrix& a, const ModelMatrix& b) {
    ModelMatrix m{};
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned k = 0; k < 4; ++k)
                m[4 * r + c] += a[4 * r + k] * b[4 * k + c];
    out = m;
}
rhi::ModelPipeline Active(rhi::ModelPipeline p) {
    auto& s = p.shader;
    // Convenience material summaries are not shader constants; the native
    // snapshot transports only the distinct fixed-function material records.
    s.diffuse = {};
    s.ambient = {};
    s.specular = {};
    if (!s.lighting) {
        s.nativeDiffuse = {};
        s.materialAmbient = {};
        s.materialSpecular = {};
        s.emissive = {};
        s.globalAmbient = {};
        s.shininess = 0;
        s.lights = {};
        s.normalizeNormals = s.specularEnabled = s.localViewer = s.colorVertex = false;
        s.materialSources = {};
    }
    if (p.vertexLayout == rhi::VertexLayout::PositionColor) {
        s.normalView = Identity();
        s.normalizeNormals = s.localViewer = false;
    }
    if (!s.alphaTest) {
        s.alphaCompare = rhi::DepthComparison::Always;
        s.alphaReference = 0;
    }
    if (!p.stencil) {
        p.stencilReference = 0;
        p.stencilReadMask = p.stencilWriteMask = 255;
        p.stencilCompare = rhi::DepthComparison::Always;
        p.stencilFail = p.stencilDepthFail = p.stencilPass = rhi::ModelStencilOp::Keep;
    }
    return p;
}
bool SameResource(const rhi::ModelReplayResource& a, const rhi::ModelReplayResource& b) {
    if (a.kind != b.kind || a.vertexLayout != b.vertexLayout || a.indexFormat != b.indexFormat ||
        a.bytes != b.bytes || a.texture.srgb != b.texture.srgb ||
        a.texture.generateMips != b.texture.generateMips ||
        a.texture.levels.size() != b.texture.levels.size())
        return false;
    for (std::size_t i = 0; i < a.texture.levels.size(); ++i) {
        const auto& x = a.texture.levels[i];
        const auto& y = b.texture.levels[i];
        if (x.width != y.width || x.height != y.height || x.pixels != y.pixels)
            return false;
    }
    return true;
}
}
const char* SceneDrawFamilyName(SceneDrawFamily f) {
    switch (f) {
    case SceneDrawFamily::GroundLines:
        return "ground_lines";
    case SceneDrawFamily::GroundPlane:
        return "ground_plane";
    case SceneDrawFamily::ProjectedModelShadow:
        return "projected_model_shadow";
    case SceneDrawFamily::ModelMaterial:
        return "model_material";
    case SceneDrawFamily::AccessoryMaterial:
        return "accessory_material";
    case SceneDrawFamily::ModelOutline:
        return "model_outline";
    default:
        return "unsupported";
    }
}
bool BuildScenePassInputs(const ModelFrameInputs& frame, SceneDrawFamily family,
                          const ScenePassSettings& settings, rhi::ModelPipeline& output,
                          const ModelInputMath& math) {
    if (family == SceneDrawFamily::ModelMaterial || family == SceneDrawFamily::AccessoryMaterial)
        return false;
    // Reuse the CPU camera/light producer, then declare this pass explicitly.
    ModelFrameInputs input = frame;
    input.accessoryLightRoundTrip = family == SceneDrawFamily::ProjectedModelShadow;
    rhi::ModelPipeline p;
    ModelPassSettings pass;
    pass.stencil = settings.stencil;
    pass.alphaTest = settings.alphaTest;
    pass.maxAnisotropy = settings.maxAnisotropy;
    if (!BuildModelInputs(input, settings.shadowMaterial, pass, p, math))
        return false;
    p.vertexLayout = rhi::VertexLayout::PositionColor;
    p.shader.normalView = Identity();
    p.shader.layout = {};
    p.shader.normalizeNormals = false;
    p.raster.cull = rhi::BatchCull::CounterClockwise;
    p.shader.alphaTest = family == SceneDrawFamily::GroundPlane ? false : settings.alphaTest;
    p.shader.alphaCompare =
        p.shader.alphaTest ? rhi::DepthComparison::GreaterEqual : rhi::DepthComparison::Always;
    p.shader.alphaReference = p.shader.alphaTest ? 1 : 0;
    p.stencil = settings.stencil && family != SceneDrawFamily::GroundPlane;
    p.raster.alphaBlend = false;
    p.shader.lighting = false;
    if (family == SceneDrawFamily::GroundLines) {
        p.topology = rhi::PrimitiveTopology::LineList;
        p.raster.alphaBlend = frame.groundAlphaBlend;
    } else if (family == SceneDrawFamily::GroundPlane)
        p.raster.alphaBlend = frame.groundAlphaBlend;
    else if (family == SceneDrawFamily::ProjectedModelShadow) {
        // Projection is defined in scene coordinates, before camera/light
        // rotation. The shader light uses the separately rotated direction.
        const std::array<float, 3> light{frame.scene.lightState[3], frame.scene.lightState[4],
                                         frame.scene.lightState[5]};
        if (!(light[1] < 0))
            return false;
        ModelMatrix projection{};
        projection[0] = projection[10] = projection[15] = 1;
        projection[4] = -light[0] / light[1];
        projection[6] = -light[2] / light[1];
        projection[13] = .10000000149011612f;
        (math.multiply ? math.multiply : Multiply)(p.shader.world, projection, frame.world);
        p.shader.lighting = true;
        p.raster.alphaBlend = settings.shadowBlend;
        p.raster.cull = rhi::BatchCull::None;
        p.raster.depthCompare = rhi::DepthComparison::Less;
        p.stencilCompare = rhi::DepthComparison::Greater;
        p.stencilReference = 2;
        auto& stage = p.shader.layout.stages[0];
        stage.role = rhi::MaterialTextureRole::Toon;
        stage.coordinates = rhi::MaterialCoordinates::MeshUv0;
        stage.color = rhi::MaterialCombine::Modulate;
        stage.colorArg2 = rhi::MaterialColorSource::LitDiffuse;
        p.shader.textureTransforms[0] = Identity();
        p.shader.samplers[0].u = p.shader.samplers[0].v = rhi::SamplerAddress::Wrap;
    } else if (family == SceneDrawFamily::ModelOutline) {
        p.raster.cull = rhi::BatchCull::Clockwise;
        p.raster.alphaBlend = true;
        p.raster.depthCompare = rhi::DepthComparison::Less;
    } else if (family != SceneDrawFamily::GroundPlane)
        return false;
    output = std::move(p);
    return true;
}
bool BuildAccessoryPassInputs(const ModelFrameInputs& frame, const AccessoryPassInputs& input,
                              const ScenePassSettings& settings, rhi::ModelPipeline& output,
                              const ModelInputMath& math) {
    if (!std::isfinite(input.opacity) || input.opacity < 0 || input.opacity > 1 ||
        (input.textureType == 3 &&
         (!input.unboundScreen || input.primaryTexture || input.sphereTexture)) ||
        input.textureType > 5)
        return false;
    auto material = input.material;
    material.base.clear();
    material.sphere.clear();
    material.diffuse[3] *= input.opacity;
    material.ambient = input.material.ambient;
    ModelPassSettings pass;
    pass.stencil = settings.stencil;
    pass.alphaTest = settings.alphaTest;
    pass.maxAnisotropy = settings.maxAnisotropy;
    auto f = frame;
    f.accessoryLightRoundTrip = input.afterModelPass;
    rhi::ModelPipeline p;
    if (!BuildModelInputs(f, material, pass, p, math))
        return false;
    auto& s = p.shader;
    s.layout = {};
    s.normalizeNormals = true;
    p.raster.cull = rhi::BatchCull::CounterClockwise;
    p.raster.alphaBlend = true;
    p.raster.destinationBlend =
        input.additive ? rhi::BlendFactor::One : rhi::BlendFactor::InvSourceAlpha;
    for (unsigned c = 0; c < 3; ++c) {
        s.lights[0].diffuse[c] = 10;
        s.lights[0].ambient[c] -= .300000011920929f;
    }
    const auto stage = [&](unsigned index, bool sphere, bool add) {
        auto& t = s.layout.stages[index];
        t.role = sphere ? rhi::MaterialTextureRole::Sphere : rhi::MaterialTextureRole::Base;
        t.coordinates =
            sphere ? rhi::MaterialCoordinates::CameraNormal : rhi::MaterialCoordinates::MeshUv0;
        t.color = add ? rhi::MaterialCombine::Add : rhi::MaterialCombine::Modulate;
        t.colorArg2 =
            index ? rhi::MaterialColorSource::Previous : rhi::MaterialColorSource::LitDiffuse;
        s.textureTransforms[index] = Identity();
        if (sphere) {
            auto& m = s.textureTransforms[index];
            m[0] = .5f;
            m[5] = -.5f;
            m[10] = 0;
            m[12] = m[13] = .5f;
            s.layout.sphere = add ? rhi::SphereOperation::Add : rhi::SphereOperation::Multiply;
        }
        s.samplers[index].u = s.samplers[index].v = rhi::SamplerAddress::Wrap;
    };
    if (input.primaryTexture)
        stage(0, input.textureType == 1 || input.textureType == 2, input.textureType == 2);
    if (input.primaryTexture && input.sphereTexture &&
        (input.textureType == 4 || input.textureType == 5))
        stage(1, true, input.textureType == 5);
    output = std::move(p);
    return true;
}
bool BuildGroundDraw(const ModelFrameInputs& frame, SceneDrawFamily family,
                     const ScenePassSettings& settings, const rhi::HistoryDesc& target,
                     rhi::ClearColor clear, rhi::ModelReplayStream& output,
                     const ModelInputMath& math) {
    if (family != SceneDrawFamily::GroundLines && family != SceneDrawFamily::GroundPlane)
        return false;
    rhi::ModelReplayStream stream;
    stream.target = target;
    stream.clear = clear;
    stream.source.frame = frame.scene.frame;
    stream.events.push_back({1, rhi::ModelReplayOp::Clear});
    rhi::ModelReplayDraw draw;
    if (!BuildScenePassInputs(frame, family, settings, draw.pipeline, math))
        return false;
    std::vector<ModelEdgeVertex> vertices;
    constexpr std::uint32_t blue = 0xff0000ff, red = 0xffff0000, green = 0xff00ff00,
                            gray = 0xffb4b4b4, white = 0x00ffffff;
    if (family == SceneDrawFamily::GroundLines) {
        vertices = {{{0, .1f, 0}, blue},  {{0, .1f, -65}, blue}, {{0, .1f, 0}, red},
                    {{65, .1f, 0}, red},  {{0, .1f, 0}, green},  {{0, 65, 0}, green},
                    {{0, .1f, 0}, gray},  {{0, .1f, 50}, gray},  {{0, .1f, 0}, gray},
                    {{-50, .1f, 0}, gray}};
        for (unsigned t = 1; t <= 10; ++t) {
            const float x = float(t) * 5, n = float(t) * -5;
            for (const auto& v : std::vector<ModelEdgeVertex>{{{x, .1f, -50}, gray},
                                                              {{x, .1f, 50}, gray},
                                                              {{n, .1f, -50}, gray},
                                                              {{n, .1f, 50}, gray},
                                                              {{-50, .1f, x}, gray},
                                                              {{50, .1f, x}, gray},
                                                              {{-50, .1f, n}, gray},
                                                              {{50, .1f, n}, gray}})
                vertices.push_back(v);
        }
    } else
        vertices = {{{-50, 0, -50}, white}, {{-50, 0, 50}, white}, {{50, 0, -50}, white},
                    {{50, 0, -50}, white},  {{-50, 0, 50}, white}, {{50, 0, 50}, white}};
    rhi::ModelReplayResource vb;
    vb.key = {1, 1};
    vb.vertexLayout = rhi::VertexLayout::PositionColor;
    vb.bytes.resize(vertices.size() * 16);
    std::memcpy(vb.bytes.data(), vertices.data(), vb.bytes.size());
    for (std::size_t at = 0; at < vb.bytes.size(); at += 16)
        std::swap(vb.bytes[at + 12], vb.bytes[at + 14]);
    draw.vertices = vb.key;
    stream.resources.push_back(vb);
    stream.events.push_back({2, rhi::ModelReplayOp::PublishResource, vb.key});
    if (family == SceneDrawFamily::GroundLines) {
        rhi::ModelReplayResource ib;
        ib.key = {2, 1};
        ib.kind = rhi::ReplayResourceKind::Indices;
        for (unsigned i = 0; i < 90; ++i) {
            ib.bytes.push_back(std::uint8_t(i));
            ib.bytes.push_back(0);
        }
        stream.resources.push_back(ib);
        stream.events.push_back({3, rhi::ModelReplayOp::PublishResource, ib.key});
        draw.indices = ib.key;
        draw.vertexCount = draw.indexCount = 90;
    } else {
        draw.indexed = false;
        draw.drawVertexCount = 6;
    }
    rhi::ModelReplayEvent event;
    event.sequence = stream.events.size() + 1;
    event.op = rhi::ModelReplayOp::Draw;
    event.draw = draw;
    event.label = SceneDrawFamilyName(family);
    stream.events.push_back(event);
    stream.complete = true;
    if (!rhi::ValidateModelReplay(stream))
        return false;
    output = std::move(stream);
    return true;
}
rhi::ModelReplayResult ReplaceSceneDraws(const rhi::ModelReplayStream& baseline,
                                         const std::vector<SceneDrawReplacement>& replacements,
                                         rhi::ModelReplayStream& output) {
    auto valid = rhi::ValidateModelReplay(baseline);
    if (!valid)
        return valid;
    const auto fail = [](std::size_t boundary, const std::string& why) {
        return rhi::ModelReplayResult{false, boundary, 0, why};
    };
    auto candidate = baseline;
    candidate.events.clear();
    std::uint64_t next = 1;
    for (const auto& r : baseline.resources) {
        if (r.key.id >= std::numeric_limits<std::uint64_t>::max() - 1)
            return fail(0, "scene_resource_id_overflow");
        next = std::max(next, r.key.id + 1);
    }
    std::size_t boundary = 0, used = 0;
    for (auto event : baseline.events) {
        if (event.op == rhi::ModelReplayOp::Draw) {
            ++boundary;
            if (used < replacements.size() && replacements[used].boundary < boundary)
                return fail(boundary, "scene_replacement_order");
            if (used < replacements.size() && replacements[used].boundary == boundary) {
                const auto& replacement = replacements[used++];
                const auto& cpu = replacement.cpu;
                const auto check = rhi::ValidateModelReplay(cpu);
                if (!check || check.draws != 1)
                    return fail(boundary, "invalid_cpu_scene_draw");
                if (cpu.source.frame != baseline.source.frame ||
                    (cpu.source.scene && baseline.source.scene &&
                     cpu.source.scene != baseline.source.scene))
                    return fail(boundary, "scene_frame_mismatch");
                if (cpu.target.width != baseline.target.width ||
                    cpu.target.height != baseline.target.height ||
                    cpu.target.samples != baseline.target.samples ||
                    cpu.target.quality != baseline.target.quality ||
                    cpu.target.representation != baseline.target.representation ||
                    cpu.target.resolve != baseline.target.resolve ||
                    cpu.target.color != baseline.target.color ||
                    cpu.target.depth != baseline.target.depth ||
                    cpu.target.stencil != baseline.target.stencil ||
                    cpu.target.samplePattern != baseline.target.samplePattern)
                    return fail(boundary, "scene_target_mismatch");
                const auto& draw =
                    std::find_if(cpu.events.begin(), cpu.events.end(), [](const auto& e) {
                        return e.op == rhi::ModelReplayOp::Draw;
                    })->draw;
                const auto differences =
                    CompareModelInputs(Active(draw.pipeline), Active(event.draw.pipeline));
                if (!differences.empty()) {
                    const auto& d = differences[0];
                    char values[96]{};
                    std::snprintf(values, sizeof(values), "_generated=%.17g_reference=%.17g",
                                  d.generated, d.reference);
                    return fail(boundary, "scene_field_" + d.field + "_" +
                                              std::to_string(d.component) + values);
                }
                if (draw.indexed != event.draw.indexed ||
                    draw.firstIndex != event.draw.firstIndex ||
                    draw.indexCount != event.draw.indexCount ||
                    draw.vertexCount != event.draw.vertexCount ||
                    draw.firstVertex != event.draw.firstVertex ||
                    draw.drawVertexCount != event.draw.drawVertexCount)
                    return fail(boundary, "scene_draw_range");
                const auto resource =
                    [](const auto& stream,
                       rhi::ReplayResourceKey key) -> const rhi::ModelReplayResource* {
                    for (const auto& r : stream.resources)
                        if (r.key.id == key.id && r.key.version == key.version)
                            return &r;
                    return nullptr;
                };
                const auto equal = [&](rhi::ReplayResourceKey a, rhi::ReplayResourceKey b) {
                    if (!a.id || !b.id)
                        return !a.id && !b.id;
                    const auto* x = resource(cpu, a);
                    const auto* y = resource(baseline, b);
                    return x && y && SameResource(*x, *y);
                };
                if (!equal(draw.vertices, event.draw.vertices) ||
                    !equal(draw.indices, event.draw.indices))
                    return fail(boundary, "scene_geometry_bytes");
                for (unsigned s = 0; s < 3; ++s)
                    if (!equal(draw.textures[s], event.draw.textures[s]))
                        return fail(boundary, "scene_texture_bytes_" + std::to_string(s));
                std::map<rhi::ReplayResourceKey, rhi::ReplayResourceKey> keys;
                for (auto r : cpu.resources) {
                    if (next == std::numeric_limits<std::uint64_t>::max())
                        return fail(boundary, "scene_resource_id_overflow");
                    const auto original = r.key;
                    r.key = {next++, r.key.version};
                    keys.emplace(original, r.key);
                    candidate.resources.push_back(std::move(r));
                }
                for (auto e : cpu.events) {
                    if (e.op == rhi::ModelReplayOp::Clear)
                        continue;
                    if (e.op == rhi::ModelReplayOp::PublishResource)
                        e.resource = keys.at(e.resource);
                    else if (e.op == rhi::ModelReplayOp::Draw) {
                        e.draw.vertices = keys.at(e.draw.vertices);
                        if (e.draw.indices.id)
                            e.draw.indices = keys.at(e.draw.indices);
                        for (auto& t : e.draw.textures)
                            if (t.id)
                                t = keys.at(t);
                        e.label = SceneDrawFamilyName(replacement.family);
                    }
                    e.sequence = candidate.events.size() + 1;
                    candidate.events.push_back(std::move(e));
                }
                continue;
            }
        }
        event.sequence = candidate.events.size() + 1;
        candidate.events.push_back(std::move(event));
    }
    if (used != replacements.size())
        return fail(boundary, "missing_scene_replacement_boundary");
    valid = rhi::ValidateModelReplay(candidate);
    if (!valid)
        return valid;
    output = std::move(candidate);
    return valid;
}
}
