#include "mikudancestudio/render_snapshot.hpp"
#include <limits>
#include <utility>
#include <algorithm>
#include <cstring>

namespace mikudancestudio::render {
BatchDependency ClassifyBatchDependency(const format::ModelDocument& model, std::uint32_t first,
                                        std::uint32_t count) {
    // PMX inheritance, SDEF/QDEF and external parents need their own audit.
    if (model.kind != format::ModelKind::Pmd)
        return BatchDependency::Unsupported;
    const auto& pmd = model.pmd;
    if (!count || count % 3 || first > pmd.indices.size() || count > pmd.indices.size() - first)
        return BatchDependency::Invalid;
    std::vector<bool> affected(pmd.bones.size()), physics(pmd.bones.size());
    for (const auto& ik : pmd.ikChains) {
        for (auto bone : {ik.boneIndex, ik.targetBone}) {
            if (bone >= affected.size())
                return BatchDependency::Invalid;
            affected[bone] = true;
        }
        for (auto bone : ik.links) {
            if (bone >= affected.size())
                return BatchDependency::Invalid;
            affected[bone] = true;
        }
    }
    for (const auto& rigid : pmd.rigids) {
        if (rigid.mode == 0 || rigid.boneIndex == -1)
            continue;
        if (rigid.boneIndex < 0 || static_cast<std::size_t>(rigid.boneIndex) >= physics.size())
            return BatchDependency::Invalid;
        physics[rigid.boneIndex] = true;
    }
    std::vector<bool> checked(pmd.bones.size());
    for (std::uint32_t i = first; i < first + count; ++i) {
        if (pmd.indices[i] >= pmd.vertices.size())
            return BatchDependency::Invalid;
        const auto& vertex = pmd.vertices[pmd.indices[i]];
        if (vertex.weightPercent > 100)
            return BatchDependency::Invalid;
        for (int influence = 0; influence < 2; ++influence) {
            if ((influence == 0 && vertex.weightPercent == 0) ||
                (influence == 1 && vertex.weightPercent == 100))
                continue;
            std::int32_t bone = vertex.bone[influence];
            std::vector<std::uint32_t> chain;
            while (bone != -1) {
                if (bone < 0 || static_cast<std::size_t>(bone) >= checked.size() ||
                    std::find(chain.begin(), chain.end(), bone) != chain.end())
                    return BatchDependency::Invalid;
                if (checked[bone])
                    break;
                if (affected[bone])
                    return BatchDependency::Ik;
                if (physics[bone])
                    return BatchDependency::Physics;
                if (pmd.bones[bone].type > 4)
                    return BatchDependency::Unsupported;
                chain.push_back(static_cast<std::uint32_t>(bone));
                bone = pmd.bones[bone].parent;
            }
            for (auto ancestor : chain)
                checked[ancestor] = true;
        }
    }
    return BatchDependency::Independent;
}

bool CaptureSceneSnapshot(const format::SceneSession& session, SceneSnapshot& output) {
    if (!session.Loaded() || session.Models().size() != session.Document().models.size() ||
        session.Pose().models.size() != session.Document().models.size())
        return false;
    SceneSnapshot snapshot;
    snapshot.frame = session.Pose().frame;
    snapshot.camera = session.Pose().camera;
    std::array<float, 3> attachment{};
    if (session.CameraAttachmentPoint(attachment))
        snapshot.cameraAttachment = attachment;
    for (std::size_t index = 0; index < session.Models().size(); ++index) {
        const auto& resource = session.Models()[index];
        const auto slot = session.Document().models[index].slot;
        if (!resource || resource->kind == format::ModelKind::Unknown ||
            session.Pose().models[index].slot != slot)
            return false;
        ModelSnapshot model;
        model.slot = slot;
        model.drawOrder = session.Document().models[index].selection.comboIndex;
        model.visible = session.Pose().models[index].visible != 0;
        model.kind = resource->kind;
        const auto vertices = resource->kind == format::ModelKind::Pmd
                                  ? resource->pmd.vertices.size()
                                  : resource->pmx.vertices.size();
        const auto indices = resource->kind == format::ModelKind::Pmd
                                 ? resource->pmd.indices.size()
                                 : resource->pmx.indices.size();
        if (vertices > std::numeric_limits<std::uint32_t>::max() ||
            indices > std::numeric_limits<std::uint32_t>::max() ||
            !session.KinematicWorld(slot, model.bones))
            return false;
        model.vertexCount = static_cast<std::uint32_t>(vertices);
        model.indexCount = static_cast<std::uint32_t>(indices);
        auto append = [&model, &resource](const format::MaterialData& material) {
            if (material.faceVertexCount < 0)
                return false;
            MaterialBatch batch;
            if (!model.batches.empty()) {
                const auto& previous = model.batches.back();
                batch.firstIndex = previous.firstIndex + previous.indexCount;
            }
            batch.indexCount = static_cast<std::uint32_t>(material.faceVertexCount);
            if (batch.firstIndex > model.indexCount ||
                batch.indexCount > model.indexCount - batch.firstIndex)
                return false;
            batch.textureIndex = material.textureIndex;
            batch.materialIndex = static_cast<std::uint32_t>(model.batches.size());
            batch.flags = material.flags;
            batch.sphereMode = material.sphereMode;
            batch.dependency =
                ClassifyBatchDependency(*resource, batch.firstIndex, batch.indexCount);
            if (resource->kind == format::ModelKind::Pmd) {
                const auto end = std::find(material.pmdTexture, material.pmdTexture + 20, '\0');
                const auto split = std::find(material.pmdTexture, end, '*');
                batch.texture.path.assign(material.pmdTexture, split);
                if (split != end)
                    batch.sphere.path.assign(split + 1, end);
                if (material.toonReference == 255) {
                    batch.toon.encoding = TextureEncoding::SharedToon;
                    batch.toon.index = 0;
                    batch.toon.defaultFallback = true;
                } else if (material.toonReference < 10) {
                    batch.toon.index = material.toonReference;
                    const auto& name = resource->pmd.toonNames[material.toonReference];
                    batch.toon.path.assign(name.begin(), std::find(name.begin(), name.end(), '\0'));
                }
            } else {
                const auto encoding = resource->pmx.header.textEncoding == 1
                                          ? TextureEncoding::Utf8
                                          : TextureEncoding::Utf16LE;
                auto texture = [&](std::int32_t textureIndex, TextureReference& ref) {
                    ref.index = textureIndex;
                    ref.encoding = encoding;
                    if (textureIndex == -1)
                        return true;
                    if (textureIndex < 0 ||
                        static_cast<std::size_t>(textureIndex) >= resource->pmx.textures.size())
                        return false;
                    ref.path = resource->pmx.textures[textureIndex];
                    return true;
                };
                if (!texture(material.textureIndex, batch.texture) ||
                    !texture(material.sphereIndex, batch.sphere))
                    return false;
                if (material.sharedToon) {
                    batch.toon.encoding = TextureEncoding::SharedToon;
                    batch.toon.index = material.toonReference;
                } else {
                    if (!texture(material.toonIndex, batch.toon))
                        return false;
                    batch.toon.defaultFallback = material.toonIndex == -1;
                }
            }
            for (int channel = 0; channel < 4; ++channel)
                batch.diffuse[channel] = material.diffuse[channel];
            model.batches.push_back(batch);
            return true;
        };
        if (resource->kind == format::ModelKind::Pmd) {
            for (const auto& material : resource->pmd.materials)
                if (!append(material))
                    return false;
        } else {
            for (const auto& material : resource->pmx.materials)
                if (!append(material.fields))
                    return false;
        }
        snapshot.models.push_back(std::move(model));
    }
    std::stable_sort(snapshot.models.begin(), snapshot.models.end(),
                     [](const ModelSnapshot& a, const ModelSnapshot& b) {
                         return a.drawOrder < b.drawOrder ||
                                (a.drawOrder == b.drawOrder && a.slot < b.slot);
                     });
    for (const auto& pose : session.Pose().accessories) {
        AccessorySnapshot accessory;
        accessory.slot = pose.slot;
        accessory.visible = pose.visible != 0;
        format::PmmWorldMatrix world{};
        if (session.AccessoryWorld(pose.slot, world))
            accessory.world = world;
        snapshot.accessories.push_back(std::move(accessory));
    }
    output = std::move(snapshot);
    return true;
}
}
