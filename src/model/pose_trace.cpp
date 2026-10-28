// Kinect trace registration: x86 0x4A4A50/0x4A5690;
// x64 0x7FF7CB4F23E0/0x7FF7CB4F2EB0 (MMD 9.32).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstring>
#include <cstdio>
#include <new>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"
namespace mikudancestudio {
namespace {
// JP overflow strings 0x52B918 / 0x52B908 (SJIS byte-exact, shared with
// key_registrars.cpp's OverflowBox).
const char kJpOverflow[] = "\x93\x6f\x98\x5e\x83\x7c\x83\x43\x83\x93\x83\x67\x90\x94\x82\xaa%d"
                           "\x8c\xc2\x82\xf0\x89\x7a\x82\xa6\x82\xdc\x82\xb5\x82\xbd\n"
                           "\x82\xb1\x82\xea\x88\xc8\x8f\xe3\x82\xcc\x93\x6f\x98\x5e\x82\xcd\x8d"
                           "\x73\x82\xa6\x82\xdc\x82\xb9\x82\xf1\n"
                           "\x81\x75\xcc\xda\xb0\xd1\x95\xd2\x8f\x57\x81\x76\x82\xcc\x81\x75\x95"
                           "\x73\x97\x70\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c\x81\x76\x82\xf0\x8e\xc0"
                           "\x8d\x73\x82\xb5\x82\xc4\x89\xba\x82\xb3\x82\xa2";
const char kJpFrameRegTitle[] = "\xcc\xda\xb0\xd1\x93\x6f\x98\x5e";

// 0x4A4C22: overflow box; the limit is the bone-key capacity (the x64
// E build's sprintf arg is 0x927C0 = 600000, the x86 original's 300000).
void OverflowBoxBoneKey(mdl::ModelRecord* m) {
    char text[256];
    if (m->physicsFlags != 0)
        sprintf_s(text, 0x100,
                  "You cannot regist over %d point\n"
                  "Please execute 'delete unused frame'",
                  static_cast<int>(mdl::kBoneKeyCapacity));
    else
        sprintf_s(text, 0x100, kJpOverflow, static_cast<int>(mdl::kBoneKeyCapacity));
    // Caption 0x4A4C22: JP 0x52B908 when the JP flag is clear, "register
    // frame" when set (the JP constant was previously unused).
    MessageBoxA(static_cast<HWND>(m->hwnd), text,
                m->physicsFlags != 0 ? "register frame" : kJpFrameRegTitle, 0);
}

// The source type selects position-only, rotation-only or full IK pose.
void FillTracePose(mdl::BoneKey& key, const float (&position)[3]) {
    std::memcpy(key.position, position, sizeof key.position);
    key.rotation[0] = key.rotation[1] = key.rotation[2] = 0.0f;
    key.rotation[3] = 1.0f;
}
void FillTracePose(mdl::BoneKey& key, const mdl::PoseTraceQuaternion& rotation) {
    key.position[0] = key.position[1] = key.position[2] = 0.0f;
    rotation.Load(key.rotation);
}
void FillTracePose(mdl::BoneKey& key, const mdl::PoseTraceIkPose& pose) {
    std::memcpy(key.position, pose.position, sizeof key.position);
    pose.rotation.Load(key.rotation);
}
template <class Source> void WriteBoneKeyRecord(mdl::BoneKey& key, const Source& source) {
    FillTracePose(key, source);
    // Four channels, stored as x1[4], y1[4], x2[4], y2[4].
    std::memset(key.interpolation, 20, 8);
    std::memset(key.interpolation + 8, 107, 8);
    key.allocated = 1;
}

// Standard-bone SJIS name probes of 0x4A5690 (byte-exact, memcmp lengths).
const unsigned char kNameCenter[] = // 0x531184 "センター" (9)
    {0x83, 0x5A, 0x83, 0x93, 0x83, 0x5E, 0x81, 0x5B, 0x00};
const unsigned char kNameUpper[] = // 0x53117C "upper body" (7)
    {0x8F, 0xE3, 0x94, 0xBC, 0x90, 0x67, 0x00};
const unsigned char kNameNeck[] = // 0x531178 "neck" (3)
    {0x8E, 0xF1, 0x00};
const unsigned char kNameLArm[] = // 0x52B81C "left arm" (5)
    {0x8D, 0xB6, 0x98, 0x72, 0x00};
const unsigned char kNameLElbow[] = // 0x52B814 "left elbow" (7)
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB6, 0x00};
const unsigned char kNameRArm[] = // 0x52B804 "right arm" (5)
    {0x89, 0x45, 0x98, 0x72, 0x00};
const unsigned char kNameRElbow[] = // 0x52B7FC "right elbow" (7)
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB6, 0x00};
const unsigned char kNameLower[] = // 0x531170 "lower body" (7)
    {0x89, 0xBA, 0x94, 0xBC, 0x90, 0x67, 0x00};
