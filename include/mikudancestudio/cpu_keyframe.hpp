#pragma once
#include "mikudancestudio/cpu_bone_types.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include "btBulletDynamicsCommon.h"
#include <cstring>
#include <cmath>
namespace mikudancestudio::cpu_keyframe {
template <class Traits> struct Kernel {
    using Model = typename Traits::Model;
    using Bone = typename Traits::Bone;
    using BoneKey = typename Traits::BoneKey;
    using Morph = typename Traits::Morph;
    using MorphKey = typename Traits::MorphKey;
    using DisplayKey = typename Traits::DisplayKey;
    using IkChain = typename Traits::IkChain;
    using Binding = typename Traits::Binding;
    using Rigid = typename Traits::Rigid;
#if defined(_M_IX86)
    using KeyFrameTime = double;
#else
    using KeyFrameTime = float;
#endif
    static constexpr float kQuatClamp = 0.999999f;
    static double PiHalfBits() { return static_cast<double>(3.141592f) * .5; }
    static inline void CopyIkDisplayFlags(Model* m, const DisplayKey& rec) {
        const auto* flags = Traits::IkStates(rec);
        IkChain* const chains = m->ikChains;
        const std::uint32_t count = m->ikChainCount;
        for (std::uint32_t k = 0; k < count; ++k)
            chains[k].enabled = flags[k];
    }

    static inline bool ReadLiveDisplayKeyLink(void* context, std::uint32_t index,
                                              format::PmmDisplayKeyLink& output) {
        if (!context || index >= 1000)
            return false;
        const auto& key = static_cast<const DisplayKey*>(context)[index];
        output = {key.frame, key.previous, key.next, key.visible};
        return true;
    }

    static inline bool ReadLiveSelectorPair(void* context, std::uint32_t keyIndex,
                                            std::uint32_t selectorIndex,
                                            format::PmmRigidState& output) {
        if (!context || keyIndex >= 1000)
            return false;
        const auto* keys = static_cast<const DisplayKey*>(context);
        if (!Traits::SelectorStates(keys[keyIndex]))
            return false;
        const auto& pair = Traits::SelectorStates(keys[keyIndex])[selectorIndex];
        output = {pair.modelIndex, pair.boneIndex};
        return true;
    }

    static inline void ApplyDisplaySelection(Model* m, DisplayKey* keys,
                                             const format::PmmDisplaySelection& selection) {
        using Kind = format::PmmDisplaySelectionKind;
        if (selection.kind == Kind::None)
            return;
        m->loadComplete = selection.visible;
        CopyIkDisplayFlags(m, keys[selection.heldIndex]);
        Binding* const selectors = (m)->boneOrderTable;
        const auto count = m->boneOrderCount;
        for (std::uint32_t i = 0; i < count; ++i) {
            format::PmmEvaluatedSelector window;
            if (!format::EvaluatePmmSelectorWindow(ReadLiveDisplayKeyLink, ReadLiveSelectorPair,
                                                   keys, selection, i, window))
                continue;
            auto& selector = selectors[i];
            selector.linkedModel = window.linkedModel;
            selector.linkedBone = window.linkedBone;
            selector.windowStart = window.windowStart;
            selector.windowEnd = window.windowEnd;
        }
    }

    static inline void CopyBoneKeyVerbatim(Bone* bone, const BoneKey& rec) {
        for (int c = 0; c < 4; ++c)
            bone->rotQuat[c] = rec.rotation[c];
        for (int c = 0; c < 3; ++c)
            bone->trans[c] = rec.position[c];
    }

    static inline void WriteBonePrevVerbatim(Bone* bone, const float pq[4], const float ppos[3]) {
        for (int c = 0; c < 4; ++c)
            bone->rotQuat[c] = pq[c];
        for (int c = 0; c < 3; ++c)
            bone->trans[c] = ppos[c];
    }

    static inline format::PmmEvaluatedSelector PortableSelector(const Binding& window) {
        return {window.linkedModel, window.linkedBone, window.windowStart, window.windowEnd};
    }

    static inline format::PmmBoneKey PortableBoneKey(const BoneKey& key) {
        format::PmmBoneKey result{};
        result.frame = key.frame;
        std::memcpy(result.position.data(), key.position, sizeof key.position);
        std::memcpy(result.rotation.data(), key.rotation, sizeof key.rotation);
        return result;
    }

    static inline bool ApplyLiveBoneSelectorPose(Bone* bone, format::PmmBoneSelectorResult decision,
                                                 const BoneKey& previous, const BoneKey& next,
                                                 bool playback,
                                                 const float previousPosition[3] = nullptr,
                                                 const float previousRotation[4] = nullptr) {
        if (decision == format::PmmBoneSelectorResult::Unchanged)
            return false;
        auto left = PortableBoneKey(previous);
        const auto right = PortableBoneKey(next);
        if (previousPosition)
            std::memcpy(left.position.data(), previousPosition, sizeof(float) * 3);
        if (previousRotation)
            std::memcpy(left.rotation.data(), previousRotation, sizeof(float) * 4);
        format::PmmEvaluatedBone pose;
        if (!format::ApplyPmmBoneSelectorPose(decision, left, right, playback, pose))
            return false;
        std::memcpy(bone->trans, pose.translation.data(), sizeof(float) * 3);
        std::memcpy(bone->rotQuat, pose.rotation.data(), sizeof(float) * 4);
        return true;
    }

    static inline void MirrorBackupsToWorking(Bone* bone) {
        for (int c = 0; c < 4; ++c)
            bone->ikWorkingQuat[c] = bone->ikBackup[3 + c];
        for (int c = 0; c < 3; ++c)
            bone->ikWorkingPos[c] = bone->ikBackup[c];
    }

    static inline void MirrorBackupsToCurrent(Bone* bone) {
        for (int c = 0; c < 4; ++c)
            bone->rotQuat[c] = bone->ikBackup[3 + c];
        for (int c = 0; c < 3; ++c)
            bone->trans[c] = bone->ikBackup[c];
    }

    static float BoneEase(const Model* model, int channel, int keyIdx, float t) {
        return format::EvaluatePmmBoneCurve(model->boneKeys[keyIdx].interpolation, channel, t);
    }

    static void NotifyBonePhysicsMode(Model* model, int boneIdx, unsigned char mode) {
        Rigid* const rigids = model->rigidTable;
        const int cnt = static_cast<int>(model->rigidCount);
        for (int i = 0; i < cnt; ++i) {
            Rigid* const r = &rigids[i];
            if (r->staticFlag != 0 || r->boneIndex != boneIdx)
                continue;
            btRigidBody* const body = r->body;
            if (body == nullptr)
                continue;

            const int flags = body->getCollisionFlags();
            if (mode != 0) {
                body->setCollisionFlags(flags | btCollisionObject::CF_KINEMATIC_OBJECT);
                r->mode = 0;
            } else {
                body->setCollisionFlags(flags & ~btCollisionObject::CF_KINEMATIC_OBJECT);
                r->mode = r->kinematicFlag != 0 ? 2 : 1;
            }
            // Verified against x64: the entire 0x7FF7CB4E3140 function (and the inline Advance sections at
            // 0x7FF7CB4F15EA / 0x7FF7CB4F1780) only write the btRigidBody
            // +0xE0 CF_KINEMATIC bit (or/and 2) and mode byte, with no
            // activation call. The port's previous
            // setActivationState(DISABLE_DEACTIVATION) was removed to match x64.
        }
    }

    static void ApplyModelBonePhysicsMode(Model* model, int mode) {
        const int boneCount = model->boneCount; // 0x4B2218
        Bone* bones = model->boneTable;
        if (mode == 0) {
            for (int i = 0; boneCount > 0 && i < boneCount; ++i) // 0x4B221A
                if (bones[i].hasRigidBody != 0)
                    NotifyBonePhysicsMode(model, i, 1); // 0x4B2243
        } else if (mode == 1) {
            for (int i = 0; boneCount > 0 && i < boneCount; ++i) // 0x4B2263
                if (bones[i].hasRigidBody != 0)
                    NotifyBonePhysicsMode(model, i, 0); // 0x4B2283
        } else if (mode >= 2) {
            for (int i = 0; boneCount > 0 && i < boneCount; ++i) { // 0x4B22A3
                Bone* bone = &bones[i];
                if (bone->hasRigidBody != 0)
                    NotifyBonePhysicsMode(model, i, bone->physicsDisabled != 0); // 0x4B22D1
            }
        }
    }

    static void InitModelTrackCursors(Model* model, float cursor, int physicsMode) {
        Model* const m = model;
        // Both originals round seconds * 30 through a float before track comparisons.
        const float frame = cursor * 30.0f;

        // IK/display master track.  This is a single linked list rooted at 0.
        DisplayKey* const ikKeys = (m)->displayKeys;
        Model& state = *m;
        format::PmmDisplayCursor displayCursor;
        format::PmmDisplaySelection displaySelection;
        if (format::SelectPmmDisplayKey(ReadLiveDisplayKeyLink, ikKeys, frame,
                                        format::PmmDisplayTraversal::InitializePlayback,
                                        displayCursor, displaySelection)) {
            state.displayKeyCursor = displayCursor.index;
            state.displayTrackActive = displayCursor.active ? 1 : 0;
            ApplyDisplaySelection(m, ikKeys, displaySelection);
        }

        // Morph tracks are independently rooted at their morph index.
        const int morphCount = static_cast<int>(m->morphCount);
        MorphKey* const morphKeys = m->morphKeys;
        Morph* const morphValues = m->morphs;
        unsigned char* const morphActive = state.morphTrackActive;
        std::uint32_t* const morphCursor = state.morphKeyCursors;
        for (int morph = 0; morph < morphCount; ++morph) {
            morphActive[morph] = 1;
            morphCursor[morph] = static_cast<std::uint32_t>(morph);
            for (;;) {
                const int cur = static_cast<int>(morphCursor[morph]);
                if (static_cast<KeyFrameTime>(morphKeys[cur].frame) >= frame)
                    break;
                const int next = static_cast<int>(morphKeys[cur].next);
                if (next != 0) {
                    morphCursor[morph] = static_cast<std::uint32_t>(next);
                    continue;
                }
                morphActive[morph] = 0;
                morphValues[morph].value = morphKeys[cur].value;
                break;
            }
        }

        // Bone tracks are independently rooted at their bone index.  The start
        // initializer excludes only type 7, exactly as 0x4A301F does.
        const int boneCount = static_cast<int>(m->boneCount);
        BoneKey* const boneKeys = m->boneKeys;
        Bone* const bones = m->boneTable;
        unsigned char* const boneActive = state.boneTrackActive;
        std::uint32_t* const boneCursor = state.boneKeyCursors;
        for (int boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
            Bone* const bone = &bones[boneIndex];
            boneActive[boneIndex] = 1;
            if (bone->type == cpu_bone::BoneType::InertTip) {
                boneActive[boneIndex] = 0;
                continue;
            }
            boneCursor[boneIndex] = static_cast<std::uint32_t>(boneIndex);
            for (;;) {
                const int cur = static_cast<int>(boneCursor[boneIndex]);
                const BoneKey& rec = boneKeys[cur];
                if (static_cast<KeyFrameTime>(rec.frame) >= frame)
                    break;
                const int next = static_cast<int>(rec.next);
                if (next != 0) {
                    boneCursor[boneIndex] = static_cast<std::uint32_t>(next);
                    continue;
                }
                boneActive[boneIndex] = 0;
                std::memcpy(bone->rotQuat, rec.rotation, sizeof rec.rotation);
                std::memcpy(bone->trans, rec.position, sizeof rec.position);
                break;
            }

            const int cur = static_cast<int>(boneCursor[boneIndex]);
            const unsigned char mode = boneKeys[cur].physicsDisabled;
            if (bone->hasRigidBody != 0) {
                if (physicsMode >= 2)
                    NotifyBonePhysicsMode(m, boneIndex, mode);
                bone->physicsDisabled = mode;
            }
        }
    }

    static void AdvanceModelKeyframes(Model* model, float cursor, int physicsMode) {
        Model* const m = model;
        Model& state = *m;

        // x64 RVA 0xD08EC..0xD0925 uses mulss/divss throughout. x86
        // 0x4A31E1/0x4A31F7/0x4A322A stores both frame and scaled as floats;
        // its integer/1000 division stays on the x87 stack until the float store.
        float frame = cursor * 30.0f;
        const float scaled = frame * 1000.0f;
        const int whole = static_cast<int>(scaled);
        if (scaled - static_cast<float>(whole) >= 0.5f) {
#if defined(_M_IX86)
            frame = static_cast<float>(static_cast<double>(whole + 1) / 1000.0);
#else
            frame = static_cast<float>(whole + 1) / 1000.0f;
#endif
        }

        // ==== section 1: IK master tracks (0x4A3236) ==========================
        if (state.displayTrackActive != 0) {
            DisplayKey* const keys = (m)->displayKeys;
            format::PmmDisplayCursor cursor{state.displayKeyCursor, true};
            format::PmmDisplaySelection selection;
            if (format::SelectPmmDisplayKey(ReadLiveDisplayKeyLink, keys, frame,
                                            format::PmmDisplayTraversal::AdvancePlayback, cursor,
                                            selection)) {
                state.displayKeyCursor = cursor.index;
                state.displayTrackActive = cursor.active ? 1 : 0;
                ApplyDisplaySelection(m, keys, selection);
            }
        }

        // ==== section 2: morph tracks (0x4A391B) ==============================
        const int morphCnt = static_cast<int>(m->morphCount);
        if (morphCnt > 0) {
            MorphKey* const keys = m->morphKeys;
            Morph* const vals = m->morphs; // 136-byte structs
            unsigned char* const active = state.morphTrackActive;
            std::uint32_t* const cursors = state.morphKeyCursors;
            for (int k = 0; k < morphCnt; ++k) {
                if (active[k] == 0)
                    continue;
                while (static_cast<KeyFrameTime>(keys[cursors[k]].frame) < frame &&
                       active[k] != 0) {
                    const MorphKey& rec = keys[cursors[k]];
                    const int next = static_cast<int>(rec.next);
                    if (next != 0)
                        cursors[k] = next;
                    else {
                        active[k] = 0;
                        vals[k].value = rec.value;
                    }
                }
                if (active[k] == 0)
                    continue;
                const MorphKey& rec = keys[cursors[k]];
                float v;
                if (frame == static_cast<KeyFrameTime>(rec.frame)) {
                    v = rec.value;
                } else {
                    const MorphKey& prev = keys[rec.previous];
#if defined(_M_IX86)
                    // x86 0x4A3A31..0x4A3A8A: t and delta each round through a
                    // float stack slot (fstp/fld), then the product+add runs on
                    // the x87 stack (extended precision) and rounds once at the
                    // final store - modelled with doubles like the rest of the
                    // x86 branches in this port.
                    const float tF = (float)((frame - (double)prev.frame) /
                                             (double)(std::uint32_t)(rec.frame - prev.frame));
                    const float delta = rec.value - prev.value;
                    v = (float)((double)tF * (double)delta + (double)prev.value);
#else
                    v = format::InterpolatePmmMorphValue(prev.value, rec.value, prev.frame,
                                                         rec.frame, frame);
#endif
                }
                vals[k].value = v;
            }
        }

        // ==== section 3: bone tracks (0x4A3AA5) ===============================
        const int boneCnt = static_cast<int>(m->boneCount);
        if (boneCnt > 0) {
            BoneKey* const keys = m->boneKeys;
            Bone* const bones = m->boneTable;
            unsigned char* const active = state.boneTrackActive;
            std::uint32_t* const cursors = state.boneKeyCursors;
            for (int b = 0; b < boneCnt; ++b) {
                Bone* const bone = &bones[b];
                if (active[b] == 0)
                    continue;

                // advance (0x4A3ACA) - MIKUDANCESTUDIO_SPIN_CANARY guards a record-
                // link cycle (diagnostic; the original chain is acyclic)
#ifdef MIKUDANCESTUDIO_DIAG
                int spin = 0;
#endif
                while (static_cast<KeyFrameTime>(keys[cursors[b]].frame) < frame &&
                       active[b] != 0) {
#ifdef MIKUDANCESTUDIO_DIAG
                    if (++spin > 100000 && getenv("MIKUDANCESTUDIO_SPIN_CANARY")) {
                        FILE* sf = fopen(getenv("MIKUDANCESTUDIO_SPIN_CANARY"), "a");
                        if (sf) {
                            fprintf(sf, "BONE spin b=%d cursor=%d frame=%.3f\n", b, cursors[b],
                                    frame);
                            fclose(sf);
                        }
                        spin = 0;
                    }
#endif
                    const BoneKey& rec = keys[cursors[b]];
                    const int next = static_cast<int>(rec.next);
                    if (next != 0) {
                        cursors[b] = next;
                        continue;
                    }
                    // terminal key (0x4A3B1C)
                    active[b] = 0;
                    for (int c = 0; c < 4; ++c)
                        bone->rotQuat[c] = rec.rotation[c]; // +0x14C
                    for (int c = 0; c < 3; ++c)
                        bone->trans[c] = rec.position[c];
                    const unsigned char mode = rec.physicsDisabled;
                    if (bone->hasRigidBody != 0 && bone->physicsDisabled != mode &&
                        physicsMode >= 2) {
                        NotifyBonePhysicsMode(m, b, mode); // 0x4A3BF9
                        bone->physicsDisabled = rec.physicsDisabled;
                    }
                    const int selIdx = bone->slotIndex;
                    if (selIdx < 0) {
                        active[b] = 0;
                    } else {
                        const Binding& window = (m)->boneOrderTable[selIdx];
                        const auto decision = format::SelectPmmBoneTerminalPose(
                            rec.frame, frame, PortableSelector(window));
                        ApplyLiveBoneSelectorPose(bone, decision, rec, rec, true);
                        if (window.windowEnd == 0)
                            active[b] = 0;
                    }
                    break;
                }
                if (active[b] == 0)
                    continue;

                const int cursorIdx = cursors[b];
                const BoneKey& rec = keys[cursorIdx];
                const std::uint32_t curFrame = rec.frame;

                if (frame == static_cast<KeyFrameTime>(curFrame)) {
                    // exact hit (0x4A3D32)
                    CopyBoneKeyVerbatim(bone, rec);
                    if (physicsMode >= 2 && bone->hasRigidBody != 0) { // 0x4A3DBE
                        const int nextIdx = static_cast<int>(rec.next);
                        const unsigned char cmode = rec.physicsDisabled;
                        if (nextIdx <= 0) {
                            if (bone->physicsDisabled != cmode)
                                NotifyBonePhysicsMode(m, b, cmode);
                            bone->physicsDisabled = cmode;
                            continue;
                        }
                        const BoneKey& nrec = keys[nextIdx];
                        if (nrec.physicsDisabled != 1 || cmode != 0) {
                            if (bone->physicsDisabled != cmode)
                                NotifyBonePhysicsMode(m, b, cmode);
                            bone->physicsDisabled = cmode;
                            continue;
                        }
                        // arm the interpolation for the upcoming segment
                        // (0x4A3E17): notify, remember the start frame, mirror
                        // the backups into the working copies AND the current
                        // pose
                        if (bone->physicsDisabled == 0)
                            NotifyBonePhysicsMode(m, b, 1);
                        bone->physicsDisabled = 1;
                        bone->rigidIdx = static_cast<std::int32_t>(curFrame);
                        for (int c = 0; c < 4; ++c)
                            bone->ikWorkingQuat[c] = bone->ikBackup[3 + c];
                        for (int c = 0; c < 3; ++c)
                            bone->ikWorkingPos[c] = bone->ikBackup[c];
                        for (int c = 0; c < 4; ++c)
                            bone->rotQuat[c] = bone->ikBackup[3 + c]; // +0x14C <- +0x194
                        for (int c = 0; c < 3; ++c)
                            bone->trans[c] = bone->ikBackup[c];
                    }
                    continue;
                }

                // between keys: previous-key blend inputs (0x4A3F94)
                const int prevIdx = static_cast<int>(rec.previous);
                const BoneKey& prevRec = keys[prevIdx];
                std::uint32_t prevFrame = prevRec.frame;
                float ppos[3] = {prevRec.position[0], prevRec.position[1], prevRec.position[2]};
                float pq[4] = {prevRec.rotation[0], prevRec.rotation[1], prevRec.rotation[2],
                               prevRec.rotation[3]};

                if (physicsMode >= 2 && bone->hasRigidBody != 0) { // 0x4A3FAE
                    if (rec.physicsDisabled == 1 && prevRec.physicsDisabled == 0) {
                        // mid-interpolation segment (0x4A3FC3)
                        const std::uint32_t start = static_cast<std::uint32_t>(bone->rigidIdx);
                        if (start < prevFrame || start >= curFrame) {
                            // stale arm -> re-arm from the backups (0x4A3FF1,
                            // unsigned; disasm 0x4A3FF1's [ecx+ebp] operand is
                            // the PREVIOUS record, var_34 the CURRENT frame)
                            if (bone->physicsDisabled == 0)
                                NotifyBonePhysicsMode(m, b, 1);
                            bone->physicsDisabled = 1;
                            bone->rigidIdx = static_cast<std::int32_t>(prevFrame);
                            std::memcpy(bone->ikWorkingQuat, bone->ikBackup + 3,
                                        sizeof(bone->ikWorkingQuat));
                            std::memcpy(bone->ikWorkingPos, bone->ikBackup,
                                        sizeof(bone->ikWorkingPos));
                        }
                        if (bone->physicsDisabled == 0)
                            NotifyBonePhysicsMode(m, b, 1);
                        bone->physicsDisabled = 1;
                        // blend from the armed working copies (0x4A40CD)
                        pq[0] = bone->ikWorkingQuat[0];
                        pq[1] = bone->ikWorkingQuat[1];
                        pq[2] = bone->ikWorkingQuat[2];
                        pq[3] = bone->ikWorkingQuat[3];
                        ppos[0] = bone->ikWorkingPos[0];
                        ppos[1] = bone->ikWorkingPos[1];
                        ppos[2] = bone->ikWorkingPos[2];
                        prevFrame = static_cast<std::uint32_t>(bone->rigidIdx);
                    } else {
                        const unsigned char pmode = prevRec.physicsDisabled;
                        if (bone->physicsDisabled != pmode)
                            NotifyBonePhysicsMode(m, b, pmode);
                        bone->physicsDisabled = pmode;
                    }
                }

                // Selector-window crossing overrides this segment's interpolation.
                const int selIdx = bone->slotIndex;
                if (selIdx >= 0) {
                    const Binding& window = (m)->boneOrderTable[selIdx];
                    const auto decision = format::SelectPmmBoneSegmentPose(
                        prevFrame, curFrame, frame, PortableSelector(window));
                    if (ApplyLiveBoneSelectorPose(bone, decision, prevRec, rec, true, ppos, pq))
                        continue;
                }

#if defined(_M_X64)
                // Continuous playback has distinct float rounding from direct
                // seeking. The portable kernel consumes the live edited keys;
                // physics and selector decisions above remain in this adapter.
                format::PmmBoneKey left{}, right{};
                left.frame = prevFrame;
                right.frame = curFrame;
                std::memcpy(left.position.data(), ppos, sizeof ppos);
                std::memcpy(left.rotation.data(), pq, sizeof pq);
                std::memcpy(right.position.data(), rec.position, sizeof rec.position);
                std::memcpy(right.rotation.data(), rec.rotation, sizeof rec.rotation);
                std::memcpy(right.interpolation.data(), rec.interpolation,
                            sizeof rec.interpolation);
                format::PmmEvaluatedBone blended;
                if (format::InterpolatePmmBonePlaybackSegment(left, right, frame, blended)) {
                    std::memcpy(bone->rotQuat, blended.rotation.data(), sizeof(float) * 4);
                    const auto boneType = static_cast<std::int8_t>(bone->type);
                    if (boneType <= static_cast<std::int8_t>(cpu_bone::BoneType::Effector) ||
                        bone->type == cpu_bone::BoneType::FixedAxis)
                        std::memcpy(bone->trans, blended.translation.data(), sizeof(float) * 3);
                    continue;
                }
#endif

                // full interpolation (LABEL_122, 0x4A433D)
                const float cq[4] = {rec.rotation[0], rec.rotation[1], rec.rotation[2],
                                     rec.rotation[3]};
#if defined(_M_IX86)
                // x86 0x4A4361..0x4A4398: fild m32int prevFrame (jge + fadd
                // flt_52B9F0 = 2^32 when negative, i.e. unsigned) subtracted
                // from the float-stored frame by fsubp 0x4A4373, fild m32int
                // (curFrame - prevFrame) with the same unsigned fixup as the
                // fdivp 0x4A4389 divisor, and a single rounding at the
                // fstp float 0x4A4390 - double intermediates like the morph
                // section above; subtract and divide on the x87 stack before tF
                // rounds to float.
                const float tF =
                    (float)((frame - (double)prevFrame) / (double)(curFrame - prevFrame));
#else
                // x64 0x7FF7CB4F1D30..61: cvtsi2ss + subss + divss - the raw
                // fraction divides in SINGLE precision on the float-cast frame
                // (frame kept as float in xmm4).
                const float tF =
                    (static_cast<float>(frame) - static_cast<float>(prevFrame)) /
                    static_cast<float>(static_cast<int>(curFrame) - static_cast<int>(prevFrame));
#endif
                // EASED rotation fraction - the slerp below uses this value
                // (see the file header's decompiler-trap note)
                const float eRot = BoneEase(m, 3, cursorIdx, tF);

#if defined(_M_IX86)
                // x86 0x4A43EF: CRT double acos/sin on float-rounded
                // intermediates (the x87 blend products stay extended).
                const double dotD = (double)pq[0] * cq[0] + (double)pq[1] * cq[1] +
                                    (double)pq[2] * cq[2] + (double)pq[3] * cq[3];
                const float dot = (float)dotD;
                if (static_cast<float>(1.0 - (double)dot * (double)dot) == 0.0f) {
                    // parallel quaternions: previous key verbatim (0x4A4414)
                    for (int c = 0; c < 4; ++c)
                        bone->rotQuat[c] = pq[c];
                } else {
                    float dc = dot;
                    if (dc > 1.0f)
                        dc = kQuatClamp;
                    else if (dc < -1.0f)
                        dc = -kQuatClamp;
                    const float th = (float)std::acos((double)dc);
                    if ((double)th > PiHalfBits() && (double)dc < 0.0) {
                        // long way around (0x4A44C6): complement angle, weights
                        // subtracted
                        const float th2 = (float)std::acos(-(double)dc);
                        const float invS = (float)(1.0 / (double)(float)std::sin((double)th2));
                        const float aStart = (float)((1.0 - (double)eRot) * (double)th2);
                        const float w0 = (float)std::sin((double)aStart) * invS;
                        const float aEnd = (float)((double)eRot * (double)th2);
                        const float w1 = (float)std::sin((double)aEnd) * invS;
                        for (int c = 0; c < 4; ++c)
                            bone->rotQuat[c] =
                                (float)((double)pq[c] * (double)w0 - (double)w1 * (double)cq[c]);
                    } else {
                        const float invS = (float)(1.0 / (double)(float)std::sin((double)th));
                        const float aStart = (float)((1.0 - (double)eRot) * (double)th);
                        const float w0 = (float)std::sin((double)aStart) * invS;
                        const float aEnd = (float)((double)eRot * (double)th);
                        const float w1 = (float)std::sin((double)aEnd) * invS;
                        for (int c = 0; c < 4; ++c)
                            bone->rotQuat[c] =
                                (float)((double)pq[c] * (double)w0 + (double)w1 * (double)cq[c]);
                    }
                }
#else
                // x64 0x7FF7CB4F1D91..0x7FF7CB4F2040: SSE single precision -
                // float dot accumulation, ucomiss parallel test on
                // 1.0f - dot*dot, acosf/sinf, float angle products, and the
                // weights formed as sin * (1.0f/s) with the reciprocal
                // computed ONCE by divss (0x7FF7CB4F1EB7, then mulss at
                // 0x7FF7CB4F1ED8/0x7FF7CB4F1EEE).  The pi/2 gate compares
                // against dword_7FF7CB552C1C = 0x3FC90FD8 (the float image of
                // the truncated-double constant - float-exact, so the double
                // compare below is bit-identical).
                const float dot = pq[0] * cq[0] + pq[1] * cq[1] + pq[2] * cq[2] + pq[3] * cq[3];
                if (1.0f - dot * dot == 0.0f) {
                    // parallel quaternions: previous key verbatim (0x4A4414)
                    for (int c = 0; c < 4; ++c)
                        bone->rotQuat[c] = pq[c];
                } else {
                    float dc = dot;
                    if (dc > 1.0f)
                        dc = kQuatClamp;
                    else if (dc < -1.0f)
                        dc = -kQuatClamp;
                    const float th = std::acos(dc);
                    if (th > 1.570796012878418f && dc < 0.0f) {
                        // long way around (0x4A44C6): complement angle, weights
                        // subtracted
                        const float th2 = std::acos(-dc);
                        const float invS = 1.0f / std::sin(th2);
                        const float w0 = std::sin((1.0f - eRot) * th2) * invS;
                        const float w1 = std::sin(eRot * th2) * invS;
                        for (int c = 0; c < 4; ++c)
                            bone->rotQuat[c] = pq[c] * w0 - w1 * cq[c];
                    } else {
                        const float invS = 1.0f / std::sin(th);
                        const float w0 = std::sin((1.0f - eRot) * th) * invS;
                        const float w1 = std::sin(eRot * th) * invS;
                        for (int c = 0; c < 4; ++c)
                            bone->rotQuat[c] = pq[c] * w0 + w1 * cq[c];
                    }
                }
#endif

                // position easing (0x4A46AF); the raw fraction goes in, the
                // callee eases per axis.  Type gate uses a SIGNED <= 6 compare
                // on the byte (setle), so negative type bytes pass too.
                const std::int8_t btype = static_cast<std::int8_t>(bone->type);
                if (btype <= static_cast<std::int8_t>(cpu_bone::BoneType::Effector) ||
                    bone->type == cpu_bone::BoneType::FixedAxis) {
                    for (int axis = 0; axis < 3; ++axis) {
                        const float delta = rec.position[axis] - ppos[axis];
                        if (delta == 0.0f) {
                            bone->trans[axis] = ppos[axis];
                        } else {
                            const float e = BoneEase(m, axis, cursorIdx, tF);
                            const float easedDelta = e * delta;
                            bone->trans[axis] = easedDelta + ppos[axis];
                        }
                    }
                }
            }
        }
    }

    static inline void InterpolateSeekRotation(Bone* bone, const float previousRotation[4],
                                               const float currentRotation[4],
                                               float rotationFraction) {
        format::InterpolatePmmBoneSeekRotation(previousRotation, currentRotation, rotationFraction,
                                               bone->rotQuat);
    }

    static void SeekDisplayTrack(Model& model, std::uint32_t frameU32) {
        auto* m = &model;
        {
            DisplayKey* const keys = (m)->displayKeys;
            format::PmmDisplayCursor cursor;
            format::PmmDisplaySelection selection;
            if (format::SelectPmmDisplayKey(ReadLiveDisplayKeyLink, keys,
                                            static_cast<double>(frameU32),
                                            format::PmmDisplayTraversal::Seek, cursor, selection))
                ApplyDisplaySelection(m, keys, selection);
        }
    }

    static int SeekMorphTracks(Model& model, std::uint32_t frameU32) {
        int result = 0;
        const int morphCnt = static_cast<int>(model.morphCount);
        if (morphCnt > 0) {
            MorphKey* const keys = model.morphKeys;
            Morph* const vals = model.morphs; // 136-byte structs
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
                const MorphKey& rec = keys[found];
                float v;
                if (rec.frame == frameU32) {
                    v = rec.value;
                } else {
                    const MorphKey& prev = keys[rec.previous];
#if defined(_M_IX86)
                    // Round the fraction and delta to float before the double multiply/add.
                    const float tF = (float)((double)(frameU32 - prev.frame) /
                                             (double)(std::uint32_t)(rec.frame - prev.frame));
                    const float delta = rec.value - prev.value;
                    v = (float)((double)tF * (double)delta + (double)prev.value);
#else
                    // Keep subtraction, division and interpolation in single precision.
                    const float tF = static_cast<float>(frameU32 - prev.frame) /
                                     static_cast<float>(static_cast<int>(rec.frame) -
                                                        static_cast<int>(prev.frame));
                    v = tF * (rec.value - prev.value) + prev.value;
#endif
                }
                vals[k].value = v;
                result = k + 1;
            }
        }
        return result;
    }

