#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/render_scene_passes.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <stdexcept>
namespace mikudancestudio::render {
namespace {
using namespace rhi;
using namespace cpu_math;
bool Fail(std::string& reason, const std::string& why) {
    reason = why;
    return false;
}
std::filesystem::path Path(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return std::filesystem::u8path(value);
}
std::filesystem::path TexturePath(const format::PmxDocument& doc, int index) {
    if (index < 0)
        return {};
    if (std::size_t(index) >= doc.textures.size())
        throw std::runtime_error("texture_index_range");
    std::string value;
    const auto& encoded = doc.textures[index];
    if (!text::DecodeUtf16Le(encoded.data(), encoded.size(), value) ||
        value.find('\0') != std::string::npos)
        throw std::runtime_error("texture_path_encoding");
    return Path(value);
}
template <std::size_t N> std::string Cp932(const char (&value)[N]) {
    std::string result;
    const auto end = std::find(value, value + N, '\0');
    if (!text::DecodeCp932(std::string_view(value, std::size_t(end - value)), result))
        throw std::runtime_error("texture_path_encoding");
    return result;
}
Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float Dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
ModelInputMath ShadowMath() {
    ModelInputMath math;
    math.multiply = [](Matrix& out, const Matrix& a, const Matrix& b) { out = Multiply(a, b); };
    math.normalize = [](Vec3& v) {
        Vec3 out{};
        Normalize(v, out);
        v = out;
    };
    math.lookAtTarget = [](Matrix& out, const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 z{target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]}, x{};
        if (!Normalize(z, z) || !Normalize(Cross(up, z), x)) {
            out = {};
            return;
        }
        const auto y = Cross(z, x);
        out = {x[0], y[0], z[0], 0, x[1],         y[1],         z[1],         0,
               x[2], y[2], z[2], 0, -Dot(x, eye), -Dot(y, eye), -Dot(z, eye), 1};
    };
    return math;
}
bool Camera(const runtime::SceneRuntime& scene, ModelFrameInputs& f, std::string& reason) {
    f.world = f.lightRotation = Identity();
    f.scene.camera.distance = -45;
    f.scene.camera.fov = 45;
    f.scene.camera.parentModel = -1;
    f.scene.lightState = {.6f, .6f, .6f, -.5f, -1, -.5f};
    if (const auto* session = scene.Session()) {
        const auto& doc = session->Document();
        f.scene.frame = session->Pose().frame;
        f.scene.camera = session->Pose().camera;
        f.scene.lightState = session->Pose().lightState;
        auto& c = f.scene.camera;
        if (doc.envelope.header.version == format::PmmVersion::V1 && c.parentModel >= 0)
            return Fail(reason, "v1_camera_parent_requires_explicit_import");
        Vec3 focus = c.position;
        Matrix attachment = Identity();
        if (c.parentModel >= 0) {
            if (!scene.BoneAttachment(c.parentModel, c.parentBone, attachment, reason))
                return false;
            std::copy_n(attachment.begin() + 12, 3, focus.begin());
            if (!(doc.appearance.followCamera || doc.timeline.cameraReferenceMode) ||
                doc.appearance.suppressCameraTransform)
                attachment = Identity();
            attachment[12] = attachment[13] = attachment[14] = 0;
        }
        if (!doc.appearance.followCamera && !doc.timeline.cameraReferenceMode &&
            std::any_of(doc.appearance.cameraAttachmentBasis.begin(),
                        doc.appearance.cameraAttachmentBasis.end(), [](float v) { return v != 0; }))
            attachment = doc.appearance.cameraAttachmentBasis;
        auto rotation = Multiply(Multiply(RotationY(c.rotation[1]), RotationX(c.rotation[0])),
                                 RotationZ(c.rotation[2]));
        f.lightRotation = Multiply(attachment, rotation);
        f.world = Multiply(Translation({-focus[0], -focus[1], -focus[2]}), f.lightRotation);
        // The shared shader producer uses attached camera position as its eye.
        // Native compatibility uses exactly the same explicit input convention.
    } else if (const auto* snapshot = scene.Snapshot(0))
        f.scene.frame = snapshot->frame;
    return true;
}
struct Material {
    std::filesystem::path base, sphere;
    std::array<std::shared_ptr<const MaterialTextureAsset>, 3> textures;
    std::array<float, 4> toonColor{1, 1, 1, 1};
};
ModelMaterialInputs Inputs(const format::MaterialData& src, bool pmd, const Material& saved) {
    ModelMaterialInputs m;
    std::copy_n(src.diffuse, 4, m.diffuse.begin());
    m.ambient = m.diffuse;
    std::copy_n(src.specular, 3, m.specular.begin());
    std::copy_n(src.ambient, 3, m.emissive.begin());
    m.shininess = src.specularPower;
    m.pmd = pmd;
    m.doubleSided = !pmd && (src.flags & 1);
    m.base = saved.base;
    m.sphere = saved.sphere;
    m.sphereMode = src.sphereMode;
    return m;
}
bool Texture(const std::filesystem::path& directory, std::filesystem::path& reference,
             std::shared_ptr<const MaterialTextureAsset>& output, std::string& reason) {
    if (reference.empty())
        return true;
    MaterialTextureAsset asset;
    if (LoadMaterialTexture(directory, reference, asset) != MaterialResourceStatus::Ready)
        return Fail(reason, "texture_missing_or_unsupported:" + reference.generic_u8string());
    if (!PrepareMaterialMipChain(asset, MipPreparationPolicy::BoxLinear))
        return Fail(reason, "texture_mip_preparation");
    output = std::make_shared<const MaterialTextureAsset>(std::move(asset));
    return true;
}
void InstallShadow(ModelPipeline& p, const BuiltinShadowState& shared,
                   const ModelMaterialInputs& material, const std::array<float, 4>& toon,
                   bool receive) {
    p.shadow = shared;
    p.shadow.worldViewProjection =
        Multiply(Multiply(p.shader.world, p.shader.view), p.shader.projection);
    p.shadow.toonColor = toon;
    p.shadow.receive = receive;
    const auto& light = p.shader.lights[0];
    for (unsigned c = 0; c < 3; ++c) {
        p.shadow.color[c] = material.ambient[c] * light.ambient[c] + material.emissive[c];
        p.shadow.specular[c] = material.specular[c] * light.specular[c];
    }
    p.shadow.color[3] = material.diffuse[3];
    p.shadow.specular[3] = material.shininess == 0 ? .1f : material.shininess;
}
}
struct SceneFrameProducer::Impl {
    struct Model {
        std::shared_ptr<const runtime::ModelResource> resource;
        std::unique_ptr<ModelGeometry> geometry;
        ModelSubmissionBuilder submission;
        std::vector<Material> materials;
        explicit Model(std::uint64_t id) : submission(id + 16) {}
    };
    struct Accessory {
        std::shared_ptr<const runtime::AccessoryResource> resource;
        std::unique_ptr<ModelGeometry> geometry;
        ModelSubmissionBuilder submission;
        std::vector<Material> materials;
        explicit Accessory(std::uint64_t id) : submission(id + 16) {}
    };
    std::map<std::uint8_t, std::unique_ptr<Model>> models;
    std::map<std::uint8_t, std::unique_ptr<Accessory>> accessories;
};
SceneFrameProducer::SceneFrameProducer() = default;
SceneFrameProducer::~SceneFrameProducer() = default;
bool SceneFrameProducer::Load(const runtime::SceneRuntime& scene, const SceneRenderSources& sources,
                              const AssetSource& builtins, std::string& reason) try {
    auto next = std::make_unique<Impl>();
    ToonTextureResolver toons(sources.sharedToons, builtins);
    for (unsigned slot = 0; slot < 256; ++slot)
        if (auto resource = scene.Resource(std::uint8_t(slot))) {
            const auto found = sources.models.find(std::uint8_t(slot));
            if (found == sources.models.end())
                return Fail(reason, "model_source_path_missing");
            if (nextId_ > UINT64_MAX - 0x100000000ull)
                return Fail(reason, "scene_resource_id_exhausted");
            const auto id = nextId_;
            nextId_ += 0x100000000ull;
            auto model = std::make_unique<Impl::Model>(id);
            model->resource = resource;
            const auto& doc = resource->Document();
            const bool pmd = resource->SourceKind() == format::ModelKind::Pmd;
        if (doc.vertices.empty()) {
            next->models.emplace(std::uint8_t(slot),std::move(model));
            continue;
        }
            model->geometry = std::make_unique<ModelGeometry>(id, unsigned(doc.vertices.size()));
            if (!model->geometry->SetIndices(IndexFormat::UInt32, doc.indices.data(),
                                             doc.indices.size() * sizeof(std::int32_t)))
                return Fail(reason, "model_indices");
            for (const auto& record : doc.materials) {
                const auto& m = record.fields;
                Material saved;
                if (pmd) {
                    auto texture = Cp932(m.pmdTexture);
                    const auto split = texture.find('*');
                    saved.base = Path(texture.substr(0, split));
                    if (split != std::string::npos)
                        saved.sphere = Path(texture.substr(split + 1));
                } else {
                    saved.base = TexturePath(doc, m.textureIndex);
                    saved.sphere = TexturePath(doc, m.sphereIndex);
                }
                const auto directory = found->second.parent_path();
                ToonRequest request;
                request.modelDirectory = directory;
                request.missing = pmd ? MissingToonPolicy::Unbound : MissingToonPolicy::Default;
                if (pmd && m.toonReference != 255) {
                    if (m.toonReference >= 10)
                        return Fail(reason, "pmd_toon_slot");
                    const auto& name = resource->PmdToonNames()[m.toonReference];
                    std::string decoded;
                    if (!text::DecodeCp932(
                            std::string_view(name.data(),
                                             std::find(name.begin(), name.end(), 0) - name.begin()),
                            decoded))
                        return Fail(reason, "pmd_toon_encoding");
                    request.kind = ToonKind::ModelFile;
                    request.path = Path(decoded);
                    for (unsigned n = 1; n <= 10; ++n) {
                        char expected[20];
                        std::snprintf(expected, sizeof expected, "toon%02u.bmp", n);
                        if (decoded == expected) {
                            request.kind = ToonKind::Shared;
                            request.sharedSlot = n;
                            break;
                        }
                    }
                } else if (!pmd && m.sharedToon) {
                    request.kind = ToonKind::Shared;
                    request.sharedSlot = unsigned(m.toonReference) + 1;
                } else if (!pmd && m.toonIndex >= 0) {
                    request.kind = ToonKind::ModelFile;
                    request.path = TexturePath(doc, m.toonIndex);
                }
                MaterialTextureAsset toon;
                if (!toons.Load(request, toon))
                    return Fail(reason, "toon_missing_or_unsupported");
                if (request.kind == ToonKind::Shared && toon.origin == TextureOrigin::Builtin)
                    for (unsigned c = 0; c < 3; ++c)
                        saved.toonColor[c] =
                            DefaultToonShadowColors[3 * (request.sharedSlot - 1) + c];
                else if (request.kind != ToonKind::Default && !toon.fallback) {
                    const auto& image = toon.content.image;
                    const auto at = std::size_t(image.height - 1) * image.width * 4;
                    for (unsigned c = 0; c < 3; ++c)
                        saved.toonColor[c] = image.pixels[at + c] * (1.f / 256);
                }
                saved.textures[0] = std::make_shared<const MaterialTextureAsset>(std::move(toon));
                if (!Texture(directory, saved.base, saved.textures[1], reason) ||
                    !Texture(directory, saved.sphere, saved.textures[2], reason))
                    return false;
                model->materials.push_back(std::move(saved));
            }
            next->models.emplace(std::uint8_t(slot), std::move(model));
        }
    if (const auto* session = scene.Session())
        for (const auto& item : session->Document().accessories) {
            auto resource = scene.Accessory(item.slot);
            const auto source = sources.accessories.find(item.slot);
            if (!resource || source == sources.accessories.end())
                return Fail(reason, "accessory_source_missing");
            const auto id = nextId_;
            nextId_ += 0x100000000ull;
            auto accessory = std::make_unique<Impl::Accessory>(id);
            accessory->resource = resource;
            accessory->geometry =
                std::make_unique<ModelGeometry>(id, unsigned(resource->Vertices().size()));
            if (!accessory->geometry->SetIndices(IndexFormat::UInt32, resource->Indices().data(),
                                                 resource->Indices().size() * 4))
                return Fail(reason, "accessory_indices");
            auto* vertices = static_cast<ModelVertex*>(accessory->geometry->MainData());
            for (std::size_t n = 0; n < resource->Vertices().size(); ++n) {
                const auto& v = resource->Vertices()[n];
                std::copy_n(v.position.data(), 3, vertices[n].position);
                std::copy_n(v.normal.data(), 3, vertices[n].normal);
                std::copy_n(v.uv.data(), 2, vertices[n].uv);
            }
            if (!accessory->geometry->Publish())
                return Fail(reason, "accessory_geometry");
            for (const auto& m : resource->Materials()) {
                Material saved;
                saved.base = Path(m.texture);
                if (saved.base.filename() == "screen.bmp")
                    return Fail(reason, "screen_texture_requires_scene_history");
                if (!Texture(source->second.parent_path(), saved.base, saved.textures[1], reason))
                    return false;
                accessory->materials.push_back(std::move(saved));
            }
            next->accessories.emplace(item.slot, std::move(accessory));
        }
    impl_ = std::move(next);
    reason.clear();
    return true;
} catch (const std::exception& e) {
    return Fail(reason, e.what());
}