const unsigned char kNameLLeg[] = // 0x531168 "left leg" (5)
    {0x8D, 0xB6, 0x91, 0xAB, 0x00};
const unsigned char kNameRLeg[] = // 0x531160 "right leg" (5)
    {0x89, 0x45, 0x91, 0xAB, 0x00};
const unsigned char kNameLLegIk[] = // 0x531154 "left leg IK" (9)
    {0x8D, 0xB6, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00};
const unsigned char kNameRLegIk[] = // 0x531148 "right leg IK" (9)
    {0x89, 0x45, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00};
const unsigned char kNameLKnee[] = // 0x530F60 "left knee" (7)
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB4, 0x00};
const unsigned char kNameRKnee[] = // 0x530F58 "right knee" (7)
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB4, 0x00};
const unsigned char kNameLWrist[] = // 0x52B80C "left wrist" (7)
    {0x8D, 0xB6, 0x8E, 0xE8, 0x8E, 0xF1, 0x00};
const unsigned char kNameRWrist[] = // 0x52B7F4 "right wrist" (7)
    {0x89, 0x45, 0x8E, 0xE8, 0x8E, 0xF1, 0x00};
const unsigned char kNameLShoulder[] = // 0x531140 "left shoulder" (5)
    {0x8D, 0xB6, 0x8C, 0xA8, 0x00};
const unsigned char kNameRShoulder[] = // 0x531138 "right shoulder" (5)
    {0x89, 0x45, 0x8C, 0xA8, 0x00};

int FindBoneByName(mdl::ModelRecord* m, const void* name, std::size_t cb) {
    const int cnt = m->boneCount;
    if (cnt <= 0)
        return -1;
    mikudancestudio::mdl::BoneRecord* const bones = m->boneTable;
    for (int i = 0; i < cnt; ++i)
        if (std::memcmp(name, bones[i].name, cb) == 0)
            return i;
    return -1; // original leaves the counter at cnt and skips the call
}

