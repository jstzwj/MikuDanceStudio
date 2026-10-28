// Rebuild a model pose at an arbitrary frame, in display/morph/bone order.
// Architecture-specific expressions preserve the original rounding stages.
// Architecture and numerical compatibility rules: docs/X64_RECONSTRUCTION.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"

#include "keyframe_common.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include "mikudancestudio/scene_timeline_bridge.hpp"

namespace mikudancestudio {
namespace {

using kfa::CopyBoneKeyVerbatim;
using kfa::CopyIkDisplayFlags;
using kfa::MirrorBackupsToCurrent;
using kfa::MirrorBackupsToWorking;

using kfa::WriteBonePrevVerbatim;

inline void InterpolateSeekRotation(mdl::BoneRecord* bone, const float previousRotation[4],
                                    const float currentRotation[4], float rotationFraction) {
    format::InterpolatePmmBoneSeekRotation(previousRotation, currentRotation, rotationFraction,
                                           bone->rotQuat);
}

void SeekDisplayTrack(mdl::ModelRecord& model, std::uint32_t frameU32) {
    auto* m = &model;
    {
        mdl::DisplayKey* const keys = (m)->displayKeys;
        format::PmmDisplayCursor cursor;
        format::PmmDisplaySelection selection;
        if (format::SelectPmmDisplayKey(kfa::ReadLiveDisplayKeyLink, keys,
                                        static_cast<double>(frameU32),
                                        format::PmmDisplayTraversal::Seek, cursor, selection))
            kfa::ApplyDisplaySelection(m, keys, selection);
    }
}

int SeekMorphTracks(mdl::ModelRecord& model, std::uint32_t frameU32) {
    int result = 0;
    const int morphCnt = static_cast<int>(model.morphCount);
    if (morphCnt > 0) {
        mdl::MorphKey* const keys = model.morphKeys;
        mikudancestudio::mdl::MorphRecord* const vals = model.morphs; // 136-byte structs
        for (int k = 0; k < morphCnt; ++k) {
            int found = k; // morph k's first record is #k
            if (keys[found].frame < frameU32) {
                bool resolved = false;
                for (;;) {
                    const int next = static_cast<int>(keys[found].next);
                    if (next == 0)
                        break;
                    found = next;
                    if (keys[found].frame >= frameU32) {
                        resolved = true;
                        break;
                    }
                }
                if (!resolved) {
                    vals[k].value = keys[found].value;
                    result = k + 1;
                    continue;
                }
            }
            const mdl::MorphKey& rec = keys[found];
            float v;
            if (rec.frame == frameU32) {
                v = rec.value;
            } else {
                const mdl::MorphKey& prev = keys[rec.previous];
#if defined(_M_IX86)
                // Round the fraction and delta to float before the double multiply/add.
                const float tF = (float)((double)(frameU32 - prev.frame) /
                                         (double)(std::uint32_t)(rec.frame - prev.frame));
                const float delta = rec.value - prev.value;
                v = (float)((double)tF * (double)delta + (double)prev.value);
#else
                // Keep subtraction, division and interpolation in single precision.
                const float tF =
                    static_cast<float>(frameU32 - prev.frame) /
                    static_cast<float>(static_cast<int>(rec.frame) - static_cast<int>(prev.frame));
                v = tF * (rec.value - prev.value) + prev.value;
#endif
            }
            vals[k].value = v;
            result = k + 1;
        }
    }
    return result;
}

void SeekBoneFrame(mdl::ModelRecord* m, mdl::BoneRecord* bone, mdl::BoneKey* keys, int b,
                   std::uint32_t frameU32, int physicsMode) {
    int found = b; // bone b's first record is #b
    bool resolved = false;
    if (keys[found].frame < frameU32) {
        for (;;) {
            const int next = static_cast<int>(keys[found].next);
            if (next == 0)
                break;
            found = next;
            if (keys[found].frame >= frameU32) {
                resolved = true;
                break;
            }
        }
        if (!resolved) {
            const mdl::BoneKey& rec = keys[found];
            for (int c = 0; c < 4; ++c)
                bone->rotQuat[c] = rec.rotation[c];
            for (int c = 0; c < 3; ++c)
                bone->trans[c] = rec.position[c];
            const unsigned char mode = rec.physicsDisabled;
            if (bone->hasRigidBody != 0 && bone->physicsDisabled != mode && physicsMode >= 2) {
                NotifyBonePhysicsMode(m, b, mode);
                bone->physicsDisabled = rec.physicsDisabled;
            }
            const int selIdx = bone->slotIndex;
            if (selIdx >= 0) {
                const mdl::BoneOrderEntry& window = m->boneOrderTable[selIdx];
                const auto decision = format::SelectPmmBoneTerminalPose(
                    rec.frame, frameU32, kfa::PortableSelector(window));
                kfa::ApplyLiveBoneSelectorPose(bone, decision, rec, rec, false);
            }
            return;
        }
    } else {
        resolved = true;
    }

    const mdl::BoneKey& rec = keys[found];
    const std::uint32_t curFrame = rec.frame;

    if (curFrame == frameU32) {
        CopyBoneKeyVerbatim(bone, rec);
        if (bone->hasRigidBody != 0) {
            if (physicsMode != 3) {
                const unsigned char mode = rec.physicsDisabled;
                if (bone->physicsDisabled != mode && physicsMode == 2) {
                    NotifyBonePhysicsMode(m, b, mode);
                    bone->physicsDisabled = rec.physicsDisabled;
                }
                return;
            }
            const int nextIdx = static_cast<int>(rec.next);
            const unsigned char mode = rec.physicsDisabled;
            if (nextIdx <= 0) {
                if (bone->physicsDisabled != mode)
                    NotifyBonePhysicsMode(m, b, mode);
                bone->physicsDisabled = rec.physicsDisabled;
                return;
            }
            const mdl::BoneKey& nrec = keys[nextIdx];
            if (nrec.physicsDisabled != 1 || mode != 0) {
                if (bone->physicsDisabled != mode)
                    NotifyBonePhysicsMode(m, b, mode);
                bone->physicsDisabled = rec.physicsDisabled;
                return;
            }
            // arm the interpolation for the upcoming segment
            // backups
            if (bone->physicsDisabled == 0)
                NotifyBonePhysicsMode(m, b, 1);
            bone->physicsDisabled = 1;
            bone->rigidIdx = static_cast<std::int32_t>(curFrame);
            MirrorBackupsToWorking(bone);
            MirrorBackupsToCurrent(bone);
        }
        return;
    }

    const int prevIdx = static_cast<int>(rec.previous);
    const mdl::BoneKey& prevRec = keys[prevIdx];
    std::uint32_t prevFrame = prevRec.frame;
    float previousPosition[3] = {prevRec.position[0], prevRec.position[1], prevRec.position[2]};
    float previousRotation[4] = {prevRec.rotation[0], prevRec.rotation[1], prevRec.rotation[2],
                                 prevRec.rotation[3]};

    if (bone->hasRigidBody != 0) {
        if (physicsMode == 3) {
            if (rec.physicsDisabled == 1 && prevRec.physicsDisabled == 0) {
                const std::uint32_t start = static_cast<std::uint32_t>(bone->rigidIdx);
                if (start >= curFrame || start < prevFrame) {
                    if (bone->physicsDisabled == 0)
                        NotifyBonePhysicsMode(m, b, 1);
                    bone->physicsDisabled = 1;
                    bone->rigidIdx = static_cast<std::int32_t>(prevFrame);
                    MirrorBackupsToWorking(bone);
                }
                if (bone->physicsDisabled == 0)
                    NotifyBonePhysicsMode(m, b, 1);
                bone->physicsDisabled = 1;
                previousRotation[0] = bone->ikWorkingQuat[0];
                previousRotation[1] = bone->ikWorkingQuat[1];
                previousRotation[2] = bone->ikWorkingQuat[2];
                previousRotation[3] = bone->ikWorkingQuat[3];
                previousPosition[0] = bone->ikWorkingPos[0];
                previousPosition[1] = bone->ikWorkingPos[1];
                previousPosition[2] = bone->ikWorkingPos[2];
                prevFrame = static_cast<std::uint32_t>(bone->rigidIdx);
            } else {
                const unsigned char pmode = prevRec.physicsDisabled;
                if (bone->physicsDisabled != pmode)
                    NotifyBonePhysicsMode(m, b, pmode);
                bone->physicsDisabled = prevRec.physicsDisabled;
            }
        } else {
            const unsigned char pmode = prevRec.physicsDisabled;
            if (bone->physicsDisabled != pmode && physicsMode == 2) {
                NotifyBonePhysicsMode(m, b, pmode);
                bone->physicsDisabled = prevRec.physicsDisabled;
            }
        }
    }

    // Selector-window crossing overrides this segment's interpolation.
    const int selIdx = bone->slotIndex;
    if (selIdx >= 0) {
        const mdl::BoneOrderEntry& window = (m)->boneOrderTable[selIdx];
        const auto decision = format::SelectPmmBoneSegmentPose(prevFrame, curFrame, frameU32,
                                                               kfa::PortableSelector(window));
        if (kfa::ApplyLiveBoneSelectorPose(bone, decision, prevRec, rec, false, previousPosition,
                                           previousRotation)) {
            return;
        }
    }

#if defined(_M_X64)
    // The portable interpolation kernel consumes the current live
    // key pair. Selector snapping and physics preparation above
    // remain in the host; edited keys take effect on the next seek.
    format::PmmBoneKey left{}, right{};
    left.frame = prevFrame;
    right.frame = curFrame;
    std::memcpy(left.position.data(), previousPosition, sizeof previousPosition);
    std::memcpy(left.rotation.data(), previousRotation, sizeof previousRotation);
    std::memcpy(right.position.data(), rec.position, sizeof rec.position);
    std::memcpy(right.rotation.data(), rec.rotation, sizeof rec.rotation);
    std::memcpy(right.interpolation.data(), rec.interpolation, sizeof rec.interpolation);
    format::PmmEvaluatedBone blended;
    struct LiveCurve {
        const mdl::ModelRecord* model;
        int key;
    } curve{m, found};
    const auto ease = [](void* context, int channel, float fraction) {
        const auto& curve = *static_cast<const LiveCurve*>(context);
        return BoneEase(curve.model, channel, curve.key, fraction);
    };
    if (format::InterpolatePmmBoneSegment(left, right, frameU32, blended, ease, &curve)) {
        std::memcpy(bone->trans, blended.translation.data(), sizeof(float) * 3);
        std::memcpy(bone->rotQuat, blended.rotation.data(), sizeof(float) * 4);
        return;
    }
#endif

    // the slerp, raw fraction eases each position axis
    const float currentRotation[4] = {rec.rotation[0], rec.rotation[1], rec.rotation[2],
                                      rec.rotation[3]};
    // Form the fraction in single precision before applying each curve.
    const float tF = static_cast<float>(frameU32 - prevFrame) /
                     static_cast<float>(static_cast<int>(curFrame) - static_cast<int>(prevFrame));
    const float rotationFraction = BoneEase(m, 3, found, tF);

    InterpolateSeekRotation(bone, previousRotation, currentRotation, rotationFraction);

    for (int axis = 0; axis < 3; ++axis) {
        const float delta = rec.position[axis] - previousPosition[axis];
        if (delta == 0.0f) {
            bone->trans[axis] = previousPosition[axis];
        } else {
            const float e = BoneEase(m, axis, found, tF);
#if defined(_M_IX86)
            bone->trans[axis] = (float)((double)e * (double)delta + (double)previousPosition[axis]);
#else
            // x64 RVA 0xCD5E8/0xCD5EC: round the product before adding.
            bone->trans[axis] = e * delta + previousPosition[axis];
#endif
        }
    }
}

} // namespace

int SeekModelFrame(mdl::ModelRecord& model, int frameArg, int physicsMode) {
    const auto frame = static_cast<std::uint32_t>(frameArg);
    SeekDisplayTrack(model, frame);
    int result = SeekMorphTracks(model, frame);
    const int count = static_cast<int>(model.boneCount);
    for (int index = 0; index < count; ++index) {
        mdl::BoneRecord& bone = model.boneTable[index];
        // Preserve the signed-byte comparison, including negative type bytes.
        const auto type = static_cast<std::int8_t>(bone.type);
        if (type > static_cast<std::int8_t>(mdl::BoneType::Effector) &&
            bone.type != mdl::BoneType::FixedAxis)
            continue;
        SeekBoneFrame(&model, &bone, model.boneKeys, index, frame, physicsMode);
        result = index + 1;
    }
    SeekPortableSceneModel(&model, frameArg);
    return result;
}

// Seek the selected model and request reevaluation.
void SeekSelectedModelToCurrentFrame(MMDApp* app) {
    mdl::ModelRecord* model = app->SelectedModel();
    SeekModelFrame(*model, app->state.currentFrame, app->PlaybackPhysicsMode());
    app->RecordedFrameCountByte1() = 1;
}

} // namespace mikudancestudio
