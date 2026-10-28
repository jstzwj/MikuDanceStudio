#pragma once
#include "mikudancestudio/pmm_document.hpp"

namespace mikudancestudio::format {

struct PmmModelLayout {
    std::uint32_t boneCount = 0, morphCount = 0, ikCount = 0;
};
using ResolvePmmV1ModelLayout = bool (*)(void* context, std::uint8_t slot,
                                         const std::array<char, 20>& name,
                                         const std::array<char, 256>& path, PmmModelLayout& layout);

enum class PmmSceneSectionKind {
    Header,
    Model,
    Camera,
    Light,
    Accessory,
    Configuration,
    Gravity,
    SelfShadow,
    Tail
};
struct PmmSceneSection {
    PmmSceneSectionKind kind = PmmSceneSectionKind::Header;
    std::size_t offset = 0, size = 0;
    std::uint8_t slot = 0;
};
struct PmmTrackCounts {
    std::uint32_t dense = 0, sparse = 0;
};
template <class T> struct PmmSparseKey {
    std::int32_t index = 0;
    std::size_t recordOffset = 0;
    T value;
};
template <class T> struct PmmKeyTrack {
    std::vector<std::size_t> denseRecordOffsets;
    std::vector<T> dense;
    std::vector<PmmSparseKey<T>> sparse;
};
struct PmmBonePose {
    std::array<float, 8> components{};
    std::array<std::uint8_t, 3> flags{};
};
struct PmmRigidPose {
    std::array<std::int32_t, 4> values{};
};
struct PmmCameraParentRef {
    std::int32_t keyIndex = 0, model = 0, bone = 0;
};
struct PmmModelRegistration {
    std::uint8_t slot = 0;
    std::int32_t selection = 0;
};
struct PmmSceneModel {
    std::uint8_t slot = 0, groupCount = 0;
    std::array<char, 20> v1Name{};
    std::vector<char> name, sourceName;
    std::array<char, 256> path{};
    PmmModelLayout layout;
    std::uint32_t rigidCount = 0;
    PmmModelSelection selection;
    PmmTrackCounts bones, morphs, displays;
    PmmKeyTrack<PmmBoneKey> boneKeys;
    PmmKeyTrack<PmmMorphKey> morphKeys;
    PmmKeyTrack<PmmDisplayKey> displayKeys;
    std::vector<std::vector<char>> boneNames, morphNames;
    std::vector<std::int32_t> ikSources, rigidSources;
    std::vector<PmmBonePose> bonePose;
    std::vector<float> morphPose;
    std::vector<std::uint8_t> ikPose;
    std::size_t bonePoseOffset = 0, morphPoseOffset = 0, ikPoseOffset = 0;
    std::vector<PmmRigidPose> rigidPose;
    std::uint8_t postLoadFlag = 0, toonFlag = 0, previousDisplayOrder = 0;
    float edgeScale = 0;
};
struct PmmSceneAccessory {
    std::uint8_t slot = 0, order = 0;
    std::array<char, 100> name{};
    std::array<char, 256> path{};
    PmmTrackCounts keys;
    PmmKeyTrack<PmmAccessoryKey> keyRecords;
    PmmAccessoryState state;
    std::size_t stateOffset = 0;
};
struct PmmSceneDocument {
    PmmDocument envelope;
    std::vector<PmmSceneSection> sections;
    std::vector<PmmSceneModel> models;
    std::vector<std::array<char, 100>> accessoryListNames;
    std::vector<PmmSceneAccessory> accessories;
    PmmTrackCounts camera, light, gravity, selfShadow;
    PmmKeyTrack<PmmCameraKey> cameraKeys;
    PmmKeyTrack<PmmLightKey> lightKeys;
    PmmKeyTrack<PmmGravityKey> gravityKeys;
    PmmKeyTrack<PmmSelfShadowKey> selfShadowKeys;
    std::array<float, 9> cameraState{};
    std::array<float, 6> lightState{};
    std::size_t lightStateOffset = 0, timelineOffset = 0;
    std::uint8_t cameraPerspective = 0, selectedAccessorySlot = 0;
    std::int32_t displayListScroll = 0;
    PmmTimelineSettings timeline;
    PmmWaveReference wave;
    PmmBackgroundReference avi, picture;
    PmmViewSettings view;
    PmmPhysicsSettings physics;
    std::size_t physicsOffset = 0;
    PmmSceneAppearance appearance;
    std::uint8_t selfShadowMode = 0;
    float physicsInterval = 0;
    std::size_t selfShadowStateOffset = 0;
    std::uint8_t v1TailStage = 0;
    std::vector<PmmCameraParentRef> v1CameraParents;
    std::vector<PmmModelRegistration> modelRegistrations;
};

// Parses every ordered disk section. V1 models require layout counts from the
// referenced PMD; no path resolution or decoding occurs inside this function.
bool ParsePmmSceneDocument(const PmmDocument& envelope, ResolvePmmV1ModelLayout resolveV1,
                           void* resolverContext, PmmSceneDocument& scene);
bool ReadPmmSceneDocument(ReadPmmBytesResult read, void* context, ResolvePmmV1ModelLayout resolveV1,
                          void* resolverContext, PmmSceneDocument& scene);
// Writes editable scene settings, timeline, light state, and each model's
// current bone, morph, and IK state. V2 also writes values in existing bone,
// morph, display/IK/selector, camera, and light key records. Key topology is fixed.
// Other bytes retain their original value.
// The v1 resolver must still be able to open the referenced PMDs.
// No output is written if validation fails.
bool WritePmmSceneDocument(WritePmmBytes write, void* context, const PmmSceneDocument& scene,
                           ResolvePmmV1ModelLayout resolveV1 = nullptr,
                           void* resolverContext = nullptr);
}
