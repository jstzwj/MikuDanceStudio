#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace mikudancestudio::format {
using ReadPmmBytes = bool (*)(void*, void*, std::size_t);
enum class PmmVersion {
    V1,
    V2
};
enum class PmmHeaderStatus {
    Ok,
    Truncated,
    InvalidSignature,
    UnsupportedVersion
};
struct PmmHeader {
    PmmVersion version = PmmVersion::V1;
    std::size_t signatureOffset = 0;
};
struct PmmSceneSettings {
    std::int32_t renderWidth = 0, renderHeight = 0, editPanelWidth = 0;
    float cameraFov = 0;
    std::array<std::uint8_t, 7> options{};
};
struct PmmTimelineSettings {
    std::int32_t currentFrame = 0, startFrame = 0, lastRegisteredFrame = 0;
    std::int32_t editMode = 0;
    std::uint8_t cameraReferenceMode = 0;
    std::uint8_t playbackLoop = 0, returnToStart = 0, startAtCurrent = 0;
    std::int32_t playStartFrame = 0, playStopFrame = 0;
};
struct PmmWaveReference {
    std::uint8_t enabled = 0;
    std::array<char, 256> pathBytes{};
};
enum class PmmBackgroundKind {
    Avi,
    Picture
};
struct PmmBackgroundReference {
    std::int32_t offsetX = 0, offsetY = 0;
    float scale = 0;
    std::array<char, 256> pathBytes{};
    std::uint32_t enabled = 0;
};
struct PmmViewSettings {
    std::uint8_t fpsOverlay = 0, groundGrid = 0, groundShadow = 0;
    float fpsLimit = 0;
    std::int32_t captureMode = 0;
};
struct PmmPhysicsSettings {
    std::int32_t accessoryRenderOrder = 0;
    float projectedShadowAmbient = 0;
    std::uint8_t projectedShadowBlend = 0, playbackMode = 0;
    float gravityMagnitude = 0;
    std::int32_t gravityNoise = 0;
    std::array<float, 3> gravityDirection{};
    std::uint8_t gravityNoiseEnabled = 0;
};
struct PmmSceneAppearance {
    std::array<std::int32_t, 3> outlineColor{};
    std::uint8_t blackBackground = 0;
    std::int32_t cameraParentModel = 0, cameraParentBone = 0;
    std::array<float, 16> cameraAttachmentBasis{};
    std::uint8_t followCamera = 0, suppressCameraTransform = 0;
    std::uint8_t floorVisible = 0;
    std::int32_t maxFrame = 0;
};
PmmHeaderStatus ParsePmmHeader(const std::array<char, 30>& bytes, PmmHeader& header);
PmmHeaderStatus ReadPmmHeader(ReadPmmBytes read, void* context, PmmHeader& header);
bool ReadPmmSceneSettings(ReadPmmBytes read, void* context, PmmVersion version,
                          PmmSceneSettings& settings);
bool ReadPmmTimelineSettings(ReadPmmBytes read, void* context, PmmTimelineSettings& settings);
bool ReadPmmWaveReference(ReadPmmBytes read, void* context, PmmWaveReference& reference);
bool ReadPmmBackgroundReference(ReadPmmBytes read, void* context, PmmBackgroundKind kind,
                                PmmBackgroundReference& reference);
bool ReadPmmViewSettings(ReadPmmBytes read, void* context, PmmViewSettings& settings);
bool ReadPmmPhysicsSettings(ReadPmmBytes read, void* context, PmmPhysicsSettings& settings);
bool ReadPmmSceneAppearance(ReadPmmBytes read, void* context, PmmSceneAppearance& appearance);
struct PmmBoneKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    std::array<std::uint8_t, 16> interpolation{};
    std::array<float, 3> position{};
    std::array<float, 4> rotation{};
    std::uint8_t allocated = 0, physicsDisabled = 0;
};
struct PmmMorphKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    float value = 0;
    std::uint8_t allocated = 0;
};
struct PmmCameraKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    float distance = 0;
    std::array<float, 3> eye{}, target{};
    std::int32_t parentModel = 0, parentBone = 0;
    std::array<std::array<std::uint8_t, 6>, 4> interpolation{};
    std::uint8_t perspective = 0, selected = 0;
    std::int32_t fov = 0;
};
struct PmmLightKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    std::array<float, 3> color{}, direction{};
    std::uint8_t selected = 0;
};
struct PmmSelfShadowKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    std::uint8_t mode = 0, selected = 0;
    float distance = 0;
};
struct PmmGravityKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    std::uint8_t noiseEnabled = 0, selected = 0;
    std::int32_t noise = 0;
    float acceleration = 0;
    std::array<float, 3> direction{};
};
struct PmmAccessoryKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    std::uint8_t visible = 0, shadowEnabled = 0, selected = 0;
    float opacity = 0;
    std::int32_t parentModel = 0, parentBone = 0;
    std::array<float, 3> position{}, rotation{};
    float scale = 0;
};
struct PmmAccessoryState {
    std::uint8_t visible = 0, shadowEnabled = 0, additiveBlend = 0;
    float opacity = 0, scale = 0;
    std::int32_t parentModel = 0, parentBone = 0;
    std::array<float, 3> rotation{}, position{};
};
struct PmmRigidState {
    std::int32_t modelIndex = 0, boneIndex = 0;
};
struct PmmDisplayKey {
    std::uint32_t frame = 0, previous = 0, next = 0;
    std::uint8_t visible = 0, allocated = 0;
    std::vector<std::uint8_t> ikEnabled;
    std::vector<PmmRigidState> rigidStates;
};
struct PmmModelSelection {
    std::uint8_t comboIndex = 0, loadComplete = 0;
    std::int32_t selectedBone = 0, boneListPos = 0;
    std::array<std::int32_t, 4> selectedMorphs{};
    std::vector<std::uint8_t> groupFlags;
    std::uint32_t maxFrame = 0;
};
bool ReadPmmBoneKey(ReadPmmBytes read, void* context, PmmVersion version, PmmBoneKey& key);
bool ReadPmmMorphKey(ReadPmmBytes read, void* context, PmmMorphKey& key);
bool ReadPmmCameraKey(ReadPmmBytes read, void* context, PmmVersion version, PmmCameraKey& key);
bool ReadPmmLightKey(ReadPmmBytes read, void* context, PmmLightKey& key);
bool ReadPmmSelfShadowKey(ReadPmmBytes read, void* context, PmmSelfShadowKey& key);
bool ReadPmmGravityKey(ReadPmmBytes read, void* context, PmmGravityKey& key);
bool ReadPmmAccessoryKey(ReadPmmBytes read, void* context, PmmAccessoryKey& key);
bool ReadPmmAccessoryState(ReadPmmBytes read, void* context, PmmVersion version,
                           PmmAccessoryState& state);
bool ReadPmmDisplayKey(ReadPmmBytes read, void* context, PmmVersion version, std::uint32_t ikCount,
                       std::uint32_t rigidCount, PmmDisplayKey& key);
bool ReadPmmModelSelection(ReadPmmBytes read, void* context, PmmVersion version,
                           std::uint8_t v2GroupCount, PmmModelSelection& selection);
}
