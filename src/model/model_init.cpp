// ===========================================================================
// VA 0x004A8DC0 - ModelInitDefaults  (original: sub_4A8DC0, 1117 bytes)
// VA 0x004A89B0 - ModelInitMorphSlots (original: sub_4A89B0, 522 bytes)
// ===========================================================================
// Default-state initializer of the freshly allocated 0x4CCF4 model block
// (called from 0x460430 add-model, 0x450000 and 0x458F80 scene load).
// Initial values recovered from the x64 constructor and tracking reset.
// Runtime members keep tracking state separate from model-owned resources.
// =========================================================================//
#include <cstdint>
#include <cstring>

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

void ModelInitDefaults(mdl::ModelRecord* record) { // 0x4A8DC0
    mdl::ModelRecord& model = *record;
    model.physicsMode = 0;
    for (auto& text : model.pmxTextBuffers)
        text = nullptr;
    std::memset(&model.pmxReserved, 0, sizeof model.pmxReserved);
    std::memset(&model.gap6, 0, sizeof model.gap6);
    model.reservedMorphTable = nullptr;
    for (std::int32_t& count : model.uvMorphCounts.byFamily)
        count = 0;
    for (auto& table : model.uvMorphTables.byFamily)
        table = nullptr;
    model.materialMorphPools.base = nullptr;
    model.materialMorphPools.additive = nullptr;
    model.materialMorphPools.multiplicative = nullptr;
    model.morph0Count = 0;
    model.morph0Table = nullptr;
    model.boneMorphCount = 0;
    model.boneMorphTable = nullptr;
    model.displayBones = nullptr;
    model.displayGroups = nullptr;
    model.displayFrames = nullptr;
    model.morphs = nullptr;
    model.ikChains = nullptr;
    model.boneTable = nullptr;
    model.materials = nullptr;
    model.indices = nullptr;
    model.rawVertices = nullptr;
    model.pmxVertices = nullptr;
    model.vertexBuffer2 = nullptr;
    model.vertexBuffer = nullptr;
    model.boneKeyCursors = nullptr;
    model.boneTrackActive = nullptr;
    model.morphKeyCursors = nullptr;
    model.morphTrackActive = nullptr;
    model.displayKeyCursor = 0;
    model.displayTrackActive = 0;
    model.displayState = 0;
    model.selectedBone = 0;
    model.boneSelection = nullptr;
    model.bonePhysicsState = nullptr;
    model.loadComplete = 1;
    for (std::int32_t& selectedMorph : model.selectedMorphs)
        selectedMorph = -1;
    model.edgeScale = 1.0f;
    model.boneKeys = nullptr;
    model.morphKeys = nullptr;
    model.displayKeys = nullptr;
    model.boneTimelineRows = nullptr;
    model.morphTimelineRows = nullptr;
    model.boneListPos = 0;
    model.maxFrame = 0;
    model.UndoCursor() = 0;
    model.LatestUndoCursor() = 0;
    model.postLoadFlag2 = 0;
    std::memset(model.undoRings, 0, sizeof(model.undoRings));
    model.undoDirty = 0;
    model.redoDirty = 0;
    model.physicsFlags = 0;
    model.rigidTable = nullptr;
    model.rigidCount = 0;
    model.jointCount = 0;
    model.jointTable = nullptr;
    model.indexBuffer = nullptr;
    model.toonFlag = 1;
    model.currentMaterial = 0xFFFFFFFFu;
    model.displayKeyframesPresent = 0;
    model.boneCount = 0;
    model.morphCount = 0;
    model.ikChainCount = 0;
    model.displayRootBone = 0;
    model.pmxAdditionalUvCount = 0;
    model.maxBoneLayer = 0;
    model.boneOrderTable = nullptr;
    model.centerBone = 0;
    model.frameRegistrationSelection = 3;
    for (auto& position : model.currentJoints.positions)
        position[1] = mdl::kInvalidJointY;

    auto& pose = model.standardPose;
    float* const quaternions[] = {
        pose.upperBody,    pose.neck,          pose.leftArm,    pose.leftWrist, pose.leftElbow,
        pose.rightArm,     pose.rightWrist,    pose.rightElbow, pose.lowerBody, pose.leftLeg,
        pose.leftKnee,     pose.leftFoot,      pose.rightLeg,   pose.rightKnee, pose.rightFoot,
        pose.leftShoulder, pose.rightShoulder,
    };
    for (auto* quaternion : quaternions) {
        quaternion[0] = quaternion[1] = quaternion[2] = 0.0f;
        quaternion[3] = 1.0f;
    }

    ModelInitMorphSlots(record); // 0x4A89B0

    model.poseTraceRecording = 0;
    model.skeletonCalibration.legLength = -1.0f;
    model.skeletonCalibration.heightScale = 90.0f;
    model.skeletonCalibration.heightOffset = 10.0f;
    model.skeletonCalibration.legIkXOffset = 1.0f;
    model.poseTraceBuffer = nullptr;
    model.poseTraceSampleCount = 0;
}

void ModelInitMorphSlots(mdl::ModelRecord* model) { // 0x4A89B0
    // Thirty samples for each of the twenty-three sensor joints.
    // Missing samples use the original y = -999 sentinel.
    for (auto& joint : model->skeletonHistory.joints) {
        for (auto& sample : joint.samples) {
            sample[0] = 0.0f;
            sample[1] = mdl::kInvalidJointY;
            sample[2] = 0.0f;
        }
    }
    model->skeletonCalibration.legLength = -1.0f;
    model->poseTraceRecording = 0;
    model->skeletonCalibration.heightScale = 90.0f;
    model->skeletonCalibration.heightOffset = 10.0f;
    model->skeletonCalibration.legIkXOffset = 1.0f;
}

} // namespace mikudancestudio
