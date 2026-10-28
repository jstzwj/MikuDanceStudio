#include "../render/legacy_model_cpu_math.hpp"
#include "mikudancestudio/runtime_cpu_model.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/model_cpu_bridge.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/render_self_shadow.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>

namespace mikudancestudio {
struct RuntimeCpuModel::Impl {
    std::unique_ptr<MMDApp> ownedCpu;
    MMDApp* cpu = nullptr;
    PhysicsScene ownedPhysics{};
    PhysicsScene* physics = nullptr;
    mdl::ModelRecord storage{};
    std::vector<render::ModelMaterialInputs> materials;
    std::vector<std::array<float, 4>> toonColors;
    std::vector<std::array<std::shared_ptr<const rhi::MaterialTextureAsset>, 3>> textures;
    render::ModelSubmissionBuilder submission;
    bool loaded = false, prepared = false;
    ~Impl() {
        if (cpu)
            cpu->CpuModelFrames().erase(&storage);
        ModelDispose(&storage);
        DisposePhysicsWorld(&ownedPhysics);
    }
};
RuntimeCpuModel::RuntimeCpuModel() = default;
RuntimeCpuModel::~RuntimeCpuModel() = default;
bool RuntimeCpuModel::Load(const std::filesystem::path& path, std::string& reason) {
    return Load(path, nullptr, nullptr, reason);
}
bool RuntimeCpuModel::Load(const std::filesystem::path& path, MMDApp* cpu, PhysicsScene* physics,
                           std::string& reason) {
    // Validate the complete disk structure before entering the legacy adapter,
    // whose partially built records are not a general malformed-file parser.
    std::ifstream input(path, std::ios::binary);
    format::ModelDocument parsed;
    const auto read = [](void* context, void* destination, std::size_t size) {
        auto& stream = *static_cast<std::istream*>(context);
        stream.read(static_cast<char*>(destination), size);
        return int(stream.gcount());
    };
    if (!input || !format::ReadModelDocument(read, &input, parsed)) {
        reason = "invalid_cpu_model_document";
        return false;
    }
    if (parsed.kind == format::ModelKind::Pmx) {
        if (parsed.pmx.bones.empty() || parsed.pmx.frames.size() > 252) {
            reason = "unsupported_pmx_cpu_layout";
            return false;
        }
        for (const auto& vertex : parsed.pmx.vertices)
            if (vertex.weightType > 3) {
                reason = "unsupported_qdef_skinning";
                return false;
            }
    } else if (parsed.pmd.bones.empty()) {
        reason = "unsupported_pmd_cpu_layout";
        return false;
    }
    // Private candidate keeps a failed import from replacing a valid instance.
    auto next = std::make_unique<Impl>();
    ModelInitDefaults(&next->storage);
    if (cpu && physics) {
        next->cpu = cpu;
        next->physics = physics;
    } else {
        next->ownedCpu = std::make_unique<MMDApp>();
        next->cpu = next->ownedCpu.get();
        if (!SceneConstruct(&next->ownedPhysics, nullptr)) {
            reason = "cpu_physics_initialization";
            return false;
        }
        next->physics = &next->ownedPhysics;
    }
    const auto resolved = std::filesystem::absolute(path).lexically_normal();
    if (resolved.native().size() >= 256) {
        reason = "legacy_model_path_too_long";
        return false;
    }
    // CpuOnly resolves texture references against modelDirectory and never
    // changes process CWD, unlike the legacy import path.
    PathResolutionWorkspace paths{};
    if (!ModelLoadPMD(&next->storage, nullptr, resolved.c_str(), nullptr, 0, 0, 1, next->physics,
                      paths, ModelResourceMode::CpuOnly)) {
        reason = "cpu_model_import_failed";
        return false;
    }
    const auto& model = next->storage;
    if (model.vertexBuffer || model.vertexBuffer2 || model.indexBuffer) {
        reason = "cpu_import_created_native_resource";
        return false;
    }
    InitializeModelCpuGeometry(next->cpu, &next->storage, true);
    if (model.vertexCount && model.indexCount &&
        !next->cpu->CpuModelFrames().count(&next->storage)) {
        reason = "cpu_geometry_initialization";
        return false;
    }
    next->loaded = true;
    impl = std::move(next);
    reason.clear();
    return true;
}
bool RuntimeCpuModel::Deform(const std::vector<render::ModelMatrix>& matrices,
                             const std::vector<float>& morphs, float edgeDistance, bool opaqueEdge,
                             std::string& reason) {
    if (!impl || !impl->loaded) {
        reason = "cpu_model_not_loaded";
        return false;
    }
    auto& model = impl->storage;
    if (matrices.size() != model.boneCount || morphs.size() != model.morphCount ||
        !std::isfinite(edgeDistance) || edgeDistance < 0) {
        reason = "cpu_deformation_shape";
        return false;
    }
    for (const auto& m : matrices)
        for (float v : m)
            if (!std::isfinite(v)) {
                reason = "nonfinite_skin_matrix";
                return false;
            }
    for (float weight : morphs)
        if (!std::isfinite(weight)) {
            reason = "nonfinite_morph_weight";
            return false;
        }
    if (model.physicsMode == 2)
        for (unsigned i = 0; i < model.morphCount; ++i)
            if (morphs[i] != 0) {
                const auto& morph = model.morphs[i];
                if (morph.type == 8) {
                    reason = "cpu_material_morph_contract_not_migrated";
                    return false;
                }
                if (morph.type == 0)
                    for (int n = 0; n < morph.groupCount; ++n) {
                        const auto& entry = morph.groupEntries[n];
                        if (entry.weight != 0 && model.morphs[entry.morphIndex].type == 8) {
                            reason = "cpu_material_morph_contract_not_migrated";
                            return false;
                        }
                    }
            }
    // Retain the exact production skinning arithmetic and morph restore pass.
    for (std::size_t i = 0; i < matrices.size(); ++i)
        std::copy(matrices[i].begin(), matrices[i].end(), model.boneTable[i].matInit);
    for (std::size_t i = 0; i < morphs.size(); ++i)
        model.morphs[i].value = morphs[i];
    // Bone-only PMM models still participate in IK, attachments and the
    // shared world, but have no draw resources to produce.
    if (!model.vertexCount || !model.indexCount) {
        reason.clear();
        return true;
    }
    if (!UpdateCpuOnlyModelGeometry(impl->cpu, &impl->storage, edgeDistance, opaqueEdge)) {
        reason = "cpu_deformation_failed";
        return false;
    }
    reason.clear();
    return true;
}
bool RuntimeCpuModel::PrepareMaterials(const rhi::ToonTextureResolver& resolver,
                                       std::string& reason) {
    if (!impl || !impl->loaded) {
        reason = "cpu_model_not_loaded";
        return false;
    }
    const auto& model = impl->storage;
    std::vector<render::ModelMaterialInputs> materials;
    std::vector<std::array<float, 4>> toonColors;
    std::vector<std::array<std::shared_ptr<const rhi::MaterialTextureAsset>, 3>> textures;
    std::map<std::string, std::shared_ptr<const rhi::MaterialTextureAsset>> catalog;
    for (unsigned i = 0; i < model.materialCount; ++i) {
        const auto& src = model.materials[i];
        render::ModelMaterialInputs material;
        std::copy_n(src.diffuse, 4, material.diffuse.begin());
        std::copy_n(src.diffuseMirror, 4, material.ambient.begin());
        std::copy_n(src.specular, 3, material.specular.begin());
        std::memcpy(&material.specular[3], src.gap0, 4);
        std::copy_n(src.ambient, 3, material.emissive.begin());
        std::memcpy(&material.emissive[3], src.gap1, 4);
        material.shininess = src.specularPower;
        material.pmd = model.physicsMode != 2;
        material.doubleSided = !material.pmd && (src.flags & 1);
        material.base = src.texturePath;
        material.sphere = src.spherePath;
        material.sphereMode = src.sphereMode;
        rhi::ToonRequest toon;
        toon.modelDirectory = model.modelDirectory;
        toon.missing =
            material.pmd ? rhi::MissingToonPolicy::Unbound : rhi::MissingToonPolicy::Default;
        if (src.toonReference != 255) {
            // Runtime import encodes private PMX Toon as 0xFE, absent as
            // 0xFF, and shared slots as 0..9; the draw flags do not encode it.
            if (!material.pmd && src.toonReference < 10) {
                toon.kind = rhi::ToonKind::Shared;
                toon.sharedSlot = src.toonReference + 1;
            } else {
                toon.kind = rhi::ToonKind::ModelFile;
                if (material.pmd) {
                    if (src.toonReference >= 10) {
                        reason = "invalid_toon_slot";
                        return false;
                    }
                    wchar_t name[256]{};
                    ConvertMaterialName(nullptr, model.pmdToonFileNames[src.toonReference], name,
                                        256, model.modelDirectory);
                    toon.path = name;
                    for (unsigned n = 1; n <= 10; ++n) {
                        wchar_t shared[20]{};
                        swprintf_s(shared, L"toon%02u.bmp", n);
                        if (toon.path == shared) {
                            toon.kind = rhi::ToonKind::Shared;
                            toon.sharedSlot = n;
                            break;
                        }
                    }
                } else
                    toon.path = src.toonPath;
            }
        }
        std::array<rhi::MaterialTextureAsset, 3> assets;
        if (!resolver.Load(toon, assets[0])) {
            reason = "cpu_toon_decode";
            return false;
        }
        std::array<float, 4> toonColor{1, 1, 1, 1};
        if (src.toonReference != 255) {
            if (toon.kind == rhi::ToonKind::Shared &&
                assets[0].origin == rhi::TextureOrigin::Builtin) {
                for (unsigned c = 0; c < 3; ++c)
                    toonColor[c] = render::DefaultToonShadowColors[3 * (toon.sharedSlot - 1) + c];
            } else if (toon.kind != rhi::ToonKind::Default &&
                       assets[0].origin != rhi::TextureOrigin::Missing && !assets[0].fallback) {
                const auto& image = assets[0].content.image;
                const auto offset = std::size_t(image.height - 1) * image.width * 4;
                for (unsigned c = 0; c < 3; ++c)
                    toonColor[c] = image.pixels[offset + c] * (1.f / 256);
            }
        }
        toonColors.push_back(toonColor);
        for (unsigned stage = 1; stage < 3; ++stage) {
            auto& path = stage == 1 ? material.base : material.sphere;
            if (path.empty())
                continue;
            const auto status = rhi::LoadMaterialTexture(model.modelDirectory, path, assets[stage]);
            if (status == rhi::MaterialResourceStatus::Missing) {
                path.clear();
                continue;
            }
            if (status != rhi::MaterialResourceStatus::Ready) {
                reason = "cpu_material_decode";
                return false;
            }
            if (!rhi::PrepareMaterialMipChain(assets[stage],
                                              rhi::MipPreparationPolicy::BoxLinear)) {
                reason = "cpu_material_mips";
                return false;
            }
        }
        std::array<std::shared_ptr<const rhi::MaterialTextureAsset>, 3> references;
        for (unsigned stage = 0; stage < 3; ++stage)
            if (!assets[stage].identity.empty()) {
                const auto found = catalog.find(assets[stage].identity);
                if (found != catalog.end()) {
                    if (found->second->content.revision != assets[stage].content.revision) {
                        reason = "texture_changed_during_import";
                        return false;
                    }
                    references[stage] = found->second;
                } else {
                    references[stage] =
                        std::make_shared<const rhi::MaterialTextureAsset>(std::move(assets[stage]));
                    catalog.emplace(references[stage]->identity, references[stage]);
                }
            }
        materials.push_back(std::move(material));
        textures.push_back(std::move(references));
    }
    impl->materials = std::move(materials);
    impl->toonColors = std::move(toonColors);
    impl->textures = std::move(textures);
    impl->prepared = true;
    reason.clear();
    return true;
}
bool RuntimeCpuModel::Build(const render::ModelFrameInputs& frame, const rhi::HistoryDesc& target,
                            rhi::ClearColor clear,
                            const std::function<void(render::SceneDrawReplacement)>& append,
                            std::string& reason, const rhi::BuiltinShadowState* shadow) {
    const auto* geometry = Geometry();
    if (impl && impl->loaded && impl->prepared && append &&
        (!impl->storage.vertexCount || !impl->storage.indexCount)) {
        reason.clear();
        return true;
    }
    if (!geometry || !impl->prepared || !append) {
        reason = "cpu_model_not_prepared";
        return false;
    }
    std::vector<render::ModelMaterialPacket> packets;
    unsigned first = 0;
    const auto& model = impl->storage;
    for (unsigned i = 0; i < model.materialCount; ++i) {
        const auto count = model.materials[i].faceVertexCount;
        if (count < 0 || first > model.indexCount || unsigned(count) > model.indexCount - first) {
            reason = "material_index_range";
            return false;
        }
        if (count) {
            render::ModelMaterialPacket packet;
            packet.material = i;
            packet.firstIndex = first;
            packet.indexCount = count;
            if (!render::BuildModelInputs(frame, impl->materials[i], {}, packet.pipeline,
                                          LegacyCpuRenderMath())) {
                reason = "cpu_model_shading";
                return false;
            }
            if (shadow && model.toonFlag && shadow->mode == rhi::BuiltinShadowMode::Material) {
                auto& p = packet.pipeline;
                p.shadow = *shadow;
                const auto& material = impl->materials[i];
                const auto& light = p.shader.lights[0];
                for (unsigned c = 0; c < 3; ++c) {
                    p.shadow.color[c] =
                        material.ambient[c] * light.ambient[c] + material.emissive[c];
                    p.shadow.specular[c] = material.specular[c] * light.specular[c];
                }
                p.shadow.color[3] = material.diffuse[3];
                p.shadow.specular[3] = material.shininess == 0 ? .1f : material.shininess;
                p.shadow.toonColor = impl->toonColors[i];
                p.shadow.receive = model.physicsMode != 2 || (model.materials[i].flags & 8) != 0;
            }
            packet.textureResources[0] = impl->textures[i][0];
            for (unsigned stage = 1; stage < 3; ++stage) {
                const auto role = packet.pipeline.shader.layout.stages[stage].role;
                if (role != rhi::MaterialTextureRole::None)
                    packet.textureResources[stage] =
                        impl->textures[i][role == rhi::MaterialTextureRole::Sphere &&
                                                  !impl->materials[i].sphere.empty()
                                              ? 2
                                              : 1];
            }
            packets.push_back(std::move(packet));
        }
        first += count;
    }
    if (first != model.indexCount) {
        reason = "incomplete_material_coverage";
        return false;
    }
    rhi::ModelReplayStream stream;
    const auto result = impl->submission.Build(*geometry, target, clear, packets, stream);
    if (!result) {
        reason = result.reason;
        return false;
    }
    // Transport semantic material/asset identities with each ordered fragment.
    // A host never parses diagnostic labels or discovers GPU texture bindings.
    unsigned packetIndex = 0;
    for (const auto& event : stream.events)
        if (event.op == rhi::ModelReplayOp::Draw) {
            const auto& packet = packets[packetIndex++];
            render::SceneDrawReplacement draw;
            draw.family = render::SceneDrawFamily::ModelMaterial;
            draw.material = packet.material;
            draw.cpu.target = target;
            draw.cpu.clear = clear;
            draw.cpu.source.frame = frame.scene.frame;
            draw.cpu.complete = true;
            draw.cpu.events.push_back({1, rhi::ModelReplayOp::Clear});
            for (const auto& resource : stream.resources) {
                const auto uses = [&](rhi::ReplayResourceKey key) {
                    return key.id == resource.key.id && key.version == resource.key.version;
                };
                bool used = uses(event.draw.vertices) || uses(event.draw.indices);
                for (const auto& key : event.draw.textures)
                    used = used || uses(key);
                if (!used)
                    continue;
                draw.cpu.resources.push_back(resource);
                rhi::ModelReplayEvent publish;
                publish.sequence = draw.cpu.events.size() + 1;
                publish.op = rhi::ModelReplayOp::PublishResource;
                publish.resource = resource.key;
                draw.cpu.events.push_back(std::move(publish));
            }
            for (unsigned stage = 0; stage < 3; ++stage)
                if (packet.textureResources[stage])
                    draw.textureIdentities[stage] = packet.textureResources[stage]->identity;
            auto submission = event;
            const auto& record = model.materials[packet.material];
            if (shadow && model.toonFlag && shadow->mode == rhi::BuiltinShadowMode::Material &&
                record.diffuse[3] != .9800000190734863f &&
                (model.physicsMode != 2 || (record.edgeColor[3] > 0 && (record.flags & 4) != 0)))
                submission.draw.shadowCaster =
                    record.doubleSided ? rhi::BatchCull::None : rhi::BatchCull::CounterClockwise;
            submission.sequence = draw.cpu.events.size() + 1;
            draw.cpu.events.push_back(std::move(submission));
            append(std::move(draw));
        }
    reason.clear();
    return true;
}
const render::ModelGeometrySnapshot* RuntimeCpuModel::Geometry() const {
    if (!impl || !impl->loaded)
        return nullptr;
    const auto found = impl->cpu->CpuModelFrames().find(&impl->storage);
    return found == impl->cpu->CpuModelFrames().end() ? nullptr
                                                      : &found->second->geometry.Snapshot();
}
mdl::ModelRecord* RuntimeCpuModel::Record() const {
    return impl ? &impl->storage : nullptr;
}
bool RuntimeCpuModel::BuildEdges(const render::ModelFrameInputs& frame,
                                 render::SceneDrawFamily family,
                                 const render::ScenePassSettings& settings,
                                 const rhi::AtlasAsset* helper, const rhi::HistoryDesc& target,
                                 rhi::ClearColor clear,
                                 const std::function<void(render::SceneDrawReplacement)>& append,
                                 std::string& reason, const rhi::BuiltinShadowState* builtin) {
    if (!impl || !impl->loaded || !append) {
        reason = "cpu_model_not_loaded";
        return false;
    }
    auto& model = impl->storage;
    if (!model.loadComplete || !model.vertexCount || !model.indexCount) {
        reason.clear();
        return true;
    }
    const bool shadow = family == render::SceneDrawFamily::ProjectedModelShadow;
    if (!shadow && family != render::SceneDrawFamily::ModelOutline) {
        reason = "invalid_edge_pass";
        return false;
    }
    if (!shadow && model.edgeScale <= 0) {
        reason.clear();
        return true;
    }
    if (model.physicsMode == 2 && !builtin) {
        reason = "cpu_pmx_edges_not_validated";
        return false;
    }
    if (shadow && (!helper || !helper->revision)) {
        reason = "cpu_shadow_helper_missing";
        return false;
    }
    std::vector<render::SceneDrawReplacement> pending;
    unsigned first = 0;
    for (unsigned n = 0; n < model.materialCount; ++n) {
        const auto& material = model.materials[n];
        const auto count = unsigned(material.faceVertexCount);
        const bool pmx = model.physicsMode == 2;
        const bool eligible = shadow ? !pmx || ((material.flags & 2) && material.edgeColor[3] > 0)
                                     : material.doubleSided != 0;
        if (count && eligible) {
            render::SceneDrawReplacement draw;
            draw.family = family;
            draw.material = n;
            draw.cpu.target = target;
            draw.cpu.clear = clear;
            draw.cpu.source.frame = frame.scene.frame;
            draw.cpu.events = {{1, rhi::ModelReplayOp::Clear}};
            rhi::ModelReplayDraw command;
            if (!render::BuildScenePassInputs(frame, family, settings, command.pipeline,
                                              LegacyCpuRenderMath())) {
                reason = "cpu_edge_pipeline";
                return false;
            }
            command.firstIndex = first;
            command.indexCount = count;
            if (pmx && !shadow) {
                command.pipeline.shadow = *builtin;
                command.pipeline.shadow.mode = rhi::BuiltinShadowMode::Outline;
                std::copy_n(material.edgeColor, 4, command.pipeline.shadow.color.begin());
            }
            if (shadow) {
                rhi::ModelReplayResource texture;
                texture.kind = rhi::ReplayResourceKind::Texture;
                texture.key = {0x8000000000000001ull, helper->revision};
                texture.texture.levels = {helper->image};
                draw.cpu.resources.push_back(texture);
                draw.cpu.events.push_back({2, rhi::ModelReplayOp::PublishResource, texture.key});
                command.textures[0] = texture.key;
                draw.textureIdentities[0] = "builtin:accessory_helper";
            }
            const auto appended = render::AppendModelGeometry(draw.cpu, *Geometry(), command, true);
            draw.cpu.complete = true;
            if (!appended || !rhi::ValidateModelReplay(draw.cpu)) {
                reason = "cpu_edge_geometry";
                return false;
            }
            pending.push_back(std::move(draw));
        }
        first += count;
    }
    for (auto& draw : pending)
        append(std::move(draw));
    reason.clear();
    return true;
}
}
