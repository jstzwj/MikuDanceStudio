#pragma once
#include "mikudancestudio/pmm_scene_document.hpp"

namespace mikudancestudio::format {
struct PmmEvaluatedBone {
    std::array<float, 3> translation{};
    std::array<float, 4> rotation{};
    std::uint8_t physicsDisabled = 0;
};
struct PmmEvaluatedSelector {
    std::int32_t linkedModel = 0, linkedBone = 0;
    std::uint32_t windowStart = 0, windowEnd = 0;
};
struct PmmEvaluatedModel {
    std::uint8_t slot = 0;
    std::uint8_t visible = 0;
    std::vector<float> morphValues;
    std::vector<PmmEvaluatedBone> bones;
    std::vector<std::uint8_t> ikEnabled;
    std::vector<PmmEvaluatedSelector> selectors;
};
struct PmmEvaluatedCamera {
    std::array<float, 3> position{}, rotation{};
    float distance = 0, fov = 0;
    std::uint8_t perspective = 0;
    std::int32_t parentModel = 0, parentBone = 0;
};
struct PmmEvaluatedAccessory {
    std::uint8_t slot = 0, visible = 0, shadowEnabled = 0;
    float opacity = 0, scale = 0;
    std::int32_t parentModel = 0, parentBone = 0;
    std::array<float, 3> position{}, rotation{};
};
struct PmmEvaluatedGravity {
    std::uint8_t noiseEnabled = 0;
    std::int32_t noise = 0;
    float acceleration = 0;
    std::array<float, 3> direction{};
};
struct PmmSceneEvaluation {
    std::uint32_t frame = 0;
    PmmEvaluatedCamera camera;
    std::array<float, 6> lightState{}; // color RGB, direction XYZ
    std::uint8_t selfShadowMode = 0;
    float selfShadowDistance = 0;
    PmmEvaluatedGravity gravity;
    std::vector<PmmEvaluatedModel> models;
    std::vector<PmmEvaluatedAccessory> accessories;
};
struct PmmTrackCursor {
    std::uint32_t index = 0;
    bool active = true;
};
struct PmmModelPlaybackCursors {
    PmmTrackCursor display;
    std::vector<PmmTrackCursor> bones, morphs;
};
struct PmmScenePlaybackState {
    double frame = 0.0;
    PmmSceneEvaluation pose;
    std::vector<PmmModelPlaybackCursors> models;
    std::vector<PmmTrackCursor> accessories;
    bool initialized = false;
};

// Starts from the frame-0 keys. Advance consumes the same float-seconds
// cursor as the Windows playback path and retains exhausted track state.
// Call Start again to restart or seek backward. Does not model physics,
// runtime bone-type gates, or external-parent transforms.
bool StartPmmScenePlayback(const PmmSceneDocument& scene, PmmScenePlaybackState& state);
bool AdvancePmmScenePlayback(const PmmSceneDocument& scene, float cursorSeconds,
                             PmmScenePlaybackState& state);
bool EvaluatePmmAccessoryTrack(const PmmKeyTrack<PmmAccessoryKey>& track, std::uint8_t slot,
                               double frame, PmmEvaluatedAccessory& output);
bool EvaluatePmmSelfShadowTrack(const PmmKeyTrack<PmmSelfShadowKey>& track, double frame,
                                std::uint8_t& mode, float& distance);
bool EvaluatePmmGravityTrack(const PmmKeyTrack<PmmGravityKey>& track, double frame,
                             PmmEvaluatedGravity& output, bool frameSeek = false);

// Linked display/IK track selection shared by seek, playback start, and
// incremental playback. The caller supplies key links from its own storage.
struct PmmDisplayKeyLink {
    std::uint32_t frame = 0, previous = 0, next = 0;
    std::uint8_t visible = 0;
};
using ReadPmmDisplayKeyLink = bool (*)(void* context, std::uint32_t index, PmmDisplayKeyLink& key);
struct PmmDisplayCursor {
    std::uint32_t index = 0;
    bool active = true;
};
enum class PmmDisplayTraversal {
    Seek,
    InitializePlayback,
    AdvancePlayback
};
enum class PmmDisplaySelectionKind {
    None,
    Terminal,
    Exact,
    Between
};
struct PmmDisplaySelection {
    PmmDisplaySelectionKind kind = PmmDisplaySelectionKind::None;
    std::uint32_t heldIndex = 0, cursorIndex = 0;
    std::uint8_t visible = 0;
};
bool SelectPmmDisplayKey(ReadPmmDisplayKeyLink read, void* context, double frame,
                         PmmDisplayTraversal traversal, PmmDisplayCursor& cursor,
                         PmmDisplaySelection& selection);
using ReadPmmSelectorPair = bool (*)(void* context, std::uint32_t keyIndex,
                                     std::uint32_t selectorIndex, PmmRigidState& pair);
bool EvaluatePmmSelectorWindow(ReadPmmDisplayKeyLink readKey, ReadPmmSelectorPair readPair,
                               void* context, const PmmDisplaySelection& selection,
                               std::uint32_t selectorIndex, PmmEvaluatedSelector& output);
enum class PmmBoneSelectorResult {
    Unchanged,
    PreviousKey,
    NextKey,
    Reset
};
// Selector overrides operate on a bone track after its ordinary key bracket
// is known. Exact key hits are copied by the caller before this step.
PmmBoneSelectorResult SelectPmmBoneSegmentPose(std::uint32_t previousFrame, std::uint32_t nextFrame,
                                               double frame, const PmmEvaluatedSelector& selector);
PmmBoneSelectorResult SelectPmmBoneTerminalPose(std::uint32_t terminalFrame, double frame,
                                                const PmmEvaluatedSelector& selector);
bool ApplyPmmBoneSelectorPose(PmmBoneSelectorResult decision, const PmmBoneKey& previous,
                              const PmmBoneKey& next, bool playback, PmmEvaluatedBone& pose);

// Interpolation kernels shared by the portable scene evaluator and the
// Windows frame-seek adapter. Inputs may come from live edited key records.
float InterpolatePmmMorphValue(float previousValue, float nextValue, std::uint32_t previousFrame,
                               std::uint32_t nextFrame, std::uint32_t frame);
float InterpolatePmmMorphValue(float previousValue, float nextValue, std::uint32_t previousFrame,
                               std::uint32_t nextFrame, double frame);
using PmmBoneEase = float (*)(void* context, int channel, float fraction);

// Shared with live model keys; preserve the reference architecture's curve
// and quaternion rounding without depending on runtime record layouts.
float EvaluatePmmBoneCurve(const std::uint8_t* interpolation, int channel, float fraction);
void InterpolatePmmBoneSeekRotation(const float* previous, const float* next, float fraction,
                                    float* output);

// An adapter can supply its live curve evaluator while sharing interpolation.
bool InterpolatePmmBoneSegment(const PmmBoneKey& previous, const PmmBoneKey& next,
                               std::uint32_t frame, PmmEvaluatedBone& output,
                               PmmBoneEase ease = nullptr, void* context = nullptr);
// Playback uses a floating frame and reciprocal-multiply slerp weights;
// direct seeking uses an integer frame and direct division instead.
bool InterpolatePmmBonePlaybackSegment(const PmmBoneKey& previous, const PmmBoneKey& next,
                                       double frame, PmmEvaluatedBone& output);
enum class PmmLightInterpolation {
    FrameSeek,
    Playback
};
bool EvaluatePmmLightTrack(const PmmKeyTrack<PmmLightKey>&, double frame, PmmLightInterpolation,
                           std::array<float, 6>&);
bool InterpolatePmmLightSegment(const PmmLightKey& previous, const PmmLightKey& next, double frame,
                                PmmLightInterpolation mode, std::array<float, 6>& output);

// Stateless evaluation of the tracks currently ported to the format core.
// Returns false on malformed key links or invalid frame order.
bool EvaluatePmmSceneFrame(const PmmSceneDocument& scene, std::uint32_t frame,
                           PmmSceneEvaluation& evaluation);
enum class PmmCameraInterpolation {
    Seek,
    Playback
};
bool EvaluatePmmCameraTrack(const PmmKeyTrack<PmmCameraKey>& track, double frame,
                            PmmCameraInterpolation mode, PmmEvaluatedCamera& output);
}