// Register a named trace channel as consecutive keys. The three source types
// replace the original mode 0/1/2 and float-slot parameters. Undo snapshots are
// taken before each overwrite or insertion, in current/successor/free order.
template <class Source>
bool RegisterTraceBoneKey(mdl::ModelRecord* m, int boneIndex,
                          Source mdl::PoseTraceSample::* channel, std::uint32_t startFrame) {
    mdl::BoneKey* const keys = m->boneKeys;

    // chain walk: advance while frame < startFrame (unsigned, 0x4A4A8F..)
    int current = boneIndex; // v18
    if (keys[current].frame < startFrame) {
        for (;;) {
            const int nxt = static_cast<int>(keys[current].next);
            if (nxt == 0)
                break; // 0x4A4A97
            if (keys[nxt].frame >= startFrame)
                break;
            current = nxt; // 0x4A4AAE
        }
    }

    int successor = static_cast<int>(keys[current].next);
    int freeSlot = m->boneCount; // v9
    int candidate = freeSlot;    // v20
    if (freeSlot >= static_cast<int>(mdl::kBoneKeyCapacity) || keys[freeSlot].frame != 0) {
        do {
            ++freeSlot;
            if (freeSlot >= static_cast<int>(mdl::kBoneKeyCapacity)) { // 0x4A4AFC
                OverflowBoxBoneKey(m);                                 // 0x4A4C22
                return false;
            }
        } while (keys[freeSlot].frame != 0);
        candidate = freeSlot; // 0x4A4B0E
    }

    if (m->poseTraceSampleCount == 0)
        return true; // 0x4A4B22

    std::uint32_t frame = startFrame; // v19
    unsigned int sampleIndex = 0;     // v22
    for (;;) {
        AppendBoneKeyToUndo(m, current);   // 0x4A4B47
        AppendBoneKeyToUndo(m, successor); // 0x4A4B53
        AppendBoneKeyToUndo(m, freeSlot);  // 0x4A4B5B

        if (keys[current].frame == frame) {
            // exact-frame overwrite at current (unlink, rewrite, relink below)
            keys[current].next = 0;
            WriteBoneKeyRecord(keys[current], m->poseTraceBuffer[sampleIndex].*channel);
        } else {
            // insert path (0x4A4F00)
            if (keys[successor].frame == frame) {
                // overwrite the successor record instead (0x4A4F05..)
                keys[current].next = static_cast<std::uint32_t>(successor);
                keys[successor].previous = static_cast<std::uint32_t>(current);
                WriteBoneKeyRecord(keys[successor], m->poseTraceBuffer[sampleIndex].*channel);
                current = successor;  // 0x4A520A
                freeSlot = candidate; // 0x4A5218
                successor = static_cast<int>(keys[current].next);
            } else {
                // fresh record at candidate (0x4A5229..)
                keys[current].next = static_cast<std::uint32_t>(candidate);
                keys[candidate].previous = static_cast<std::uint32_t>(current);
                keys[candidate].frame = frame;
                keys[candidate].next = 0;
                WriteBoneKeyRecord(keys[candidate], m->poseTraceBuffer[sampleIndex].*channel);
                current = candidate;      // 0x4A5550
                freeSlot = candidate + 1; // 0x4A5554
                candidate = freeSlot;
                if (freeSlot >= static_cast<int>(mdl::kBoneKeyCapacity) ||
                    keys[freeSlot].frame != 0) {
                    for (;;) {
                        ++freeSlot;
                        candidate = freeSlot; // 0x4A5578
                        if (freeSlot >= static_cast<int>(mdl::kBoneKeyCapacity))
                            break; // 0x4A557C
                        if (keys[freeSlot].frame == 0)
                            break;
                    }
                    if (freeSlot >= static_cast<int>(mdl::kBoneKeyCapacity)) {
                        OverflowBoxBoneKey(m); // 0x4A5636
                        return false;          // 0x4A55FB
                    }
                }
            }
        }
        // LABEL_42 (0x4A5598)
        if (frame > m->maxFrame)
            m->maxFrame = frame;
        if (successor != 0) { // 0x4A55A6
            keys[current].next = static_cast<std::uint32_t>(successor);
            keys[successor].previous = static_cast<std::uint32_t>(current);
        }
        ++frame; // 0x4A55D6
        ++sampleIndex;
        if (sampleIndex >= static_cast<unsigned int>(m->poseTraceSampleCount))
            return true; // 0x4A55EE
    }
}

} // namespace

