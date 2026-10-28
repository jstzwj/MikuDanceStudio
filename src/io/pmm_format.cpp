#include "mikudancestudio/pmm_format.hpp"
#include <cstring>
#include <utility>
namespace mikudancestudio::format {
PmmHeaderStatus ParsePmmHeader(const std::array<char, 30>& bytes, PmmHeader& header) {
    constexpr char signature[] = "Polygon Movie maker";
    constexpr std::size_t signatureSize = sizeof signature - 1;
    for (std::size_t offset = 0; offset + signatureSize <= bytes.size(); ++offset) {
        if (bytes[offset] == '\0')
            return PmmHeaderStatus::InvalidSignature;
        if (std::memcmp(bytes.data() + offset, signature, signatureSize) != 0)
            continue;
        constexpr std::size_t versionOffset = 0x14;
        if (offset + versionOffset + 5 > bytes.size())
            return PmmHeaderStatus::UnsupportedVersion;
        const char* version = bytes.data() + offset + versionOffset;
        PmmHeader parsed;
        parsed.signatureOffset = offset;
        if (std::memcmp(version, "0001", 5) == 0)
            parsed.version = PmmVersion::V1;
        else if (std::memcmp(version, "0002", 5) == 0)
            parsed.version = PmmVersion::V2;
        else
            return PmmHeaderStatus::UnsupportedVersion;
        header = parsed;
        return PmmHeaderStatus::Ok;
    }
    return PmmHeaderStatus::InvalidSignature;
}
PmmHeaderStatus ReadPmmHeader(ReadPmmBytes read, void* context, PmmHeader& header) {
    std::array<char, 30> bytes{};
    if (!read || !read(context, bytes.data(), bytes.size()))
        return PmmHeaderStatus::Truncated;
    return ParsePmmHeader(bytes, header);
}
bool ReadPmmSceneSettings(ReadPmmBytes read, void* context, PmmVersion version,
                          PmmSceneSettings& settings) {
    if (!read)
        return false;
    PmmSceneSettings parsed;
    if (!read(context, &parsed.renderWidth, 4) || !read(context, &parsed.renderHeight, 4) ||
        !read(context, &parsed.editPanelWidth, 4) || !read(context, &parsed.cameraFov, 4))
        return false;
    const int optionCount = version == PmmVersion::V1 ? 6 : 7;
    for (int index = 0; index < optionCount; ++index) {
        std::uint8_t flag = 0;
        if (!read(context, &flag, 1))
            return false;
        parsed.options[index] = flag == 1;
    }
    settings = parsed;
    return true;
}
bool ReadPmmTimelineSettings(ReadPmmBytes read, void* context, PmmTimelineSettings& settings) {
    if (!read)
        return false;
    PmmTimelineSettings parsed;
    std::uint8_t flag = 0;
    if (!read(context, &parsed.currentFrame, 4) || !read(context, &parsed.startFrame, 4) ||
        !read(context, &parsed.lastRegisteredFrame, 4) || !read(context, &parsed.editMode, 4) ||
        !read(context, &parsed.cameraReferenceMode, 1) || !read(context, &flag, 1))
        return false;
    parsed.playbackLoop = flag != 0;
    if (!read(context, &flag, 1))
        return false;
    parsed.returnToStart = flag != 0;
    if (!read(context, &flag, 1))
        return false;
    parsed.startAtCurrent = flag != 0;
    if (!read(context, &parsed.playStartFrame, 4) || !read(context, &parsed.playStopFrame, 4))
        return false;
    settings = parsed;
    return true;
}
bool ReadPmmWaveReference(ReadPmmBytes read, void* context, PmmWaveReference& reference) {
    if (!read)
        return false;
    PmmWaveReference parsed;
    std::uint8_t flag = 0;
    if (!read(context, &flag, 1) ||
        !read(context, parsed.pathBytes.data(), parsed.pathBytes.size()))
        return false;
    parsed.enabled = flag != 0;
    reference = parsed;
    return true;
}
bool ReadPmmBackgroundReference(ReadPmmBytes read, void* context, PmmBackgroundKind kind,
                                PmmBackgroundReference& reference) {
    if (!read)
        return false;
    PmmBackgroundReference parsed;
    if (!read(context, &parsed.offsetX, 4) || !read(context, &parsed.offsetY, 4) ||
        !read(context, &parsed.scale, 4) ||
        !read(context, parsed.pathBytes.data(), parsed.pathBytes.size()))
        return false;
    if (kind == PmmBackgroundKind::Avi) {
        if (!read(context, &parsed.enabled, 4))
            return false;
    } else {
        std::uint8_t enabled = 0;
        if (!read(context, &enabled, 1))
            return false;
        parsed.enabled = enabled;
    }
    reference = parsed;
    return true;
}
bool ReadPmmViewSettings(ReadPmmBytes read, void* context, PmmViewSettings& settings) {
    if (!read)
        return false;
    PmmViewSettings parsed;
    std::uint8_t flag = 0;
    if (!read(context, &flag, 1))
        return false;
    parsed.fpsOverlay = flag != 0;
    if (!read(context, &flag, 1))
        return false;
    parsed.groundGrid = flag != 0;
    if (!read(context, &flag, 1))
        return false;
    parsed.groundShadow = flag != 0;
    if (!read(context, &parsed.fpsLimit, 4) || !read(context, &parsed.captureMode, 4))
        return false;
    settings = parsed;
    return true;
}
bool ReadPmmPhysicsSettings(ReadPmmBytes read, void* context, PmmPhysicsSettings& settings) {
    if (!read)
        return false;
    PmmPhysicsSettings parsed;
    std::uint8_t flag = 0;
    if (!read(context, &parsed.accessoryRenderOrder, 4) ||
        !read(context, &parsed.projectedShadowAmbient, 4) || !read(context, &flag, 1))
        return false;
    parsed.projectedShadowBlend = flag == 1;
    if (!read(context, &parsed.playbackMode, 1) || !read(context, &parsed.gravityMagnitude, 4) ||
        !read(context, &parsed.gravityNoise, 4) ||
        !read(context, parsed.gravityDirection.data(), 12) || !read(context, &flag, 1))
        return false;
    parsed.gravityNoiseEnabled = flag == 1;
    settings = parsed;
    return true;
}
bool ReadPmmSceneAppearance(ReadPmmBytes read, void* context, PmmSceneAppearance& appearance) {
    if (!read)
        return false;
    PmmSceneAppearance parsed;
    std::uint8_t flag = 0;
    for (auto& color : parsed.outlineColor)
        if (!read(context, &color, 4))
            return false;
    if (!read(context, &flag, 1))
        return false;
    parsed.blackBackground = flag != 0;
    if (!read(context, &parsed.cameraParentModel, 4) || !read(context, &parsed.cameraParentBone, 4))
        return false;
    for (auto& value : parsed.cameraAttachmentBasis)
        if (!read(context, &value, 4))
            return false;
    if (!read(context, &flag, 1))
        return false;
    parsed.followCamera = flag == 1;
    if (!read(context, &flag, 1))
        return false;
    parsed.suppressCameraTransform = flag == 1;
    if (!read(context, &flag, 1))
        return false;
    parsed.floorVisible = flag == 1;
    if (!read(context, &parsed.maxFrame, 4))
        return false;
    appearance = parsed;
    return true;
}
bool ReadPmmBoneKey(ReadPmmBytes read, void* context, PmmVersion version, PmmBoneKey& key) {
    if (!read)
        return false;
    PmmBoneKey parsed;
    if (!read(context, &parsed.frame, 4) || !read(context, &parsed.previous, 4) ||
        !read(context, &parsed.next, 4))
        return false;
    for (int lane = 0; lane < 4; ++lane)
        for (int group = 0; group < 4; ++group)
            if (!read(context, &parsed.interpolation[lane + group * 4], 1))
                return false;
    if (!read(context, parsed.position.data(), sizeof(float) * 3) ||
        !read(context, parsed.rotation.data(), sizeof(float) * 4) ||
        !read(context, &parsed.allocated, 1) ||
        (version == PmmVersion::V2 && !read(context, &parsed.physicsDisabled, 1)))
        return false;
    key = parsed;
    return true;
}
bool ReadPmmMorphKey(ReadPmmBytes read, void* context, PmmMorphKey& key) {
    if (!read)
        return false;
    PmmMorphKey parsed;
    if (!read(context, &parsed.frame, 4) || !read(context, &parsed.previous, 4) ||
        !read(context, &parsed.next, 4) || !read(context, &parsed.value, 4) ||
        !read(context, &parsed.allocated, 1))
        return false;
    key = parsed;
    return true;
}
bool ReadPmmCameraKey(ReadPmmBytes read, void* context, PmmVersion version, PmmCameraKey& key) {
    if (!read)
        return false;
    PmmCameraKey parsed;
    if (!read(context, &parsed.frame, 4) || !read(context, &parsed.previous, 4) ||
        !read(context, &parsed.next, 4) || !read(context, &parsed.distance, 4) ||
        !read(context, parsed.eye.data(), 12) || !read(context, parsed.target.data(), 12))
        return false;
    if (version == PmmVersion::V2 &&
        (!read(context, &parsed.parentModel, 4) || !read(context, &parsed.parentBone, 4)))
        return false;
    for (int column = 0; column < 6; ++column)
        for (int row = 0; row < 4; ++row)
            if (!read(context, &parsed.interpolation[row][column], 1))
                return false;
    std::uint8_t flag = 0;
    if (!read(context, &flag, 1))
        return false;
    parsed.perspective = flag == 1;
    if (!read(context, &parsed.fov, 4) || !read(context, &flag, 1))
        return false;
    parsed.selected = flag == 1;
    key = parsed;
    return true;
}
bool ReadPmmLightKey(ReadPmmBytes read, void* context, PmmLightKey& key) {
    if (!read)
        return false;
    PmmLightKey parsed;
    std::uint8_t flag = 0;
    if (!read(context, &parsed.frame, 4) || !read(context, &parsed.previous, 4) ||
        !read(context, &parsed.next, 4) || !read(context, parsed.color.data(), 12) ||
        !read(context, parsed.direction.data(), 12) || !read(context, &flag, 1))
        return false;
    parsed.selected = flag == 1;
    key = parsed;
    return true;
}
bool ReadPmmSelfShadowKey(ReadPmmBytes read, void* context, PmmSelfShadowKey& key) {
    if (!read)
        return false;
    PmmSelfShadowKey parsed;
    std::uint8_t flag = 0;
    if (!read(context, &parsed.frame, 4) || !read(context, &parsed.previous, 4) ||
        !read(context, &parsed.next, 4) || !read(context, &parsed.mode, 1) ||
        !read(context, &parsed.distance, 4) || !read(context, &flag, 1))
        return false;
    parsed.selected = flag == 1;
    key = parsed;
    return true;
}
bool ReadPmmGravityKey(ReadPmmBytes read, void* context, PmmGravityKey& key) {
    if (!read)
        return false;
    PmmGravityKey parsed;
    std::uint8_t flag = 0;
    if (!read(context, &parsed.frame, 4) || !read(context, &parsed.previous, 4) ||
        !read(context, &parsed.next, 4) || !read(context, &flag, 1))
        return false;
    parsed.noiseEnabled = flag == 1;
    if (!read(context, &parsed.noise, 4) || !read(context, &parsed.acceleration, 4) ||
        !read(context, parsed.direction.data(), 12) || !read(context, &flag, 1))
        return false;
    parsed.selected = flag == 1;
    key = parsed;
    return true;
}
bool ReadPmmAccessoryKey(ReadPmmBytes read, void* context, PmmAccessoryKey& key) {
    if (!read)
        return false;
    PmmAccessoryKey parsed;
    std::uint8_t flag = 0;
    if (!read(context, &parsed.frame, 4) || !read(context, &parsed.previous, 4) ||
        !read(context, &parsed.next, 4) || !read(context, &flag, 1))
        return false;
    parsed.visible = flag & 1;
    parsed.opacity = static_cast<float>(100 - (flag >> 1)) / 100.0f;
    if (!read(context, &parsed.parentModel, 4) || !read(context, &parsed.parentBone, 4) ||
        !read(context, parsed.position.data(), 12) || !read(context, parsed.rotation.data(), 12) ||
        !read(context, &parsed.scale, 4) || !read(context, &flag, 1))
        return false;
    parsed.shadowEnabled = flag == 1;
    if (!read(context, &flag, 1))
        return false;
    parsed.selected = flag == 1;
    key = parsed;
    return true;
}
bool ReadPmmAccessoryState(ReadPmmBytes read, void* context, PmmVersion version,
                           PmmAccessoryState& state) {
    if (!read)
        return false;
    PmmAccessoryState parsed;
    std::uint8_t flag = 0;
    if (!read(context, &flag, 1))
        return false;
    parsed.visible = flag & 1;
    parsed.opacity = static_cast<float>(100 - (flag >> 1)) / 100.0f;
    if (!read(context, &parsed.parentModel, 4) || !read(context, &parsed.parentBone, 4) ||
        !read(context, parsed.rotation.data(), 12) || !read(context, &parsed.scale, 4) ||
        !read(context, parsed.position.data(), 12) || !read(context, &flag, 1))
        return false;
    parsed.shadowEnabled = flag == 1;
    if (version == PmmVersion::V2) {
        if (!read(context, &flag, 1))
            return false;
        parsed.additiveBlend = flag == 1;
    }
    state = parsed;
    return true;
}
bool ReadPmmDisplayKey(ReadPmmBytes read, void* context, PmmVersion version, std::uint32_t ikCount,
                       std::uint32_t rigidCount, PmmDisplayKey& key) {
    if (!read || ikCount > 100'000 || rigidCount > 100'000)
        return false;
    PmmDisplayKey parsed;
    std::uint8_t flag = 0;
    if (!read(context, &parsed.frame, 4) || !read(context, &parsed.previous, 4) ||
        !read(context, &parsed.next, 4) || !read(context, &flag, 1))
        return false;
    parsed.visible = flag == 1;
    parsed.ikEnabled.resize(ikCount);
    for (auto& enabled : parsed.ikEnabled) {
        if (!read(context, &flag, 1))
            return false;
        enabled = flag == 1;
    }
    if (version == PmmVersion::V2) {
        parsed.rigidStates.resize(rigidCount);
        for (auto& state : parsed.rigidStates)
            if (!read(context, &state.modelIndex, 4) || !read(context, &state.boneIndex, 4))
                return false;
    }
    if (!read(context, &flag, 1))
        return false;
    parsed.allocated = flag == 1;
    key = std::move(parsed);
    return true;
}
bool ReadPmmModelSelection(ReadPmmBytes read, void* context, PmmVersion version,
                           std::uint8_t v2GroupCount, PmmModelSelection& selection) {
    if (!read)
        return false;
    PmmModelSelection parsed;
    std::uint8_t flag = 0;
    if (!read(context, &parsed.comboIndex, 1) || !read(context, &flag, 1) ||
        !read(context, &parsed.selectedBone, 4))
        return false;
    parsed.loadComplete = flag == 1;
    for (auto& morph : parsed.selectedMorphs)
        if (!read(context, &morph, 4))
            return false;
    if (!read(context, &flag, 1))
        return false;
    const std::size_t count = version == PmmVersion::V1 ? flag : v2GroupCount;
    parsed.groupFlags.resize(count);
    for (auto& group : parsed.groupFlags) {
        if (!read(context, &flag, 1))
            return false;
        group = flag == 1;
    }
    if (!read(context, &parsed.boneListPos, 4) || !read(context, &parsed.maxFrame, 4))
        return false;
    selection = std::move(parsed);
    return true;
}
}
