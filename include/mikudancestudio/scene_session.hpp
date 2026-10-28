#pragma once
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include "mikudancestudio/pmm_scene_world.hpp"
#include <functional>
#include <optional>

namespace mikudancestudio::format {
using ResolveSceneModel = bool (*)(void* context, std::uint8_t slot,
                                   const std::array<char, 256>& path, ModelDocument& model);

// Owns a PMM scene, optional parsed model resources, and its evaluated pose.
// V1 requires a host model resolver for PMD layout; v2 can omit it. Paths
// remain byte strings and are never resolved by this library.
class SceneSession {
public:
    static bool Load(ReadPmmBytesResult read, void* readContext, ResolveSceneModel resolveModel,
                     void* modelContext, SceneSession& output);
    bool Seek(std::uint32_t frame);
    bool StartPlayback();
    // Absolute playback cursor in seconds, not a per-frame delta.
    bool Advance(float seconds);
    // Optional host application gate: evaluating another subsystem does not
    // necessarily apply its camera/light tracks. Keep displayed state separate
    // from Pose while legacy playback gates remain in use. Standalone hosts
    // omit these overrides and consume Pose directly.
    bool SetRuntimeAppliedLight(const std::array<float, 6>&);
    const std::optional<std::array<float, 6>>& RuntimeAppliedLight() const {
        return runtimeAppliedLight_;
    }
    bool SetRuntimeAppliedCamera(const PmmEvaluatedCamera&);
    const std::optional<PmmEvaluatedCamera>& RuntimeAppliedCamera() const {
        return runtimeAppliedCamera_;
    }
    // Windows runtime bridge: replace one model's linked key tracks after an
    // editor registration. The original byte envelope is then stale, so Save
    // requires reloading a newly serialized envelope first.
    bool ReplaceRuntimeModelTracks(std::uint8_t slot, PmmKeyTrack<PmmBoneKey> bones,
                                   PmmKeyTrack<PmmMorphKey> morphs,
                                   PmmKeyTrack<PmmDisplayKey> displays);
    bool ReplaceRuntimeCameraTrack(PmmKeyTrack<PmmCameraKey> camera);
    bool ReplaceRuntimeLightTrack(PmmKeyTrack<PmmLightKey> light);
    bool ReplaceRuntimeAccessoryTrack(std::uint8_t slot, PmmKeyTrack<PmmAccessoryKey> accessory);
    bool ReplaceRuntimeSelfShadowTrack(PmmKeyTrack<PmmSelfShadowKey> shadow);
    bool ReplaceRuntimeGravityTrack(PmmKeyTrack<PmmGravityKey> gravity);
    // Validates edits by writing and reparsing a private copy before commit.
    // Existing key values and current poses can be edited; track topology is
    // fixed by the PMM writer.
    bool Edit(const std::function<void(PmmSceneDocument&)>& change);
    bool Save(WritePmmBytes write, void* context) const;
    bool KinematicWorld(std::uint8_t slot,
                        std::vector<std::optional<std::array<float, 16>>>& world) const;
    bool CameraAttachmentPoint(std::array<float, 3>& point) const;
    bool AccessoryWorld(std::uint8_t slot, PmmWorldMatrix& world) const;

    const PmmSceneDocument& Document() const { return scene_; }
    const PmmSceneEvaluation& Pose() const { return pose_; }
    const PmmScenePlaybackState& Playback() const { return playback_; }
    const std::vector<std::optional<ModelDocument>>& Models() const { return models_; }
    bool Loaded() const { return loaded_; }
    bool Playing() const { return playing_; }
    bool RuntimeTracksChanged() const { return runtimeTracksChanged_; }

private:
    PmmSceneDocument scene_;
    std::vector<std::optional<ModelDocument>> models_;
    PmmSceneEvaluation pose_;
    PmmScenePlaybackState playback_;
    std::optional<std::array<float, 6>> runtimeAppliedLight_;
    std::optional<PmmEvaluatedCamera> runtimeAppliedCamera_;
    bool loaded_ = false, playing_ = false;
    bool runtimeTracksChanged_ = false;
};
}