// ---------------------------------------------------------------------------
// VA 0x004A5690 - RegisterKinectPoseCapture(model, frame): the
// "register physics-driven
// pose" driver.  Clears every mark byte in the three key arrays, snapshots
// the current pose into the undo ring (slot = model+12724, 30-deep), then
// probes the standard skeleton by SJIS bone name and runs RegisterTraceBoneKey for
// each found chain.  On any registrar failure it returns EARLY, skipping
// the model+8620 free and the model+14584 reset (original leak/quirk).
// __thiscall(model).  Sole caller: 0x46B090.
// ---------------------------------------------------------------------------
bool RegisterKinectPoseCapture(mdl::ModelRecord* m, std::uint32_t frame) {
    mdl::ModelRecord& model = *m;
    mdl::BoneKey* const boneKeys = m->boneKeys;
    for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i)
        boneKeys[i].allocated = 0;
    mdl::MorphKey* const morphKeys = m->morphKeys;
    for (int i = 0; i < static_cast<int>(mdl::kMorphKeyCapacity); ++i)
        morphKeys[i].allocated = 0;
    mdl::DisplayKey* const masterKeys = m->displayKeys;
    for (int i = 0; i < static_cast<int>(mdl::kDisplayKeyCapacity); ++i)
        masterKeys[i].allocated = 0;

    const auto cleanup = [&]() { // 0x4A60BF
        if (m->poseTraceBuffer != nullptr) {
            ::operator delete(m->poseTraceBuffer);
            m->poseTraceBuffer = nullptr;
        }
        m->poseTraceSampleCount = 0;
    };

    if (model.poseTraceSampleCount == 0) { // 0x4A56FC
        cleanup();
        return true;
    }

    HWND hwnd = static_cast<HWND>(model.hwnd);
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), 1); // 0x4A5713
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), 0); // 0x4A572A

    if (++model.UndoCursor() >= mdl::kUndoHistoryDepth)
        model.UndoCursor() = 0; // 0x4A574D
    const int slot = model.UndoCursor();
    model.LatestUndoCursor() = slot; // 0x4A5762
    model.undoDirty = 1;             // 0x4A573D
    model.redoDirty = 0;             // 0x4A5744

    auto& undo = m->undoRings[0].slots[slot];
    undo.operation = mdl::UndoOperation::BoneKeys; // 0x4A5768
    undo.frame = frame;                            // 0x4A5786

    const int boneCnt = model.boneCount;
    auto*& pose = undo.bonePose;
    if (pose != nullptr) { // 0x4A579C
        ::operator delete(pose);
        pose = nullptr;
    }
    pose = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
        ::operator new(sizeof(*pose) * boneCnt)); // 0x4A57F1
    std::memset(pose, 0, sizeof(*pose) * static_cast<std::size_t>(boneCnt));
    if (boneCnt > 0) { // 0x4A582C
        mikudancestudio::mdl::BoneRecord* const bones = model.boneTable;
        const unsigned char* flags = model.bonePhysicsState;
        for (int b = 0; b < boneCnt; ++b) {
            auto& rec = pose[b];
            const mikudancestudio::mdl::BoneRecord* bone = &bones[b];
            rec.boneIndex = b; // 0x4A5856
            std::memcpy(rec.position, bone->trans, sizeof(rec.position));
            std::memcpy(rec.rotation, bone->rotQuat, sizeof(rec.rotation));
            rec.poseEdited = flags[b]; // 0x4A58F2
        }
    }
    undo.recordCount = 0; // 0x4A5922

    auto*& poseSrc = undo.keySnapshots;
    if (poseSrc != nullptr) { // 0x4A5939
        ::operator delete(poseSrc);
        poseSrc = nullptr;
    }
    poseSrc = static_cast<mdl::BoneKeyUndoEntry*>(::operator new(
        mdl::kTraceUndoKeysPerSample* static_cast<std::size_t>(model.poseTraceSampleCount) *
        sizeof(mdl::BoneKeyUndoEntry))); // 0x4A5995
    std::memset(poseSrc, 0,
                mdl::kTraceUndoKeysPerSample *
                    static_cast<std::size_t>(model.poseTraceSampleCount) *
                    sizeof(mdl::BoneKeyUndoEntry)); // 0x4A59C2
    std::memset(m->keyVisitMap, 0,
                sizeof(m->keyVisitMap)); // 0x4A59D5

    // standard-bone probes (order and srcIdx/mode pairs from the original)
    int i;
    i = FindBoneByName(m, kNameCenter, 9); // 0x4A5A00
    if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::center, frame))
        return false;                     // 0x4A5A2D
    i = FindBoneByName(m, kNameUpper, 7); // 0x4A5A53
    if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::upperBody, frame))
        return false;                        // 0x4A5A80
    if (model.openniVersion >= 14) {         // 0x4A5A8D
        i = FindBoneByName(m, kNameNeck, 3); // 0x4A5AB0
        if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::neck, frame))
            return false; // 0x4A5ADD
    }
    i = FindBoneByName(m, kNameLArm, 5); // 0x4A5B03
    if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::leftArm, frame))
        return false;                      // 0x4A5B30
    i = FindBoneByName(m, kNameLElbow, 7); // 0x4A5B60
    if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::leftElbow, frame))
        return false;                    // 0x4A5B8D
    i = FindBoneByName(m, kNameRArm, 5); // 0x4A5BB3
    if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::rightArm, frame))
        return false;                      // 0x4A5BE0
    i = FindBoneByName(m, kNameRElbow, 7); // 0x4A5C10
    if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::rightElbow, frame))
        return false;                     // 0x4A5C3D
    i = FindBoneByName(m, kNameLower, 7); // 0x4A5C63
    if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::lowerBody, frame))
        return false;                    // 0x4A5C90
    i = FindBoneByName(m, kNameLLeg, 5); // 0x4A5CC0
    if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::leftLeg, frame))
        return false;                    // 0x4A5CED
    i = FindBoneByName(m, kNameRLeg, 5); // 0x4A5D13
    if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::rightLeg, frame))
        return false; // 0x4A5D40

    // Leg IK chains (model+9920, 24-byte structs: +0 root bone idx,
    // +18 enabled flag): enabled -> leg IK pose, else the knee
    {
        const int ikCnt = model.ikChainCount;
        const mdl::IkChain* const chains = model.ikChains;
        mikudancestudio::mdl::BoneRecord* const bones = model.boneTable;
        int chain = -1;
        for (int c = 0; c < ikCnt; ++c) { // 0x4A5D80
            const mikudancestudio::mdl::BoneRecord* root = &bones[chains[c].boneIndex];
            if (std::memcmp(kNameLLegIk, root->name, 9) == 0) {
                chain = c;
                break;
            }
        }
        if (chain >= 0) {
            if (chains[chain].enabled != 0) {          // 0x4A5D9D
                i = FindBoneByName(m, kNameLLegIk, 9); // 0x4A5DD0
                if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::leftLegIk, frame))
                    return false; // 0x4A5DF3
            } else {
                i = FindBoneByName(m, kNameLKnee, 7); // 0x4A5E14
                if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::leftKnee, frame))
                    return false; // 0x4A5E3A
            }
        }
    }
    {
        const int ikCnt = model.ikChainCount;
        const mdl::IkChain* const chains = model.ikChains;
        mikudancestudio::mdl::BoneRecord* const bones = model.boneTable;
        int chain = -1;
        for (int c = 0; c < ikCnt; ++c) { // 0x4A5E80
            const mikudancestudio::mdl::BoneRecord* root = &bones[chains[c].boneIndex];
            if (std::memcmp(kNameRLegIk, root->name, 9) == 0) {
                chain = c;
                break;
            }
        }
        if (chain >= 0) {
            if (chains[chain].enabled != 0) {          // 0x4A5E9D
                i = FindBoneByName(m, kNameRLegIk, 9); // 0x4A5ED0
                if (i != -1 &&
                    !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::rightLegIk, frame))
                    return false; // 0x4A5EF3
            } else {
                i = FindBoneByName(m, kNameRKnee, 7); // 0x4A5F14
                if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::rightKnee, frame))
                    return false; // 0x4A5F3A
            }
        }
    }

    if (model.openniVersion >= 14) {           // 0x4A5F4E
        i = FindBoneByName(m, kNameLWrist, 7); // 0x4A5F74
        if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::leftWrist, frame))
            return false; // 0x4A5FA1
        // 0x4A5FAE is a REAL re-test of the spec gate in the binary (cmp
        // byte ptr [ebp+38FDh], 0Eh / jl), not decompiler noise.  Nothing
        // in FindBoneByName/RegisterTraceBoneKey writes model.openniVersion, so the re-test
        // always re-enters here; kept verbatim for structural fidelity.
        if (model.openniVersion >= 14) {           // 0x4A5FAE
            i = FindBoneByName(m, kNameRWrist, 7); // 0x4A5FD0
            if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::rightWrist, frame))
                return false; // 0x4A5FFD
        }
    }
    if (model.openniVersion >= 15) {              // 0x4A600A
        i = FindBoneByName(m, kNameLShoulder, 5); // 0x4A6030
        // 0x4A6056/0x4A60B6: like every other probe, a left/right shoulder
        // failure is a PLAIN return in the original - no cleanup.
        if (i != -1 &&
            !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::leftShoulder, frame)) // 0x4A6056
            return false;
        // 0x4A606A: same real re-test in the binary (cmp byte ptr
        // [ebp+38FDh], 0Fh / jl); always true here, kept verbatim.
        if (model.openniVersion >= 15) {              // 0x4A606A
            i = FindBoneByName(m, kNameRShoulder, 5); // 0x4A6090
            if (i != -1 && !RegisterTraceBoneKey(m, i, &mdl::PoseTraceSample::rightShoulder,
                                                 frame)) // 0x4A60B6
                return false;
        }
    }
    cleanup(); // 0x4A60BF
    return true;
}

} // namespace mikudancestudio