bool SceneFrameProducer::Build(const runtime::SceneRuntime& scene,
                               const SceneRenderSettings& settings, RenderFrame& output,
                               std::string& reason) try {
    if (!impl_ || !settings.width || !settings.height || settings.width > 4096 ||
        settings.height > 4096 || settings.shadow.mode < -1 || settings.shadow.mode > 2 ||
        settings.shadow.mapSize < 16 || settings.shadow.mapSize > 4096)
        return Fail(reason, "scene_render_settings");
    ModelFrameInputs frame;
    frame.view.aspect = float(settings.width) / settings.height;
    frame.view.viewport = {0, 0, settings.width, settings.height, 0, 1};
    if (!Camera(scene, frame, reason))
        return false;
    const auto* session = scene.Session();
    const auto* doc = session ? &session->Document() : nullptr;
    for (unsigned slot = 0; slot < 256; ++slot)
        if (scene.Resource(std::uint8_t(slot)) && !impl_->models.count(std::uint8_t(slot)))
            return Fail(reason, "model_resource_changed_reload_required");
    if (doc && (doc->avi.enabled || doc->picture.enabled))
        return Fail(reason, "scene_background_requires_media_service");
    if (doc && doc->view.groundShadow)
        return Fail(reason, "projected_shadow_requires_scene_pass_migration");
    const int mode = settings.shadow.mode < 0 ? (session ? session->Pose().selfShadowMode : 0)
                                              : settings.shadow.mode;
    const auto distance = settings.shadow.distance > 0 ? settings.shadow.distance
                          : session                    ? session->Pose().selfShadowDistance
                                                       : .01125f;
    BuiltinShadowState shadow;
    shadow.mode = mode ? BuiltinShadowMode::Material : BuiltinShadowMode::None;
    shadow.rotation = frame.lightRotation;
    shadow.perspective = mode == 2;
    ModelPipeline camera;
    ModelMaterialInputs neutral;
    if (!BuildModelInputs(frame, neutral, {}, camera))
        return Fail(reason, "scene_camera");
    const Vec3 eye = frame.scene.camera.parentModel >= 0 ? frame.scene.camera.position
                                                         : Vec3{0, 0, frame.scene.camera.distance};
    Matrix inverse;
    if (!Inverse(frame.world, inverse))
        return Fail(reason, "scene_camera_inverse");
    auto target = TransformPoint(eye, inverse);
    std::copy_n(target.data(), 3, shadow.eye.begin());
    shadow.eye[3] = 1;
    Vec3 light{frame.scene.lightState[3], frame.scene.lightState[4], frame.scene.lightState[5]};
    if (!Normalize(light, light))
        return Fail(reason, "scene_light_direction");
    std::copy_n(light.data(), 3, shadow.lightDirection.begin());
    shadow.lightDirection[3] = 1;
    if (mode && !BuildSelfShadowTransform(frame.world, eye, camera.shader.lights[0].direction,
                                          distance, mode, shadow.lightViewProjection, ShadowMath()))
        return Fail(reason, "scene_shadow_transform");
    RenderFrame result;
    result.finalColor = 1;
    result.source.frame = frame.scene.frame;
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    result.targets = {{1, {settings.width, settings.height, TextureFormat::Rgba8, access}},
                      {2,
                       {settings.width, settings.height, TextureFormat::Depth24Stencil8,
                        unsigned(TextureAccess::DepthStencilAttachment)}}};
    FramePass main;
    main.name = "scene";
    main.color = FrameAttachment{1};
    main.color->clear =
        doc && doc->appearance.blackBackground ? ClearColor{0, 0, 0, 0} : ClearColor{1, 1, 1, 0};
    main.depthStencil = FrameAttachment{2};
    FramePass cast;
    cast.name = "self_shadow";
    if (mode) {
        result.targets.push_back(
            {3,
             {settings.shadow.mapSize, settings.shadow.mapSize, TextureFormat::R32Float, access}});
        result.targets.push_back(
            {4,
             {settings.shadow.mapSize, settings.shadow.mapSize, TextureFormat::Depth24Stencil8,
              unsigned(TextureAccess::DepthStencilAttachment)}});
        cast.color = FrameAttachment{3};
        cast.color->clear = {1, 1, 1, 1};
        cast.depthStencil = FrameAttachment{4};
        main.sampled = {3};
    }
    HistoryDesc history;
    history.width = settings.width;
    history.height = settings.height;
    const auto append = [&](ModelReplayStream& stream) {
        for (auto& r : stream.resources)
            if (std::none_of(result.resources.begin(), result.resources.end(),
                             [&](const auto& x) { return x.key.id == r.key.id; }))
                result.resources.push_back(r);
        for (auto& e : stream.events)
            if (e.op == ModelReplayOp::Draw) {
                if (e.draw.pipeline.shadow.mode == BuiltinShadowMode::Material)
                    e.draw.shadowTarget = 3;
                main.draws.push_back(e.draw);
            }
    };
    if (doc && doc->view.groundGrid) {
        for (const auto family : {SceneDrawFamily::GroundLines, SceneDrawFamily::GroundPlane}) {
            ModelReplayStream ground;
            if (!BuildGroundDraw(frame, family, {}, history, main.color->clear, ground))
                return Fail(reason, "scene_ground_pipeline");
            const std::uint64_t offset = family == SceneDrawFamily::GroundPlane ? 2 : 0;
            for (auto& resource : ground.resources)
                resource.key.id += offset;
            for (auto& event : ground.events) {
                if (event.resource.id)
                    event.resource.id += offset;
                if (event.draw.vertices.id)
                    event.draw.vertices.id += offset;
                if (event.draw.indices.id)
                    event.draw.indices.id += offset;
            }
            append(ground);
        }
    }
    const auto modelStart = main.draws.size();
    std::vector<std::uint8_t> order;
    for (const auto& item : impl_->models)
        if (scene.Resource(item.first))
            order.push_back(item.first);
    if (doc) {
        const auto display = [&](unsigned slot) {
            return std::find_if(doc->models.begin(), doc->models.end(),
                                [&](const auto& m) { return m.slot == slot; })
                ->selection.comboIndex;
        };
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [&](auto slot) { return display(slot) == 255; }),
                    order.end());
        std::stable_sort(order.begin(), order.end(),
                         [&](auto a, auto b) { return display(a) < display(b); });
        order.erase(std::unique(order.begin(), order.end(),
                                [&](auto a, auto b) { return display(a) == display(b); }),
                    order.end());
    }
    for (auto slot : order) {
        auto& model = *impl_->models.at(slot);
        if (scene.Resource(slot) != model.resource)
            return Fail(reason, "model_resource_changed_reload_required");
        if (!model.geometry)
            continue;
        if (session) {
            const auto& poses = session->Pose().models;
            const auto it = std::find_if(poses.begin(), poses.end(),
                                         [&](const auto& p) { return p.slot == slot; });
            if (it == poses.end() || !it->visible)
                continue;
        }
        const auto* snapshot = scene.Snapshot(slot);
        if (!snapshot || snapshot->vertices.size() != model.resource->Document().vertices.size())
            return Fail(reason, "model_snapshot_missing");
        auto* vertices = static_cast<ModelVertex*>(model.geometry->MainData());
        for (std::size_t n = 0; n < snapshot->vertices.size(); ++n) {
            const auto& v = snapshot->vertices[n];
            std::copy_n(v.position.data(), 3, vertices[n].position);
            std::copy_n(v.normal.data(), 3, vertices[n].normal);
            std::copy_n(v.uv.data(), 2, vertices[n].uv);
        }
        if (!model.geometry->Publish())
            return Fail(reason, "model_geometry_publish");
        std::vector<ModelMaterialPacket> packets;
        unsigned first = 0;
        for (std::size_t n = 0; n < snapshot->materials.size(); ++n) {
            const auto& pose = snapshot->materials[n];
            const auto& saved = model.materials.at(n);
            for (const auto* tint : {&pose.textureTint, &pose.sphereTint, &pose.toonTint})
                if (*tint != std::array<float, 4>{1, 1, 1, 1})
                    return Fail(reason, "texture_morph_requires_generic_shader");
            const auto count = pose.material.faceVertexCount;
            if (count < 0)
                return Fail(reason, "material_index_range");
            if (count) {
                ModelMaterialPacket packet;
                packet.material = unsigned(n);
                packet.firstIndex = first;
                packet.indexCount = unsigned(count);
                auto material = Inputs(
                    pose.material, model.resource->SourceKind() == format::ModelKind::Pmd, saved);
                if (!BuildModelInputs(frame, material, {}, packet.pipeline))
                    return Fail(reason, "model_pipeline");
                if (mode)
                    InstallShadow(packet.pipeline, shadow, material, saved.toonColor,
                                  material.pmd || (pose.material.flags & 8));
                for (unsigned stage = 0; stage < 3; ++stage) {
                    const auto role = packet.pipeline.shader.layout.stages[stage].role;
                    if (role == MaterialTextureRole::None)
                        continue;
                    packet.textureResources[stage] =
                        saved
                            .textures[role == MaterialTextureRole::Toon ? 0
                                      : role == MaterialTextureRole::Sphere && !saved.sphere.empty()
                                          ? 2
                                          : 1];
                }
                packets.push_back(std::move(packet));
            }
            first += unsigned(count);
        }
        if (first != model.resource->Document().indices.size())
            return Fail(reason, "incomplete_material_coverage");
        ModelReplayStream stream;
        auto built = model.submission.Build(model.geometry->Snapshot(), history, main.color->clear,
                                            packets, stream);
        if (!built)
            return Fail(reason, built.reason);
        append(stream);
        if (mode)
            for (std::size_t n = main.draws.size() - packets.size(); n < main.draws.size(); ++n) {
                const auto& material =
                    snapshot->materials[packets[n - (main.draws.size() - packets.size())].material]
                        .material;
                const bool pmd = model.resource->SourceKind() == format::ModelKind::Pmd;
                if (material.diffuse[3] == .9800000190734863f ||
                    (!pmd && (!(material.flags & 4) || material.edgeColor[3] <= 0)))
                    continue;
                auto draw = main.draws[n];
                draw.shadowTarget = 0;
                draw.pipeline.shadow.mode = BuiltinShadowMode::Depth;
                draw.pipeline.raster.cull =
                    (material.flags & 1) && !pmd ? BatchCull::None : BatchCull::CounterClockwise;
                draw.pipeline.raster.alphaBlend = false;
                draw.pipeline.stencil = false;
                draw.pipeline.shader.alphaTest = false;
                draw.pipeline.shader.layout = {};
                draw.pipeline.viewport =
                    ModelViewport{0, 0, settings.shadow.mapSize, settings.shadow.mapSize, 0, 1};
                draw.textures = {};
                cast.draws.push_back(std::move(draw));
            }
    }
    std::vector<const format::PmmSceneAccessory*> accessoryOrder;
    if (doc)
        for (const auto& item : doc->accessories)
            if (item.order != 255)
                accessoryOrder.push_back(&item);
    std::stable_sort(accessoryOrder.begin(), accessoryOrder.end(), [](auto* a, auto* b) {
        return a->order == b->order ? a->slot < b->slot : a->order < b->order;
    });
    accessoryOrder.erase(std::unique(accessoryOrder.begin(), accessoryOrder.end(),
                                     [](auto* a, auto* b) { return a->order == b->order; }),
                         accessoryOrder.end());
    std::size_t beforeModels = 0;
    for (const auto* item : accessoryOrder) {
        const auto& poses = session->Pose().accessories;
        const auto evaluated = std::find_if(poses.begin(), poses.end(),
                                            [&](const auto& p) { return p.slot == item->slot; });
        if (evaluated == poses.end())
            return Fail(reason, "accessory_pose_missing");
        const auto& pose = *evaluated;
        if (pose.visible && scene.Accessory(pose.slot)) {
            auto found = impl_->accessories.find(pose.slot);
            if (found == impl_->accessories.end() ||
                found->second->resource != scene.Accessory(pose.slot))
                return Fail(reason, "accessory_resource_changed_reload_required");
            auto& accessory = *found->second;
            auto local = frame;
            Matrix placement;
            if (!scene.AccessoryPlacement(pose.slot, placement, reason))
                return false;
            local.world = Multiply(placement, frame.world);
            std::vector<ModelMaterialPacket> packets;
            for (const auto& subset : accessory.resource->Subsets()) {
                const auto& material = accessory.resource->Materials().at(subset.material);
                const auto& saved = accessory.materials.at(subset.material);
                AccessoryPassInputs input;
                input.material.diffuse = material.diffuse;
                input.material.ambient = material.ambient;
                std::copy_n(material.specular.data(), 3, input.material.specular.begin());
                std::copy_n(material.emissive.data(), 3, input.material.emissive.begin());
                input.material.shininess = material.shininess;
                input.opacity = pose.opacity;
                input.primaryTexture = bool(saved.textures[1]);
                input.afterModelPass = int(item->order) >= doc->physics.accessoryRenderOrder;
                input.additive = item->state.additiveBlend != 0;
                const auto name = saved.base.generic_u8string();
                input.textureType =
                    name.find(".sph") != std::string::npos || name.find(".SPH") != std::string::npos
                        ? 1
                    : name.find(".spa") != std::string::npos ||
                            name.find(".SPA") != std::string::npos
                        ? 2
                        : 0;
                ModelMaterialPacket packet;
                packet.material = unsigned(packets.size());
                packet.firstIndex = subset.firstIndex;
                packet.indexCount = subset.indexCount;
                if (!BuildAccessoryPassInputs(local, input, {}, packet.pipeline))
                    return Fail(reason, "accessory_pipeline");
                packet.textureResources[0] = saved.textures[1];
                packets.push_back(std::move(packet));
            }
            ModelReplayStream stream;
            const auto built = accessory.submission.Build(accessory.geometry->Snapshot(), history,
                                                          main.color->clear, packets, stream);
            if (!built)
                return Fail(reason, built.reason);
            // Accessory projected/self-shadow policy remains explicit, never guessed.
            if (mode && pose.shadowEnabled)
                return Fail(reason, "accessory_self_shadow_not_migrated");
            const auto start = main.draws.size();
            append(stream);
            if (int(item->order) < doc->physics.accessoryRenderOrder) {
                const auto count = main.draws.size() - start;
                std::rotate(main.draws.begin() + modelStart + beforeModels,
                            main.draws.begin() + start, main.draws.end());
                beforeModels += count;
            }
        }
    }
    if (mode)
        result.passes.push_back(std::move(cast));
    result.passes.push_back(std::move(main));
    if (settings.postProcessCopy &&
        !AppendPostProcess(result, 1, 5, settings.width, settings.height))
        return Fail(reason, "scene_post_process");
    if (!ApplyMaterialSampling(result, MaterialSamplingPolicy::PortableTrilinear))
        return Fail(reason, "scene_sampling_policy");
    const auto valid = ValidateRenderFrame(result);
    if (!valid)
        return Fail(reason, valid.reason);
    output = std::move(result);
    reason.clear();
    return true;
} catch (const std::exception& e) {
    return Fail(reason, e.what());
}
}
