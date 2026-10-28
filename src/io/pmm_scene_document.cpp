#include "mikudancestudio/pmm_scene_document.hpp"
#include <algorithm>
#include <cstring>
#include <utility>

namespace mikudancestudio::format {
namespace {
constexpr std::uint32_t kMaxRecords = 100'000;
struct Cursor {
    const PmmDocument& document;
    std::size_t offset = 0;
    std::size_t size() const {
        return document.prefixBytes.size() + document.remainingBytes.size();
    }
    std::size_t remaining() const { return size() - offset; }
    bool exact(void* destination, std::size_t length) {
        if (length > remaining())
            return false;
        auto* out = static_cast<std::uint8_t*>(destination);
        const auto prefix = document.prefixBytes.size();
        const auto first = offset < prefix ? std::min(length, prefix - offset) : 0;
        if (first)
            std::memcpy(out, document.prefixBytes.data() + offset, first);
        if (first < length)
            std::memcpy(out + first, document.remainingBytes.data() + offset + first - prefix,
                        length - first);
        offset += length;
        return true;
    }
    static bool read(void* context, void* destination, std::size_t length) {
        return static_cast<Cursor*>(context)->exact(destination, length);
    }
    template <class T> bool scalar(T& value) { return exact(&value, sizeof value); }
    bool skip(std::size_t length) {
        if (length > remaining())
            return false;
        offset += length;
        return true;
    }
    bool count(std::uint32_t& value) {
        std::int32_t signedValue = 0;
        if (!scalar(signedValue) || signedValue < 0 ||
            signedValue > static_cast<std::int32_t>(kMaxRecords))
            return false;
        value = static_cast<std::uint32_t>(signedValue);
        return true;
    }
};
bool ValidLayout(const PmmModelLayout& layout, std::uint32_t rigidCount = 0) {
    return layout.boneCount <= kMaxRecords && layout.morphCount <= kMaxRecords &&
           layout.ikCount <= kMaxRecords && rigidCount <= kMaxRecords;
}
bool Name(Cursor& cursor, std::vector<char>& name) {
    std::uint8_t length = 0;
    if (!cursor.scalar(length))
        return false;
    name.resize(length);
    return cursor.exact(name.data(), length);
}
template <class T, class Reader>
bool Track(Cursor& cursor, std::uint32_t dense, Reader reader, PmmTrackCounts& counts,
           PmmKeyTrack<T>& keys) {
    if (dense > kMaxRecords)
        return false;
    keys.dense.resize(dense);
    keys.denseRecordOffsets.resize(dense);
    for (std::uint32_t index = 0; index < dense; ++index) {
        keys.denseRecordOffsets[index] = cursor.offset;
        if (!reader(cursor, keys.dense[index]))
            return false;
    }
    std::uint32_t sparse = 0;
    if (!cursor.count(sparse))
        return false;
    keys.sparse.resize(sparse);
    for (std::uint32_t index = 0; index < sparse; ++index) {
        auto& key = keys.sparse[index];
        if (!cursor.scalar(key.index) || key.index < 0)
            return false;
        key.recordOffset = cursor.offset;
        if (!reader(cursor, key.value))
            return false;
    }
    counts = {dense, sparse};
    return true;
}
bool Bone(Cursor& cursor, PmmVersion version, PmmBoneKey& value) {
    return ReadPmmBoneKey(Cursor::read, &cursor, version, value);
}
bool Morph(Cursor& cursor, PmmMorphKey& value) {
    return ReadPmmMorphKey(Cursor::read, &cursor, value);
}
bool Display(Cursor& cursor, PmmVersion version, std::uint32_t ikCount, std::uint32_t rigidCount,
             PmmDisplayKey& value) {
    return ReadPmmDisplayKey(Cursor::read, &cursor, version, ikCount, rigidCount, value);
}
bool Camera(Cursor& cursor, PmmVersion version, PmmCameraKey& value) {
    return ReadPmmCameraKey(Cursor::read, &cursor, version, value);
}
bool Light(Cursor& cursor, PmmLightKey& value) {
    return ReadPmmLightKey(Cursor::read, &cursor, value);
}
bool Accessory(Cursor& cursor, PmmAccessoryKey& value) {
    return ReadPmmAccessoryKey(Cursor::read, &cursor, value);
}
bool Gravity(Cursor& cursor, PmmGravityKey& value) {
    return ReadPmmGravityKey(Cursor::read, &cursor, value);
}
bool Shadow(Cursor& cursor, PmmSelfShadowKey& value) {
    return ReadPmmSelfShadowKey(Cursor::read, &cursor, value);
}
void Section(PmmSceneDocument& scene, PmmSceneSectionKind kind, std::size_t begin, std::size_t end,
             std::uint8_t slot = 0) {
    scene.sections.push_back({kind, begin, end - begin, slot});
}
bool Model(Cursor& cursor, PmmVersion version, ResolvePmmV1ModelLayout resolveV1,
           void* resolverContext, PmmSceneModel& model) {
    if (!cursor.scalar(model.slot) || model.slot == 255)
        return false;
    if (version == PmmVersion::V1) {
        if (!cursor.exact(model.v1Name.data(), model.v1Name.size()) ||
            !cursor.exact(model.path.data(), model.path.size()) || !resolveV1 ||
            !resolveV1(resolverContext, model.slot, model.v1Name, model.path, model.layout) ||
            !ValidLayout(model.layout))
            return false;
    } else {
        if (!Name(cursor, model.name) || !Name(cursor, model.sourceName) ||
            !cursor.exact(model.path.data(), model.path.size()) ||
            !cursor.scalar(model.groupCount) || !cursor.count(model.layout.boneCount))
            return false;
        model.boneNames.resize(model.layout.boneCount);
        for (auto& name : model.boneNames)
            if (!Name(cursor, name))
                return false;
        if (!cursor.count(model.layout.morphCount))
            return false;
        model.morphNames.resize(model.layout.morphCount);
        for (auto& name : model.morphNames)
            if (!Name(cursor, name))
                return false;
        if (!cursor.count(model.layout.ikCount))
            return false;
        model.ikSources.resize(model.layout.ikCount);
        for (auto& source : model.ikSources)
            if (!cursor.scalar(source))
                return false;
        if (!cursor.count(model.rigidCount) || !ValidLayout(model.layout, model.rigidCount))
            return false;
        model.rigidSources.resize(model.rigidCount);
        for (auto& source : model.rigidSources)
            if (!cursor.scalar(source))
                return false;
    }
    if (!ReadPmmModelSelection(Cursor::read, &cursor, version, model.groupCount, model.selection) ||
        !Track(
            cursor, model.layout.boneCount,
            [version](Cursor& c, PmmBoneKey& key) { return Bone(c, version, key); }, model.bones,
            model.boneKeys) ||
        !Track(cursor, model.layout.morphCount, Morph, model.morphs, model.morphKeys) ||
        !Track(
            cursor, 1,
            [&](Cursor& c, PmmDisplayKey& key) {
                return Display(c, version, model.layout.ikCount, model.rigidCount, key);
            },
            model.displays, model.displayKeys))
        return false;
    model.bonePoseOffset = cursor.offset;
    model.bonePose.resize(model.layout.boneCount);
    const auto components = version == PmmVersion::V1 ? 8u : 7u;
    const auto flags = version == PmmVersion::V1 ? 2u : 3u;
    for (auto& pose : model.bonePose) {
        for (std::size_t i = 0; i < components; ++i)
            if (!cursor.scalar(pose.components[i]))
                return false;
        for (std::size_t i = 0; i < flags; ++i)
            if (!cursor.scalar(pose.flags[i]))
                return false;
    }
    model.morphPoseOffset = cursor.offset;
    model.morphPose.resize(model.layout.morphCount);
    for (auto& pose : model.morphPose)
        if (!cursor.scalar(pose))
            return false;
    model.ikPoseOffset = cursor.offset;
    model.ikPose.resize(model.layout.ikCount);
    for (auto& pose : model.ikPose)
        if (!cursor.scalar(pose))
            return false;
    if (version == PmmVersion::V2) {
        model.rigidPose.resize(model.rigidCount);
        for (auto& pose : model.rigidPose)
            for (auto& value : pose.values)
                if (!cursor.scalar(value))
                    return false;
        if (!cursor.scalar(model.postLoadFlag) || !cursor.scalar(model.edgeScale) ||
            !cursor.scalar(model.toonFlag) || !cursor.scalar(model.previousDisplayOrder))
            return false;
    }
    return true;
}
bool Accessories(Cursor& cursor, PmmVersion version, PmmSceneDocument& scene) {
    if (!cursor.scalar(scene.selectedAccessorySlot) || scene.selectedAccessorySlot == 255 ||
        !cursor.scalar(scene.displayListScroll))
        return false;
    std::uint8_t count = 0;
    if (!cursor.scalar(count))
        return false;
    scene.accessoryListNames.resize(count);
    for (auto& name : scene.accessoryListNames)
        if (!cursor.exact(name.data(), name.size()))
            return false;
    scene.accessories.resize(count);
    std::array<bool, 255> occupied{};
    for (auto& accessory : scene.accessories) {
        if (!cursor.scalar(accessory.slot) || accessory.slot == 255 || occupied[accessory.slot] ||
            !cursor.exact(accessory.name.data(), accessory.name.size()) ||
            !cursor.exact(accessory.path.data(), accessory.path.size()) ||
            !cursor.scalar(accessory.order) ||
            !Track(cursor, 1, Accessory, accessory.keys, accessory.keyRecords))
            return false;
        accessory.stateOffset = cursor.offset;
        if (!ReadPmmAccessoryState(Cursor::read, &cursor, version, accessory.state))
            return false;
        occupied[accessory.slot] = true;
    }
    return true;
}
bool Configuration(Cursor& cursor, PmmVersion version, PmmSceneDocument& scene) {
    scene.timelineOffset = cursor.offset;
    if (!ReadPmmTimelineSettings(Cursor::read, &cursor, scene.timeline) ||
        !ReadPmmWaveReference(Cursor::read, &cursor, scene.wave) ||
        !ReadPmmBackgroundReference(Cursor::read, &cursor, PmmBackgroundKind::Avi, scene.avi) ||
        !ReadPmmBackgroundReference(Cursor::read, &cursor, PmmBackgroundKind::Picture,
                                    scene.picture) ||
        !ReadPmmViewSettings(Cursor::read, &cursor, scene.view))
        return false;
    if (version == PmmVersion::V1) {
        scene.physics.gravityMagnitude = 9.8000002f;
        scene.physics.gravityNoise = 10;
        scene.physics.gravityDirection = {0.0f, -1.0f, 0.0f};
        return true;
    }
    scene.physicsOffset = cursor.offset;
    return ReadPmmPhysicsSettings(Cursor::read, &cursor, scene.physics);
}
bool V2Tail(Cursor& cursor, PmmSceneDocument& scene) {
    auto begin = cursor.offset;
    if (!Track(cursor, 1, Gravity, scene.gravity, scene.gravityKeys))
        return false;
    scene.selfShadowStateOffset = cursor.offset;
    if (!cursor.scalar(scene.selfShadowMode) || !cursor.scalar(scene.physicsInterval))
        return false;
    Section(scene, PmmSceneSectionKind::Gravity, begin, cursor.offset);
    begin = cursor.offset;
    if (!Track(cursor, 1, Shadow, scene.selfShadow, scene.selfShadowKeys))
        return false;
    Section(scene, PmmSceneSectionKind::SelfShadow, begin, cursor.offset);
    begin = cursor.offset;
    if (!ReadPmmSceneAppearance(Cursor::read, &cursor, scene.appearance))
        return false;
    if (cursor.remaining() == 0) {
        Section(scene, PmmSceneSectionKind::Tail, begin, cursor.offset);
        return true;
    }
    std::uint8_t flag = 0;
    if (!cursor.scalar(flag))
        return false;
    if (flag == 1) {
        scene.modelRegistrations.resize(scene.models.size());
        for (auto& registration : scene.modelRegistrations)
            if (!cursor.scalar(registration.slot) || !cursor.scalar(registration.selection))
                return false;
    }
    if (cursor.remaining() != 0)
        return false;
    Section(scene, PmmSceneSectionKind::Tail, begin, cursor.offset);
    return true;
}
bool V1Tail(Cursor& cursor, PmmSceneDocument& scene) {
    if (cursor.remaining() == 0)
        return true;
    // Each later feature starts behind the loader's version gate. End of
    // stream at a gate is valid for older v1 projects; partial fields are not.
    if (!cursor.scalar(scene.physics.accessoryRenderOrder) ||
        !cursor.scalar(scene.physics.projectedShadowAmbient))
        return false;
    for (std::uint32_t slot = 0; slot < 255; ++slot)
        for (auto& model : scene.models)
            if (model.slot == slot && !cursor.scalar(model.postLoadFlag))
                return false;
    for (std::uint32_t slot = 0; slot < 255; ++slot)
        for (auto& accessory : scene.accessories)
            if (accessory.slot == slot && !cursor.scalar(accessory.state.additiveBlend))
                return false;
    scene.v1TailStage = 1;
    if (cursor.remaining() == 0)
        return true;
    std::uint8_t gate = 0;
    float edgeScale = 0;
    if (!cursor.scalar(edgeScale) || !cursor.scalar(scene.physics.projectedShadowBlend) ||
        !cursor.scalar(gate))
        return false;
    for (auto& model : scene.models)
        model.edgeScale = edgeScale;
    scene.v1TailStage = 2;
    if (gate != 1)
        return cursor.remaining() == 0;
    for (std::uint32_t slot = 0; slot < 255; ++slot)
        for (auto& model : scene.models)
            if (model.slot == slot && !cursor.scalar(model.edgeScale))
                return false;
    scene.v1TailStage = 3;
    if (cursor.remaining() == 0)
        return true;
    if (!cursor.scalar(gate))
        return false;
    if (gate != 1)
        return cursor.remaining() == 0;
    scene.physicsOffset = cursor.offset;
    if (!cursor.scalar(scene.physics.playbackMode) ||
        !cursor.scalar(scene.physics.gravityMagnitude) ||
        !cursor.scalar(scene.physics.gravityNoise))
        return false;
    for (auto& direction : scene.physics.gravityDirection)
        if (!cursor.scalar(direction))
            return false;
    if (!cursor.scalar(scene.physics.gravityNoiseEnabled))
        return false;
    scene.v1TailStage = 4;
    if (cursor.remaining() == 0)
        return true;
    if (!cursor.scalar(gate))
        return false;
    if (gate != 1)
        return cursor.remaining() == 0;
    scene.selfShadowStateOffset = cursor.offset;
    if (!cursor.scalar(scene.selfShadowMode) || !cursor.scalar(scene.physicsInterval))
        return false;
    for (std::uint32_t slot = 0; slot < 255; ++slot)
        for (auto& model : scene.models)
            if (model.slot == slot && !cursor.scalar(model.toonFlag))
                return false;
    scene.v1TailStage = 5;
    if (cursor.remaining() == 0)
        return true;
    if (!cursor.scalar(gate))
        return false;
    if (gate != 1)
        return cursor.remaining() == 0;
    if (!Track(cursor, 1, Shadow, scene.selfShadow, scene.selfShadowKeys))
        return false;
    scene.v1TailStage = 6;
    if (cursor.remaining() == 0)
        return true;
    if (!cursor.scalar(gate))
        return false;
    if (gate != 1)
        return cursor.remaining() == 0;
    for (auto& color : scene.appearance.outlineColor)
        if (!cursor.scalar(color))
            return false;
    scene.v1TailStage = 7;
    if (cursor.remaining() == 0)
        return true;
    if (!cursor.scalar(gate))
        return false;
    if (gate != 1)
        return cursor.remaining() == 0;
    if (!cursor.scalar(scene.appearance.blackBackground))
        return false;
    scene.v1TailStage = 8;
    if (cursor.remaining() == 0)
        return true;
    if (!cursor.scalar(gate))
        return false;
    if (gate != 1)
        return cursor.remaining() == 0;
    scene.v1CameraParents.emplace_back();
    if (!cursor.scalar(scene.v1CameraParents.back().model) ||
        !cursor.scalar(scene.v1CameraParents.back().bone))
        return false;
    std::uint32_t cameraParents = 0;
    if (!cursor.count(cameraParents))
        return false;
    scene.v1CameraParents.resize(cameraParents + 1);
    for (std::size_t index = 1; index < scene.v1CameraParents.size(); ++index) {
        auto& parent = scene.v1CameraParents[index];
        if (!cursor.scalar(parent.keyIndex) || !cursor.scalar(parent.model) ||
            !cursor.scalar(parent.bone))
            return false;
    }
    if (!cursor.scalar(scene.appearance.cameraParentModel) ||
        !cursor.scalar(scene.appearance.cameraParentBone))
        return false;
    scene.v1TailStage = 9;
    if (cursor.remaining() == 0)
        return true;
    if (!cursor.scalar(gate))
        return false;
    if (gate != 1)
        return cursor.remaining() == 0;
    for (auto& value : scene.appearance.cameraAttachmentBasis)
        if (!cursor.scalar(value))
            return false;
    if (!cursor.scalar(scene.appearance.followCamera) ||
        !cursor.scalar(scene.appearance.suppressCameraTransform))
        return false;
    scene.v1TailStage = 10;
    if (cursor.remaining() == 0)
        return true;
    if (!cursor.scalar(gate))
        return false;
    if (gate != 1)
        return cursor.remaining() == 0;
    if (!cursor.scalar(scene.appearance.floorVisible))
        return false;
    scene.v1TailStage = 11;
    if (cursor.remaining() == 0)
        return true;
    if (!cursor.scalar(gate))
        return false;
    if (gate == 1) {
        for (std::uint32_t slot = 0; slot < 255; ++slot)
            for (auto& model : scene.models)
                if (model.slot == slot && !cursor.scalar(model.previousDisplayOrder))
                    return false;
        scene.v1TailStage = 12;
    }
    return cursor.remaining() == 0;
}
}
static bool ParseSections(const PmmDocument& envelope, ResolvePmmV1ModelLayout resolveV1,
                          void* resolverContext, PmmSceneDocument& parsed) {
    const auto version = envelope.header.version;
    const std::size_t prefixSize = 30u + (version == PmmVersion::V1 ? 22u : 23u) + 2u +
                                   (version == PmmVersion::V1 ? 20u * envelope.modelCount : 0u);
    if (envelope.prefixBytes.size() != prefixSize || envelope.modelCount > 254 ||
        envelope.selectedModelSlot == 255)
        return false;
    std::array<char, 30> headerBytes{};
    std::memcpy(headerBytes.data(), envelope.prefixBytes.data(), headerBytes.size());
    PmmHeader header;
    if (ParsePmmHeader(headerBytes, header) != PmmHeaderStatus::Ok || header.version != version ||
        header.signatureOffset != envelope.header.signatureOffset)
        return false;
    const auto slotOffset = 30u + (version == PmmVersion::V1 ? 22u : 23u);
    if (envelope.prefixBytes[slotOffset] != envelope.selectedModelSlot ||
        envelope.prefixBytes[slotOffset + 1] != envelope.modelCount)
        return false;
    Cursor cursor{envelope, prefixSize};
    Section(parsed, PmmSceneSectionKind::Header, 0, prefixSize);
    parsed.models.reserve(envelope.modelCount);
    std::array<bool, 255> occupiedModels{};
    for (std::uint32_t index = 0; index < envelope.modelCount; ++index) {
        const auto begin = cursor.offset;
        PmmSceneModel model;
        if (!Model(cursor, version, resolveV1, resolverContext, model) ||
            occupiedModels[model.slot])
            return false;
        occupiedModels[model.slot] = true;
        Section(parsed, PmmSceneSectionKind::Model, begin, cursor.offset, model.slot);
        parsed.models.push_back(std::move(model));
    }
    auto begin = cursor.offset;
    if (!Track(
            cursor, 1, [version](Cursor& c, PmmCameraKey& key) { return Camera(c, version, key); },
            parsed.camera, parsed.cameraKeys))
        return false;
    for (auto& value : parsed.cameraState)
        if (!cursor.scalar(value))
            return false;
    if (!cursor.scalar(parsed.cameraPerspective))
        return false;
    Section(parsed, PmmSceneSectionKind::Camera, begin, cursor.offset);
    begin = cursor.offset;
    if (!Track(cursor, 1, Light, parsed.light, parsed.lightKeys))
        return false;
    parsed.lightStateOffset = cursor.offset;
    for (auto& value : parsed.lightState)
        if (!cursor.scalar(value))
            return false;
    Section(parsed, PmmSceneSectionKind::Light, begin, cursor.offset);
    begin = cursor.offset;
    if (!Accessories(cursor, version, parsed))
        return false;
    Section(parsed, PmmSceneSectionKind::Accessory, begin, cursor.offset);
    begin = cursor.offset;
    if (!Configuration(cursor, version, parsed))
        return false;
    Section(parsed, PmmSceneSectionKind::Configuration, begin, cursor.offset);
    if (version == PmmVersion::V2) {
        if (!V2Tail(cursor, parsed))
            return false;
    } else {
        begin = cursor.offset;
        if (!V1Tail(cursor, parsed))
            return false;
        if (cursor.offset != begin)
            Section(parsed, PmmSceneSectionKind::Tail, begin, cursor.offset);
    }
    if (cursor.remaining() != 0)
        return false;
    return true;
}
bool ParsePmmSceneDocument(const PmmDocument& envelope, ResolvePmmV1ModelLayout resolveV1,
                           void* resolverContext, PmmSceneDocument& scene) {
    PmmSceneDocument parsed;
    if (!ParseSections(envelope, resolveV1, resolverContext, parsed))
        return false;
    parsed.envelope = envelope;
    scene = std::move(parsed);
    return true;
}
bool ReadPmmSceneDocument(ReadPmmBytesResult read, void* context, ResolvePmmV1ModelLayout resolveV1,
                          void* resolverContext, PmmSceneDocument& scene) {
    PmmDocument envelope;
    if (!ReadPmmDocument(read, context, envelope))
        return false;
    PmmSceneDocument parsed;
    if (!ParseSections(envelope, resolveV1, resolverContext, parsed))
        return false;
    parsed.envelope = std::move(envelope);
    scene = std::move(parsed);
    return true;
}
}