    static void SeekBoneFrame(Model* m, Bone* bone, BoneKey* keys, int b, std::uint32_t frameU32,
                              int physicsMode) {
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
                const BoneKey& rec = keys[found];
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
                    const Binding& window = m->boneOrderTable[selIdx];
                    const auto decision = format::SelectPmmBoneTerminalPose(
                        rec.frame, frameU32, PortableSelector(window));
                    ApplyLiveBoneSelectorPose(bone, decision, rec, rec, false);
                }
                return;
            }
        } else {
            resolved = true;
        }

        const BoneKey& rec = keys[found];
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
                const BoneKey& nrec = keys[nextIdx];
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
        const BoneKey& prevRec = keys[prevIdx];
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
            const Binding& window = (m)->boneOrderTable[selIdx];
            const auto decision = format::SelectPmmBoneSegmentPose(prevFrame, curFrame, frameU32,
                                                                   PortableSelector(window));
            if (ApplyLiveBoneSelectorPose(bone, decision, prevRec, rec, false, previousPosition,
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
            const Model* model;
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
        const float tF =
            static_cast<float>(frameU32 - prevFrame) /
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
                bone->trans[axis] =
                    (float)((double)e * (double)delta + (double)previousPosition[axis]);
#else
                // x64 RVA 0xCD5E8/0xCD5EC: round the product before adding.
                bone->trans[axis] = e * delta + previousPosition[axis];
#endif
            }
        }
    }

    static int SeekModelFrame(Model& model, int frameArg, int physicsMode) {
        const auto frame = static_cast<std::uint32_t>(frameArg);
        SeekDisplayTrack(model, frame);
        int result = SeekMorphTracks(model, frame);
        const int count = static_cast<int>(model.boneCount);
        for (int index = 0; index < count; ++index) {
            Bone& bone = model.boneTable[index];
            // Preserve the signed-byte comparison, including negative type bytes.
            const auto type = static_cast<std::int8_t>(bone.type);
            if (type > static_cast<std::int8_t>(cpu_bone::BoneType::Effector) &&
                bone.type != cpu_bone::BoneType::FixedAxis)
                continue;
            SeekBoneFrame(&model, &bone, model.boneKeys, index, frame, physicsMode);
            result = index + 1;
        }
        return result;
    }
};
}
