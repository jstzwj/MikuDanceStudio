#include "mikudancestudio/render_model_inputs.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <algorithm>

namespace mikudancestudio::render {
rhi::ModelReplayResult ModelSubmissionBuilder::Build(
    const ModelGeometrySnapshot& geometry, const rhi::HistoryDesc& target, rhi::ClearColor clear,
    const std::vector<ModelMaterialPacket>& packets, rhi::ModelReplayStream& output) {
    const auto fail = [](const char* reason) {
        return rhi::ModelReplayResult{false, 0, 0, reason};
    };
    if (packets.empty())
        return fail("empty_material_submission");
    auto registry = textures_;
    auto next = nextId_;
    rhi::ModelReplayStream stream;
    stream.target = target;
    stream.clear = clear;
    stream.events.push_back({1, rhi::ModelReplayOp::Clear});
    std::uint64_t previousEnd = 0;
    std::uint32_t previousMaterial = 0;
    bool first = true;
    for (const auto& packet : packets) {
        if (!packet.indexCount ||
            (!first && (packet.material <= previousMaterial || packet.firstIndex < previousEnd)))
            return fail("material_order_or_range");
        first = false;
        previousMaterial = packet.material;
        previousEnd = std::uint64_t(packet.firstIndex) + packet.indexCount;
        rhi::ModelReplayDraw draw;
        draw.pipeline = packet.pipeline;
        draw.firstIndex = packet.firstIndex;
        draw.indexCount = packet.indexCount;
        for (unsigned stage = 0; stage < 3; ++stage)
            if (draw.pipeline.shader.layout.stages[stage].role != rhi::MaterialTextureRole::None) {
                const auto& asset = packet.textureResources[stage] ? *packet.textureResources[stage]
                                                                   : packet.textures[stage];
                const auto& cpu = asset.usePrepared ? asset.prepared : asset.content;
                if (asset.identity.empty() || !cpu.revision || cpu.image.pixels.empty() ||
                    asset.mips != rhi::TextureDesc::Mips::SingleLevel)
                    return fail("unavailable_cpu_texture");
                if (asset.mipChain && (!rhi::ValidatePreparedMipChain(cpu.image, *asset.mipChain) ||
                                       asset.mipChain->sourceRevision != cpu.revision))
                    return fail("invalid_cpu_mip_chain");
                const auto levels = asset.mipChain ? asset.mipChain->levels
                                                   : std::vector<rhi::RgbaImage>{cpu.image};
                const auto preparationRevision = asset.mipChain ? asset.mipChain->revision : 0;
                const bool srgb = draw.pipeline.shader.srgbSample[stage];
                auto found = std::find_if(registry.begin(), registry.end(), [&](const Texture& t) {
                    return t.identity == asset.identity && t.resource.texture.srgb == srgb;
                });
                if (found == registry.end()) {
                    if (!next || next == std::numeric_limits<std::uint64_t>::max())
                        return fail("texture_id_overflow");
                    Texture texture;
                    texture.identity = asset.identity;
                    texture.resource.kind = rhi::ReplayResourceKind::Texture;
                    texture.resource.key = {next++, 0};
                    texture.resource.texture.srgb = srgb;
                    registry.push_back(std::move(texture));
                    found = registry.end() - 1;
                }
                auto& resource = found->resource;
                bool changed = found->sourceRevision != asset.content.revision ||
                               found->preparationRevision != preparationRevision ||
                               resource.texture.levels.size() != levels.size();
                if (!changed)
                    for (std::size_t n = 0; n < levels.size(); ++n) {
                        const auto& a = resource.texture.levels[n];
                        const auto& b = levels[n];
                        if (a.width != b.width || a.height != b.height || a.pixels != b.pixels) {
                            changed = true;
                            break;
                        }
                    }
                if (changed || !found->active) {
                    // Different bytes for one logical texture inside a single model
                    // submission are a producer error, not a mid-draw update.
                    for (const auto& prior : stream.resources)
                        if (prior.key.id == resource.key.id)
                            return fail("conflicting_material_texture");
                    if (resource.key.version == std::numeric_limits<std::uint64_t>::max())
                        return fail("texture_version_overflow");
                    ++resource.key.version;
                    resource.texture.levels = levels;
                    found->preparationRevision = preparationRevision;
                    found->sourceRevision = asset.content.revision;
                }
                found->active = true;
                draw.textures[stage] = resource.key;
                const bool published =
                    std::any_of(stream.resources.begin(), stream.resources.end(),
                                [&](const auto& r) { return r.key.id == resource.key.id; });
                if (!published) {
                    stream.resources.push_back(resource);
                    rhi::ModelReplayEvent event;
                    event.sequence = stream.events.size() + 1;
                    event.op = rhi::ModelReplayOp::PublishResource;
                    event.resource = resource.key;
                    stream.events.push_back(std::move(event));
                }
            }
        auto appended = AppendModelGeometry(stream, geometry, std::move(draw));
        if (!appended)
            return appended;
        stream.events.back().label = "material:" + std::to_string(packet.material);
    }
    stream.complete = true;
    auto result = rhi::ValidateModelReplay(stream);
    if (!result)
        return result;
    for (auto& texture : registry)
        texture.active = std::any_of(
            stream.resources.begin(), stream.resources.end(),
            [&](const auto& resource) { return resource.key.id == texture.resource.key.id; });
    textures_ = std::move(registry);
    nextId_ = next;
    output = std::move(stream);
    return result;
}
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
void Normalize(std::array<float, 3>& a) {
    const float length = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (length > 0)
        for (auto& v : a)
            v /= length;
    else
        a = {};
}
void RotateX(ModelMatrix& m, float angle) {
    m = Identity();
    m[5] = m[10] = std::cos(angle);
    m[6] = std::sin(angle);
    m[9] = -m[6];
}
void LookAt(ModelMatrix& m, const std::array<float, 3>& eye) {
    m = Identity();
    m[12] = -eye[0];
    m[13] = -eye[1];
    m[14] = -eye[2];
}
void Perspective(ModelMatrix& m, float fov, float aspect, float nearPlane, float farPlane) {
    m = {};
    m[5] = 1 / std::tan(fov * .5f);
    m[0] = m[5] / aspect;
    m[10] = farPlane / (farPlane - nearPlane);
    m[11] = 1;
    m[14] = -nearPlane * m[10];
}
std::array<float, 3> Cross(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {float(double(a[1]) * b[2] - double(a[2]) * b[1]),
            float(double(a[2]) * b[0] - double(a[0]) * b[2]),
            float(double(a[0]) * b[1] - double(a[1]) * b[0])};
}
bool Toon(ModelMatrix& out, const std::array<float, 3>& light, const ModelInputMath& math) {
    const auto norm = math.normalize ? math.normalize : Normalize;
    const auto mul = math.multiply ? math.multiply : Multiply;
    auto d = light;
    for (auto& v : d)
        v = -v;
    norm(d);
    const bool pole = d[0] == 0 && d[2] == 0;
    const auto cross = math.cross ? math.cross : Cross;
    auto h = cross({0, pole ? 0.f : 1.f, pole ? -1.f : 0.f}, d);
    norm(h);
    auto v = cross(d, h);
    norm(v);
    auto basis = Identity();
    for (unsigned i = 0; i < 3; ++i) {
        basis[4 * i] = h[i];
        basis[4 * i + 1] = v[i];
        basis[4 * i + 2] = d[i];
    }
    ModelMatrix rotation;
    (math.rotateX ? math.rotateX : RotateX)(rotation, -(3.141592f * .5f));
    auto scale = Identity();
    scale[0] = .5f;
    scale[5] = -.5f;
    auto translate = Identity();
    translate[12] = translate[13] = .5f;
    ModelMatrix temporary, projected;
    mul(temporary, basis, rotation);
    mul(projected, temporary, scale);
    mul(out, projected, translate);
    return true;
}
}
bool BuildModelInputs(const ModelFrameInputs& f, const ModelMaterialInputs& m,
                      const ModelPassSettings& p, rhi::ModelPipeline& out,
                      const ModelInputMath& math) {
    out = {};
    auto& s = out.shader;
    const auto& c = f.scene.camera;
    const auto& v = f.view;
    if (!std::isfinite(v.aspect) || v.aspect <= 0 || v.nearPlane <= 0 ||
        v.farPlane <= v.nearPlane || !v.viewport.width || !v.viewport.height ||
        p.maxAnisotropy < 1 || p.maxAnisotropy > 16)
        return false;
    if (v.viewport.x > UINT32_MAX - v.viewport.width ||
        v.viewport.y > UINT32_MAX - v.viewport.height ||
        !rhi::ValidModelViewport(v.viewport, v.viewport.x + v.viewport.width,
                                 v.viewport.y + v.viewport.height))
        return false;
    auto eye = v.editorNavigation ? v.editorEye : std::array<float, 3>{0, 0, c.distance};
    if (!v.editorNavigation && c.parentModel >= 0)
        eye = c.position;
    (math.lookAt ? math.lookAt : LookAt)(s.view, eye);
    const float degrees = v.editorNavigation ? v.editorFov : c.fov;
    const auto orthographic = v.editorNavigation ? v.editorPerspective : c.perspective;
    if (orthographic) {
        if (!std::isfinite(eye[2]) || eye[2] == 0)
            return false;
        s.projection = {};
        s.projection[0] = -2 / eye[2];
        s.projection[5] = v.aspect * s.projection[0];
        s.projection[10] = .0013f;
        s.projection[15] = 1;
    } else {
        if (!std::isfinite(degrees) || degrees <= 0 || degrees >= 180)
            return false;
        const float radians = v.fovConversion == FovConversion::DoubleMultiply
                                  ? float(double(degrees) * 0.01745329238474369)
                                  : degrees * 0.01745329238474369f;
        (math.perspective ? math.perspective : Perspective)(s.projection, radians, v.aspect,
                                                            v.nearPlane, v.farPlane);
    }
    s.world = f.world;
    if (!rhi::BuildShaderNormalMatrix(s.world, s.view, s.normalView))
        return false;
    s.nativeDiffuse = s.diffuse = m.diffuse;
    s.materialAmbient = m.ambient;
    s.materialSpecular = m.specular;
    s.emissive = m.emissive;
    s.shininess = m.shininess;
    for (unsigned i = 0; i < 3; ++i) {
        s.ambient[i] = m.ambient[i];
        s.specular[i] = m.specular[i];
    }
    s.lighting = s.specularEnabled = s.localViewer = s.colorVertex = true;
    s.materialSources = {1, 0, 2, 0};
    auto& light = s.lights[0];
    light.enabled = true;
    for (unsigned i = 0; i < 3; ++i) {
        light.direction[i] = f.scene.lightState[3] * f.lightRotation[i] +
                             f.scene.lightState[4] * f.lightRotation[4 + i] +
                             f.scene.lightState[5] * f.lightRotation[8 + i];
        light.ambient[i] = light.specular[i] = f.scene.lightState[i];
        if (f.accessoryLightRoundTrip) {
            light.ambient[i] += -0.300000011920929f;
            light.ambient[i] += 0.300000011920929f;
        }
    }
    if (!rhi::BuildMaterialTextureLayout(m.pmd, m.base, m.sphere, m.sphereMode, s.layout))
        return false;
    Toon(s.textureTransforms[0], light.direction, math);
    const ModelMatrix half = {.5f, 0, 0, 0, 0, -.5f, 0, 0, 0, 0, 0, 0, .5f, .5f, 0, 1};
    for (unsigned stage = 1; stage < 3; ++stage)
        s.textureTransforms[stage] =
            s.layout.stages[stage].coordinates == rhi::MaterialCoordinates::MeshUv1 ? Identity()
                                                                                    : half;
    // Mip filtering NONE is equivalent only for a proved single-level input.
    // The host currently rejects that policy rather than relabelling a state.
    if (!p.anisotropic)
        return false;
    for (unsigned stage = 0; stage < 3; ++stage)
        s.samplers[stage] = {rhi::SamplerFilter::Anisotropic,
                             rhi::SamplerFilter::Anisotropic,
                             rhi::SamplerFilter::Anisotropic,
                             stage == 0 ? rhi::SamplerAddress::Clamp : rhi::SamplerAddress::Wrap,
                             stage == 0 ? rhi::SamplerAddress::Clamp : rhi::SamplerAddress::Wrap,
                             p.maxAnisotropy};
    s.alphaTest = p.alphaTest;
    s.alphaCompare =
        p.alphaTest ? rhi::DepthComparison::GreaterEqual : rhi::DepthComparison::Always;
    s.alphaReference = p.alphaTest ? 1 : 0;
    out.viewport = v.viewport;
    out.raster.depthTest = out.raster.depthWrite = out.raster.alphaBlend = true;
    out.raster.depthCompare = rhi::DepthComparison::LessEqual;
    out.raster.sourceBlend = rhi::BlendFactor::SourceAlpha;
    out.raster.destinationBlend = rhi::BlendFactor::InvSourceAlpha;
    out.raster.cull =
        m.diffuse[3] < 1 || m.doubleSided ? rhi::BatchCull::None : rhi::BatchCull::CounterClockwise;
    out.separateAlpha = true;
    out.alphaOperation = rhi::ModelBlendOp::Max;
    out.stencil = p.stencil;
    out.stencilReference = 1;
    out.stencilPass = rhi::ModelStencilOp::Replace;
    return rhi::ValidateMaterialShaderInputs(s);
}
std::vector<ModelInputDifference> CompareModelInputs(const rhi::ModelPipeline& a,
                                                     const rhi::ModelPipeline& b) {
    std::vector<ModelInputDifference> differences;
    const auto scalar = [&](const std::string& field, auto x, auto y, unsigned index = 0) {
        if (x != y)
            differences.push_back({field, index, double(x), double(y)});
    };
    const auto values = [&](const std::string& field, const auto& x, const auto& y) {
        for (unsigned i = 0; i < x.size(); ++i)
            scalar(field, x[i], y[i], i);
    };
    const auto& s = a.shader;
    const auto& t = b.shader;
    values("world", s.world, t.world);
    values("view", s.view, t.view);
    values("projection", s.projection, t.projection);
    values("normalView", s.normalView, t.normalView);
    values("nativeDiffuse", s.nativeDiffuse, t.nativeDiffuse);
    values("materialAmbient", s.materialAmbient, t.materialAmbient);
    values("materialSpecular", s.materialSpecular, t.materialSpecular);
    values("emissive", s.emissive, t.emissive);
    values("globalAmbient", s.globalAmbient, t.globalAmbient);
    values("diffuse", s.diffuse, t.diffuse);
    values("ambient", s.ambient, t.ambient);
    values("specular", s.specular, t.specular);
    values("materialSources", s.materialSources, t.materialSources);
#define S(field) scalar(#field, s.field, t.field)
    S(shininess);
    S(lighting);
    S(normalizeNormals);
    S(specularEnabled);
    S(localViewer);
    S(colorVertex);
    S(alphaTest);
    S(alphaCompare);
    S(alphaReference);
#undef S
    scalar("sphere", s.layout.sphere, t.layout.sphere);
    for (unsigned i = 0; i < 8; ++i) {
        const auto prefix = "light" + std::to_string(i) + ".";
        const auto& x = s.lights[i];
        const auto& y = t.lights[i];
        scalar(prefix + "enabled", x.enabled, y.enabled);
        if (x.enabled || y.enabled) {
            values(prefix + "direction", x.direction, y.direction);
            values(prefix + "diffuse", x.diffuse, y.diffuse);
            values(prefix + "ambient", x.ambient, y.ambient);
            values(prefix + "specular", x.specular, y.specular);
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        const auto prefix = "stage" + std::to_string(i) + ".";
        const auto& x = s.layout.stages[i];
        const auto& y = t.layout.stages[i];
        scalar(prefix + "role", x.role, y.role);
        if (x.role == rhi::MaterialTextureRole::None && y.role == rhi::MaterialTextureRole::None)
            continue;
#define ST(field) scalar(prefix + #field, x.field, y.field)
        ST(coordinates);
        ST(color);
        ST(colorArg1);
        ST(colorArg2);
        ST(alpha);
        ST(alphaArg1);
        ST(alphaArg2);
#undef ST
        // Mesh UVs bypass the transform in the shader contract. Native DX9
        // may retain a previous Toon matrix while transform flags are disabled.
        if (x.coordinates == rhi::MaterialCoordinates::CameraNormal ||
            y.coordinates == rhi::MaterialCoordinates::CameraNormal)
            values(prefix + "transform", s.textureTransforms[i], t.textureTransforms[i]);
        scalar(prefix + "srgb", s.srgbSample[i], t.srgbSample[i]);
        const auto& u = s.samplers[i];
        const auto& v = t.samplers[i];
#define SA(field) scalar(prefix + #field, u.field, v.field)
        SA(min);
        SA(mag);
        SA(mip);
        SA(u);
        SA(v);
        SA(maxAnisotropy);
#undef SA
    }
#define R(field) scalar("raster." #field, a.raster.field, b.raster.field)
    R(depthTest);
    R(depthWrite);
    R(depthCompare);
    R(alphaBlend);
    R(sourceBlend);
    R(destinationBlend);
    R(cull);
#undef R
#define P(field) scalar(#field, a.field, b.field)
    P(vertexLayout);
    P(topology);
    P(separateAlpha);
    P(sourceAlpha);
    P(destinationAlpha);
    P(colorOperation);
    P(alphaOperation);
    P(stencil);
    P(stencilReadMask);
    P(stencilWriteMask);
    P(colorWriteMask);
    P(stencilReference);
    P(stencilCompare);
    P(stencilFail);
    P(stencilDepthFail);
    P(stencilPass);
#undef P
    scalar("viewport.present", bool(a.viewport), bool(b.viewport));
    if (a.viewport && b.viewport) {
        const auto& x = *a.viewport;
        const auto& y = *b.viewport;
#define V(field) scalar("viewport." #field, x.field, y.field)
        V(x);
        V(y);
        V(width);
        V(height);
        V(minDepth);
        V(maxDepth);
#undef V
    }
    return differences;
}
}
