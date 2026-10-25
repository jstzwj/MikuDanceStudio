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

namespace mikudancestudio {
namespace {

using kfa::CopyBoneKeyVerbatim;
using kfa::CopyIkDisplayFlags;
using kfa::MirrorBackupsToCurrent;
using kfa::MirrorBackupsToWorking;
using kfa::RefreshIkSelectors;
using kfa::WriteBonePrevVerbatim;

// Match the original float pi constant promoted to double, then halved.
constexpr double kSeekHalfPi = static_cast<double>(3.141592f) * 0.5;
static_assert(__builtin_bit_cast(std::uint64_t, kSeekHalfPi) == 0x3FF921FB00000000ULL,
              "pi/2 double constant bit-exact");

// Keep the original clamp inside the acos domain.
const float kQuaternionClamp = 0.999999f;

inline void InterpolateSeekRotation(mdl::BoneRecord* bone, const float previousRotation[4],
                                    const float currentRotation[4], float rotationFraction) {
#if defined(_M_IX86)
    // Preserve float rounding between the double-precision stages.
    const double dotDouble = (double)previousRotation[0] * currentRotation[0] +
                             (double)previousRotation[1] * currentRotation[1] +
                             (double)previousRotation[2] * currentRotation[2] +
                             (double)previousRotation[3] * currentRotation[3];
    const float dot = (float)dotDouble;
    if (static_cast<float>(1.0 - (double)dot * (double)dot) == 0.0f) {
        for (int c = 0; c < 4; ++c)
            bone->rotQuat[c] = previousRotation[c];
    } else {
        float clampedDot = dot;
        if (clampedDot > 1.0f)
            clampedDot = kQuaternionClamp;
        else if (clampedDot < -1.0f)
            clampedDot = -kQuaternionClamp;
        const float angle = (float)std::acos((double)clampedDot);
        if ((double)angle > kSeekHalfPi && (double)clampedDot < 0.0) {
            const float shortAngle = (float)std::acos(-(double)clampedDot);
            const float s = (float)std::sin((double)shortAngle);
            const float a0 = (float)((1.0 - (double)rotationFraction) * (double)shortAngle);
            const float previousSine = (float)std::sin((double)a0);
            const float previousWeight = (float)((double)previousSine / (double)s);
            const float endAngle = (float)((double)rotationFraction * (double)shortAngle);
            const float nextSine = (float)std::sin((double)endAngle);
            const float nextWeight = (float)((double)nextSine / (double)s);
            for (int c = 0; c < 4; ++c)
                bone->rotQuat[c] = (float)((double)previousRotation[c] * (double)previousWeight -
                                           (double)nextWeight * (double)currentRotation[c]);
        } else {
            const float s = (float)std::sin((double)angle);
            const float a0 = (float)((1.0 - (double)rotationFraction) * (double)angle);
            const float previousSine = (float)std::sin((double)a0);
            const float previousWeight = (float)((double)previousSine / (double)s);
            const float endAngle = (float)((double)rotationFraction * (double)angle);
            const float nextSine = (float)std::sin((double)endAngle);
            const float nextWeight = (float)((double)nextSine / (double)s);
            for (int c = 0; c < 4; ++c)
                bone->rotQuat[c] = (float)((double)previousRotation[c] * (double)previousWeight +
                                           (double)nextWeight * (double)currentRotation[c]);
        }
    }
#else
    const float dot =
        previousRotation[0] * currentRotation[0] + previousRotation[1] * currentRotation[1] +
        previousRotation[2] * currentRotation[2] + previousRotation[3] * currentRotation[3];
    if (1.0f - dot * dot == 0.0f) {
        for (int c = 0; c < 4; ++c)
            bone->rotQuat[c] = previousRotation[c];
    } else {
        float clampedDot = dot;
        if (clampedDot > 1.0f)
            clampedDot = kQuaternionClamp;
        else if (clampedDot < -1.0f)
            clampedDot = -kQuaternionClamp;
        const float angle = std::acos(clampedDot);
        if (angle > 1.570796012878418f && clampedDot < 0.0f) {
            const float shortAngle = std::acos(-clampedDot);
            const float s = std::sin(shortAngle);
            const float previousWeight = std::sin((1.0f - rotationFraction) * shortAngle) / s;
            const float nextWeight = std::sin(rotationFraction * shortAngle) / s;
            for (int c = 0; c < 4; ++c)
                bone->rotQuat[c] =
                    previousRotation[c] * previousWeight - nextWeight * currentRotation[c];
        } else {
            const float s = std::sin(angle);
            const float previousWeight = std::sin((1.0f - rotationFraction) * angle) / s;
            const float nextWeight = std::sin(rotationFraction * angle) / s;
            for (int c = 0; c < 4; ++c)
                bone->rotQuat[c] =
                    previousRotation[c] * previousWeight + nextWeight * currentRotation[c];
        }
    }
#endif
}

void SeekDisplayTrack(mdl::ModelRecord& model, std::uint32_t frameU32) {
    auto* m = &model;
    mdl::DisplayKey* const keys = model.displayKeys;
    int found = 0;
    bool resolved = false;
    if (keys[0].frame < frameU32) {
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
            model.loadComplete = keys[found].visible;
            CopyIkDisplayFlags(m, keys[found]);
            RefreshIkSelectors(m, keys, found, found, false, 0, 0);
        }
    } else {
        resolved = true; // requested frame is at/before the first record
    }
    if (resolved) {
        if (keys[found].frame == frameU32) {
            model.loadComplete = keys[found].visible;
            CopyIkDisplayFlags(m, keys[found]);
            RefreshIkSelectors(m, keys, found, found, true, found, found);
        } else {
            // Between keys, apply the previous state and inspect the found key.
            const int prevIdx = static_cast<int>(keys[found].previous);
            model.loadComplete = keys[prevIdx].visible;
            CopyIkDisplayFlags(m, keys[prevIdx]);
            RefreshIkSelectors(m, keys, prevIdx, prevIdx, true, found, found);
        }
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
                const std::uint32_t selStart = window.windowStart;
                if (rec.frame < selStart && selStart <= frameU32) {
                    // zero position + quaternion xyz, w = 1.0
                    for (int axis = 0; axis < 3; ++axis)
                        bone->trans[axis] = 0.0f;
                    for (int axis = 0; axis < 3; ++axis)
                        bone->rotQuat[axis] = 0.0f;
                    bone->rotQuat[3] = 1.0f;
                }
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

    // the short-circuit - selStart when the start-window test fails,
    bool doSnap = false;
    std::uint32_t snapRef = 0;
    const int selIdx = bone->slotIndex;
    if (selIdx >= 0) {
        const mdl::BoneOrderEntry& window = m->boneOrderTable[selIdx];
        const std::uint32_t selStart = window.windowStart;
        const std::uint32_t selEnd = window.windowEnd;
        if (selStart < curFrame && prevFrame < selStart) {
            doSnap = true;
            snapRef = selStart;
        } else if (!(curFrame < selEnd || prevFrame >= selEnd)) {
            doSnap = true;
            snapRef = selEnd;
        }
    }
    if (doSnap) {
        if (frameU32 >= snapRef)
            CopyBoneKeyVerbatim(bone, rec);
        else
            WriteBonePrevVerbatim(bone, previousRotation, previousPosition);
        return;
    }

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
    return result;
}

// Seek the selected model and request reevaluation.
void SeekSelectedModelToCurrentFrame(MMDApp* app) {
    mdl::ModelRecord* model = app->SelectedModel();
    SeekModelFrame(*model, app->state.currentFrame, app->PlaybackPhysicsMode());
    app->RecordedFrameCountByte1() = 1;
}

} // namespace mikudancestudio
