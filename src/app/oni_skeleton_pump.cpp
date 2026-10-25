// ===========================================================================
// x64 0x7FF7CB44C12E..0x7FF7CB44C656 - Kinect skeleton driver pump block (behavioral port)
// ===========================================================================
// Location: inside the physics-frame section of x64 pump sub_7FF7CB4474F0 (FrameDriver), between
// the settle rising-edge request (0x7FF7CB44C12E, already ported in physics_frame.cpp) and
// gate B (0x7FF7CB44C65B, the accessoryEditDialogOpen early return, also in
// physics_frame.cpp). This file implements the latter part, 0x7FF7CB44C141..0x7FF7CB44C656
// (without duplicating the settle request itself).
//
// Gate (0x7FF7CB44C106..0x7FF7CB44C128): var_1784 (selActive, the tracking flag written in the pump prologue
// at 0x7FF7CB44A344 by [app+0xA1360] = ?OpenNIIsTracking@@YGXPA_N@Z;
// represented by FrameDriver's selActive local in this port)
//   && [app+0x9FC98]==0（frameStepPlayback / x86 0x9ED90）
//   && [app+0x328]==0 (optflag[0], camera mode / x86 0x2F8).
// Without DxOpenNI.dll, selActive is always 0 and this entire block is unreachable, matching the original.
//
// Block flow (verified against x64):
//   0x7FF7CB44C12E  selectionActiveLatch(x64 0xA137C)==0 -> settle=1
//                  (first set site; already ported in physics_frame.cpp, not duplicated here)
//   0x7FF7CB44C141..0x7FF7CB44C4E7  18..23 indirect calls through [app+0xA1358]
//                  (stored by the DxOpenNI loader at 0x7FF7CB4C6CE1;
//                  ?OpenNIGetSkeltonJointPosition@@YGXHPAUD3DXVECTOR3@@@Z，
//                  namely OniExportSlot(OniExport::JointPosition)): (ecx=joint index, rdx=model+joint slot),
//                  fetching Kinect joint positions into 23 vec3 "current slots" every frame.
//                  The slots share ModelVertexHistoryPush's channel layout (x86 float
//                  indices 3570..3636 = bytes 14280..14556, x64 0x3B70..0x3C84).
//   0x7FF7CB44C4EF  ModelVertexHistoryPush(model, [app+0x360]/10); on x64,
//                  0x360 is the framesPerSecond counter (the 1-second FPS statistics at the pump head,
//                  0x7FF7CB4475E5..0x7FF7CB447673, write the same
//                  field, with the same 29->30/59->60 snapping as the port's frame driver). Smoothing window
//                  = FPS/10 (30fps -> 3 samples, 60fps -> 6 samples, internally clamped to 30).
//   0x7FF7CB44C52D  When model+0x3CA0 (matMisc / x86 14584, SM-Lip tracking sample count)
//                  >= 0x3138 (12600): CheckMenuItem(menu 0x124, MF_UNCHECKED)
//                  + [app+0xA1E14]=0 (capture-stage byte = state.autoRepeat,
//                  x86 0xA0D68; the same state-machine byte used by menu 0x124/WM_TIMER 0x65,
//                  with 13 corresponding reads/writes across both architectures).
//   0x7FF7CB44C560  ModelStandardPoseSetup(model, [0xA1E14]==4,
//                  kinectMirrorEnabled(0xA1370/x86 0xA03DC),
//                  kinectInitLostBone(0xA1371/x86 0xA03DD))——x64
//                  counterpart of sub_7FF7CB4F3A50, already ported in dialog_helpers.cpp;
//                  returns whether recording was active; 0 -> jump to gate B (block end).
//   0x7FF7CB44C5A4  DisableKinect(app)——x64 sub_7FF7CB4C6F00 = x86 0x42A020
//                  counterpart, already ported in oni_kinect.cpp.
//   0x7FF7CB44C5CC  RegisterKinectPoseCapture(model, [app+0x1450]=currentFrame)
//                  - x64 sub_7FF7CB4F2EB0, newly ported in this file.
//   0x7FF7CB44C5D4  CurvePanelRepaint(app)——x64 sub_7FF7CB482BB0 = x86
//                  counterpart of 0x4162xx, already ported in misc_dialogs.cpp.
//   0x7FF7CB44C604  SeekModelFrame(model, currentFrame, PlaybackPhysicsMode)
//                  ([app+0xA1D4C], x86 0xA0CC4), already implemented in the port.
//   0x7FF7CB44C609  lastRegisteredFrame(x64 0x9F038，x86 pin 647532)
//                  = max(itself, model->maxFrame(x64 0x354C/x86 12720)).
//   0x7FF7CB44C632  settle(0x9FCC1/x86 0x9EDB5)=1 (second set site),
//                  kinectCaptureActive(0xA1373/x86 0xA03DF)=0、
//                  fpsLimit(x64 0xA1904/x86 0xA08E0)=fpsLimitSaved(0xA1374/
//                  x86 0xA03E0)、PanelPaint(0x7FF7CB480EA0 = x86 0x414610)。
//
// Two counterparts ported alongside this block (called only from this pump block):
//   RegisterKinectPoseCapture  = sub_7FF7CB4F2EB0 (x64, 2971 bytes):
//     clear allocated flags in three keyframe tables -> open a type-2 undo slot (30-slot ring,
//     rebuild pointer snapshots) -> register the entire pose-tracking buffer as consecutive keyframes by bone name.
//   RegisterTraceBoneKey       = sub_7FF7CB4F23E0 (x64, 2761 bytes):
//     for one bone, write tracking records (308 bytes/sample) into 60-byte bone keyframes, sample by sample
//     (ordered doubly linked insertion + interpolation preset 20/20/107/107 + undo snapshots).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>

#include <cstdint>
#include <new>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// Existing counterparts (definitions are in their respective files).
void ModelVertexHistoryPush(unsigned char* model, int samples); // dialog_helpers.cpp, x86 0x4B75C0
char ModelStandardPoseSetup(unsigned char* model,
                            bool recordEnable, // dialog_helpers.cpp, x64 0x4F3A50
                            unsigned char mirrorLeftRight, unsigned char skeletonFlag);
void CurvePanelRepaint(MMDApp* app); // misc_dialogs.cpp, x64 0x482BB0

namespace {

// ---- Tracking buffer (0x3B3760 bytes, 308 bytes/sample, allocated by dialog_helpers.cpp) -------
// Pointer at model+8620 (x64 0x21E8); cursor is matMisc (x64 0x3CA0/x86 14584).
unsigned char* PoseTraceBuffer(unsigned char* model) {
    return static_cast<unsigned char*>(mdl::PoseTraceBuffer(model));
}

// ---- Bone-name constants (SJIS bytes, .rdata address anchors) ------------------------------
const unsigned char kCenter[9] = // 0x551F68 センター
    {0x83, 0x5A, 0x83, 0x93, 0x83, 0x65, 0x81, 0x5B, 0x00};
const unsigned char kUpperBody[7] = // 0x5524E8 upper body
    {0x8F, 0xE3, 0x94, 0xBC, 0x90, 0x67, 0x00};
const unsigned char kNeck[3] = // 0x5524F0 neck
    {0x8E, 0xF1, 0x00};
const unsigned char kArmL[5] = // 0x54D70C left arm
    {0x8D, 0xB6, 0x98, 0x72, 0x00};
const unsigned char kArmR[5] = // 0x54D724 right arm
    {0x89, 0x45, 0x98, 0x72, 0x00};
const unsigned char kElbowL[7] = // 0x54D714 left elbow
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB6, 0x00};
const unsigned char kElbowR[7] = // 0x54D72C right elbow
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB6, 0x00};
const unsigned char kLowerBody[7] = // 0x5524F4 lower body
    {0x89, 0xBA, 0x94, 0xBC, 0x90, 0x67, 0x00};
const unsigned char kLegL[5] = // 0x5524FC left leg
    {0x8D, 0xB6, 0x91, 0xAB, 0x00};
const unsigned char kLegR[5] = // 0x552504 right leg
    {0x89, 0x45, 0x91, 0xAB, 0x00};
const unsigned char kLegIkL[9] = // 0x552510 left leg IK
    {0x8D, 0xB6, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00};
const unsigned char kLegIkR[9] = // 0x552520 right leg IK
    {0x89, 0x45, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00};
const unsigned char kKneeL[7] = // 0x5523CC left knee
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB4, 0x00};
const unsigned char kKneeR[7] = // 0x5523C4 right knee
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB4, 0x00};
const unsigned char kWristL[7] = // 0x54D71C left wrist
    {0x8D, 0xB6, 0x8E, 0xE8, 0x8E, 0xF1, 0x00};
const unsigned char kWristR[7] = // 0x54D734 right wrist
    {0x89, 0x45, 0x8E, 0xE8, 0x8E, 0xF1, 0x00};
const unsigned char kShoulderL[5] = // 0x55252C left shoulder
    {0x8D, 0xB6, 0x8C, 0xA8, 0x00};
const unsigned char kShoulderR[5] = // 0x552534 right shoulder
    {0x89, 0x45, 0x8C, 0xA8, 0x00};

// Find a bone by name (linear scan as at 0x7FF7CB4F3271; first match).
int FindBone(unsigned char* model, const void* name, std::size_t bytes) {
    mdl::BoneRecord* bones = mdl::Bones(model);
    const int count = static_cast<int>(mdl::Mdl(model)->boneCount);
    for (int i = 0; i < count; ++i)
        if (std::memcmp(bones[i].name, name, bytes) == 0)
            return i;
    return -1;
}

// Table-full error dialog at x64 0x7FF7CB4F2591 / 0x7FF7CB4F2E35 (reuse the EN/JP branches verified in key_registrars /
// model_keyframe_edit: the model's physicsFlags byte selects the language).
void TraceKeyOverflowBox(unsigned char* model) {
    static const char kJpOverflow[] =
        "\x93\x6f\x98\x5e\x83\x7c\x83\x43\x83\x93\x83\x67\x90\x94\x82\xaa%d"
        "\x8c\xc2\x82\xf0\x89\x7a\x82\xa6\x82\xdc\x82\xb5\x82\xbd\n"
        "\x82\xb1\x82\xea\x88\xc8\x8f\xe3\x82\xcc\x93\x6f\x98\x5e\x82\xcd\x8d"
        "\x73\x82\xa6\x82\xdc\x82\xb9\x82\xf1\n"
        "\x81\x75\xcc\xda\xb0\xd1\x95\xd2\x8f\x57\x81\x76\x82\xcc\x81\x75\x95"
        "\x73\x97\x70\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c\x81\x76\x82\xf0\x8e\xfc"
        "\x8d\x73\x82\xb5\x82\xc4\x89\xba\x82\xb3\x82\xa2";
    static const char kJpTitle[] = "\xcc\xda\xb0\xd1\x93\x6f\x98\x5e";
    char text[0x100];
    if (mdl::Mdl(model)->physicsFlags != 0) {
        sprintf_s(text, sizeof(text),
                  "You cannot regist over %d point\n"
                  "Please execute 'delete unused frame'",
                  static_cast<int>(mdl::kBoneKeyCapacity));
        MessageBoxA(*reinterpret_cast<HWND*>(model), text, "register frame", 0);
    } else {
        sprintf_s(text, sizeof(text), kJpOverflow, static_cast<int>(mdl::kBoneKeyCapacity));
        MessageBoxA(*reinterpret_cast<HWND*>(model), text, kJpTitle, 0);
    }
}

// Fill a 60-byte bone keyframe from a tracking record according to mode (three switch a4 branches +
// interpolation preset 0x14/0x14/0x6B/0x6B + set allocated).
//   mode 0: position only (for IK), identity quaternion (w=1).
//   mode 1: rotation only, zero position.
//   mode 2: position from record +0..12, quaternion from +16..32.
// Group base within the record = 4*slot bytes (x64 uses i = 4*a3+8 and reads i-8..i+16).
void FillTraceKey(mdl::BoneKey& record, const unsigned char* traceGroup, int mode) {
    switch (mode) {
    case 0:
        std::memcpy(record.position, traceGroup, sizeof(record.position));
        record.rotation[0] = 0.0f;
        record.rotation[1] = 0.0f;
        record.rotation[2] = 0.0f;
        record.rotation[3] = 1.0f; // 0x3F800000
        break;
    case 1:
        record.position[0] = 0.0f;
        record.position[1] = 0.0f;
        record.position[2] = 0.0f;
        std::memcpy(record.rotation, traceGroup, sizeof(record.rotation));
        break;
    default: // case 2
        std::memcpy(record.position, traceGroup, sizeof(record.position));
        std::memcpy(record.rotation, traceGroup + 16, sizeof(record.rotation));
        break;
    }
    // 0x7FF7CB4F2732..0x7FF7CB4F2FD: bytes +12..27 = 20,20,20,20,
    // 20,20,20,20,107,107,107,107,107,107,107,107 (all four interpolation curves
    // {x1=y1=20, x2=y2=107}）。
    for (int lane = 0; lane < 4; ++lane) {
        record.interpolation[4 * lane + 0] = 20;
        record.interpolation[4 * lane + 1] = 20;
        record.interpolation[4 * lane + 2] = 107;
        record.interpolation[4 * lane + 3] = 107;
    }
    record.allocated = 1;
}

} // namespace

// ---------------------------------------------------------------------------
// x64 sub_7FF7CB4F23E0 - RegisterTraceBoneKey(model, boneIdx, slot, mode,
// frame): register one bone's entire pose track (308 bytes/sample, matMisc samples) as
// consecutive bone keyframes starting at frame. Returns false if the table is full (error already shown).
// ---------------------------------------------------------------------------
bool RegisterTraceBoneKey(unsigned char* model, unsigned boneIdx, int slot, int mode,
                          unsigned frame) {
    mdl::BoneKey* const keys = mdl::BoneKeys(model);
    const std::int32_t boneCount = static_cast<std::int32_t>(mdl::Mdl(model)->boneCount);

    // 0x7FF7CB4F2411..0x7FF7CB4F244F: walk next from the chain root (record index = bone index) to
    // the predecessor of frame, using the same traversal as RegisterBonePoseAtFrame.
    unsigned current = boneIdx;
    if (keys[current].frame < frame) {
        for (;;) {
            const unsigned next = keys[current].next;
            if (next == 0)
                break;
            if (keys[next].frame >= frame)
                break;
            current = next;
        }
    }

    // 0x7FF7CB4F2451..0x7FF7CB4F2499: scan free slots starting at boneCount (not a
    // persistent cursor; this function does not reset searchCursor).
    unsigned freeSlot = static_cast<unsigned>(boneCount);
    if (keys[freeSlot].frame != 0) {
        for (;;) {
            ++freeSlot;
            if (freeSlot >= mdl::kBoneKeyCapacity) {
                TraceKeyOverflowBox(model);
                return false;
            }
            if (keys[freeSlot].frame == 0)
                break;
        }
    }

    unsigned successor = keys[current].next;
    const unsigned sampleCount = static_cast<unsigned>(mdl::Mdl(model)->matMisc);
    if (sampleCount == 0)
        return true; // 0x7FF7CB4F24C4 (no samples)

    const unsigned char* const trace = PoseTraceBuffer(model);
    unsigned sampleFrame = frame;
    for (std::size_t group = 4 * static_cast<std::size_t>(slot);; group += 308, ++sampleFrame) {
        // 0x7FF7CB4F24D7..0x7FF7CB4F24EC: first snapshot the three records that will be touched.
        AppendBoneKeyToUndo(model, static_cast<int>(current)); // 0x49D410
        AppendBoneKeyToUndo(model, static_cast<int>(successor));
        AppendBoneKeyToUndo(model, static_cast<int>(freeSlot));

        if (keys[current].frame == sampleFrame) {
            // 0x7FF7CB4F2503: current record is the target frame; unlink and overwrite in place (the final
            // successor reconnection restores the chain).
            keys[current].next = 0;
            FillTraceKey(keys[current], trace + group, mode);
        } else if (keys[successor].frame == sampleFrame) {
            // 0x7FF7CB4F2813: successor is the target frame; reconnect the doubly linked chain, then overwrite.
            keys[current].next = successor;
            keys[successor].previous = current;
            FillTraceKey(keys[successor], trace + group, mode);
            current = successor;
            successor = keys[current].next;
        } else {
            // 0x7FF7CB4F2AD3: allocate a new record in the free slot and insert before the successor, then advance
            // the free slot for the next iteration (0x7FF7CB4F2D88..0x7FF7CB4F2DC0; the original v12/v15 always
            // have the same value here).
            keys[current].next = freeSlot;
            keys[freeSlot].previous = current;
            keys[freeSlot].frame = sampleFrame;
            keys[freeSlot].next = 0;
            FillTraceKey(keys[freeSlot], trace + group, mode);
            current = freeSlot;
            ++freeSlot;
            if (keys[freeSlot].frame != 0) {
                for (;;) {
                    ++freeSlot;
                    if (freeSlot >= mdl::kBoneKeyCapacity) {
                        TraceKeyOverflowBox(model);
                        return false;
                    }
                    if (keys[freeSlot].frame == 0)
                        break;
                }
            }
        }

        // 0x7FF7CB4F2DC9: advance maxFrame per sample (RAW frame value; original quirk).
        if (sampleFrame > mdl::Mdl(model)->maxFrame)
            mdl::Mdl(model)->maxFrame = sampleFrame;
        if (successor != 0) {
            keys[current].next = successor;     // 0x7FF7CB4F2DE4
            keys[successor].previous = current; // 0x7FF7CB4F2DF7
        }
        // 0x7FF7CB4F2DFF..0x7FF7CB4F2E15: increment frame; stop once the sample count is exhausted.
        if (sampleFrame + 1 - frame >= sampleCount)
            return true;
    }
}

// ---------------------------------------------------------------------------
// x64 sub_7FF7CB4F2EB0 - RegisterKinectPoseCapture(model, frame)：Kinect
// Finish capture: clear allocated flags in three keyframe tables, open a type-2 undo slot and rebuild
// both snapshot buffers, register the entire track as keyframes using the standard bone list, then free the tracking buffer.
// Returns the original residual eax value; the pump block does not consume it.
// ---------------------------------------------------------------------------
bool RegisterKinectPoseCapture(unsigned char* model, unsigned frame) {
    HWND hwnd = *reinterpret_cast<HWND*>(model);

    // 0x7FF7CB4F2EC1..0x7FF7CB4F2F5F: clear allocated across all three tables
    // (bones 600000/x86 300000, morphs 20000, display 1000; the original uses
    // unrolled loops clearing 2..3 records per iteration, with the same result).
    for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i)
        mdl::BoneKeys(model)[i].allocated = 0;
    for (std::size_t i = 0; i < mdl::kMorphKeyCapacity; ++i)
        mdl::MorphKeys(model)[i].allocated = 0;
    for (std::size_t i = 0; i < mdl::kDisplayKeyCapacity; ++i)
        mdl::DisplayKeys(model)[i].allocated = 0;

    const std::int32_t samples = mdl::Mdl(model)->matMisc;
    if (samples > 0) {                                             // 0x7FF7CB4F2F72 inverted gate
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);  // 400
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE); // 401

        // 0x7FF7CB4F2FB2..0x7FF7CB4F2FD8: increment undo ring cursor, wrap at 30 slots, set dirty flags
        // (word write = undoDirty=1 and redoDirty=0).
        if (++mdl::Mdl(model)->UndoCursor() >= mdl::kUndoHistoryDepth)
            mdl::Mdl(model)->UndoCursor() = 0;
        const int cursor = mdl::Mdl(model)->UndoCursor();
        mdl::Mdl(model)->LatestUndoCursor() = cursor;
        mdl::Mdl(model)->undoDirty = 1;
        mdl::Mdl(model)->redoDirty = 0;
        mdl::UndoRecord& undo = mdl::Mdl(model)->undoRings[0].slots[cursor];
        undo.operation = mdl::UndoOperation::BoneKeys;
        undo.frame = frame;

        // 0x7FF7CB4F300A..0x7FF7CB4F318C: pose snapshot (boneCount x 36 bytes
        // {bone index, trans, rotQuat, flag byte}; x64 source is bone+328..356 = trans/
        // rotQuat; flag-byte array at x64 0x3128, reusing the boneSelection array verified by frame_line_edit.cpp
        // for the same snapshot family (x86 0x43A region)).
        ::operator delete(undo.bonePose);
        undo.bonePose = nullptr;
        const std::int32_t boneCount = static_cast<std::int32_t>(mdl::Mdl(model)->boneCount);
        undo.bonePose = static_cast<mdl::BonePoseSnapshot*>(
            ::operator new(sizeof(mdl::BonePoseSnapshot) * boneCount));
        std::memset(undo.bonePose, 0, sizeof(mdl::BonePoseSnapshot) * boneCount);
        mdl::BoneRecord* const bones = mdl::Bones(model);
        unsigned char* const flags = mdl::Mdl(model)->boneSelection;
        for (std::int32_t i = 0; i < boneCount; ++i) {
            undo.bonePose[i].boneIndex = i;
            std::memcpy(undo.bonePose[i].position, bones[i].trans,
                        sizeof(undo.bonePose[i].position));
            std::memcpy(undo.bonePose[i].rotation, bones[i].rotQuat,
                        sizeof(undo.bonePose[i].rotation));
            undo.bonePose[i].physicsDisabled = flags[i];
        }

        // 0x7FF7CB4F319D..0x7FF7CB4F323D: keyframe snapshot buffer, 64 bytes/record
        // (3456 = 54 records x 64 bytes/sample); zero the count field and clear all of keyVisitMap.
        undo.recordCount = 0;
        ::operator delete(undo.auxiliaryPose);
        undo.auxiliaryPose = nullptr;
        undo.auxiliaryPose = ::operator new(3456 * static_cast<std::size_t>(samples));
        std::memset(undo.auxiliaryPose, 0, 3456 * static_cast<std::size_t>(samples));
        std::memset(mdl::Mdl(model)->keyVisitMap, 0, mdl::kBoneKeyCapacity);

        // Register each bone from the list (0x7FF7CB4F3247..0x7FF7CB4F3A16). If any bone's table
        // is full, return immediately (the original performs no cleanup).
        const std::uint8_t version = mdl::Mdl(model)->openniVersion;
        int idx;
        if ((idx = FindBone(model, kCenter, 9)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 0, 0, frame))
            return false; // センター
        if ((idx = FindBone(model, kUpperBody, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 3, 1, frame))
            return false; // upper body
        if (version >= 14 && (idx = FindBone(model, kNeck, 3)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 7, 1, frame))
            return false; // neck (1.40+)
        if ((idx = FindBone(model, kArmL, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 11, 1, frame))
            return false; // left arm
        if ((idx = FindBone(model, kElbowL, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 15, 1, frame))
            return false; // left elbow
        if ((idx = FindBone(model, kArmR, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 19, 1, frame))
            return false; // right arm
        if ((idx = FindBone(model, kElbowR, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 23, 1, frame))
            return false; // right elbow
        if ((idx = FindBone(model, kLowerBody, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 27, 1, frame))
            return false; // lower body
        if ((idx = FindBone(model, kLegL, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 31, 1, frame))
            return false; // left leg
        if ((idx = FindBone(model, kLegR, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 35, 1, frame))
            return false; // right leg

        // 0x7FF7CB4F361C..0x7FF7CB4F3856: choose between leg IK and knee; if an enabled IK chain named
        // left leg IK/right leg IK exists, register the leg IK bone (mode 2,
        // including position); otherwise register the knee bone (mode 1).
        bool legIkLeftEnabled = false;
        bool legIkRightEnabled = false;
        mdl::IkChain* const chains = mdl::IkChains(model);
        const std::int32_t chainCount = static_cast<std::int32_t>(mdl::Mdl(model)->ikChainCount);
        for (std::int32_t i = 0; i < chainCount; ++i) {
            if (std::memcmp(bones[chains[i].boneIndex].name, kLegIkL, 9) == 0)
                legIkLeftEnabled = chains[i].enabled != 0;
            if (std::memcmp(bones[chains[i].boneIndex].name, kLegIkR, 9) == 0)
                legIkRightEnabled = chains[i].enabled != 0;
        }
        if (legIkLeftEnabled) {
            if ((idx = FindBone(model, kLegIkL, 9)) >= 0 &&
                !RegisterTraceBoneKey(model, idx, 47, 2, frame))
                return false;
        } else {
            if ((idx = FindBone(model, kKneeL, 7)) >= 0 &&
                !RegisterTraceBoneKey(model, idx, 39, 1, frame))
                return false;
        }
        if (legIkRightEnabled) {
            if ((idx = FindBone(model, kLegIkR, 9)) >= 0 &&
                !RegisterTraceBoneKey(model, idx, 54, 2, frame))
                return false;
        } else {
            if ((idx = FindBone(model, kKneeR, 7)) >= 0 &&
                !RegisterTraceBoneKey(model, idx, 43, 1, frame))
                return false;
        }

        if (version >= 14 && (idx = FindBone(model, kWristL, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 61, 1, frame))
            return false; // left wrist (1.40+)
        if (version >= 14 && (idx = FindBone(model, kWristR, 7)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 65, 1, frame))
            return false; // right wrist (1.40+)
        if (version >= 15 && (idx = FindBone(model, kShoulderL, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 69, 1, frame))
            return false; // left shoulder (1.50+)
        if (version >= 15 && (idx = FindBone(model, kShoulderR, 5)) >= 0 &&
            !RegisterTraceBoneKey(model, idx, 73, 1, frame))
            return false; // right shoulder (1.50+)
    } // samples > 0; a full table returns directly without the cleanup below (original behavior)

    // 0x7FF7CB4F3A18..0x7FF7CB4F3A31: free the tracking buffer and zero the sample count.
    ::operator delete(PoseTraceBuffer(model));
    mdl::PoseTraceBuffer(model) = nullptr;
    mdl::Mdl(model)->matMisc = 0;
    return true;
}

// ---------------------------------------------------------------------------
// x64 0x7FF7CB44C141..0x7FF7CB44C656 - PumpKinectSkeleton: main pump block.
// Called from frame_driver.cpp (after PlaybackCatchup, before PhysicsFrame;
// the original physics frame's settle request is ported in physics_frame.cpp; see the file header).
// ---------------------------------------------------------------------------
void PumpKinectSkeleton(MMDApp* app, unsigned char selActive) {
    auto& s = *app;

    // Three-condition gate at 0x7FF7CB44C106..0x7FF7CB44C128.
    if (selActive == 0 || s.CameraMode() != 0 || // x64 0x328 (camera mode)
        s.state.frameStepPlayback != 0)          // x64 0x9FC98（x86 0x9ED90）
        return;

    unsigned char* model = s.SelectedModel(); // slot index at x64 0x13E0 / table at 0xBE8

    // 1. Fetch joints (0x7FF7CB44C141..0x7FF7CB44C4E7): OpenNI joint index -> slot
    // float index (byte offset = 14280 + 12*slot ordinal, sharing ModelVertexHistoryPush's channel table).
    // The first 18 are unconditional; version 1.40 adds 2 and version 1.50 adds 5.
    auto getJoint = reinterpret_cast<void(__stdcall*)(int, float*)>(
        s.OniExportSlot(OniExport::JointPosition)); // x64 [app+0xA1358]
    if (model == nullptr || getJoint == nullptr)
        return;
    mdl::CaptureSkeletonJoints(mdl::Mdl(model)->currentJoints, s.state.openniVersion, getJoint);

    // 2. Moving-average smoothing (0x7FF7CB44C4EF): window = framesPerSecond/10.
    ModelVertexHistoryPush(model, s.state.framesPerSecond / 10); // [app+0x360]/10

    // 3. At 12600 SM-Lip samples, uncheck automatic frame recording and reset the capture stage
    // （0x7FF7CB44C51C..0x7FF7CB44C558）。
    if (static_cast<std::uint32_t>(mdl::Mdl(model)->matMisc) >= 12600) {
        CheckMenuItem(GetMenu(static_cast<HWND>(s.Hwnd())), MDS_CMD_TOGGLE_MOTION_CAPTURE,
                      MF_UNCHECKED);
        s.state.autoRepeat = 0; // x64 0xA1E14 / x86 0xA0D68
    }

    // 4. Populate the standard pose and detect the recording edge (0x7FF7CB44C560..0x7FF7CB44C59B):
    // returning 0 (no recording ended this frame) goes directly to gate B.
    const char wasRecording =
        ModelStandardPoseSetup(model,
                               s.state.autoRepeat == 4 ? 1 : 0, // dl = (stage==4)
                               s.state.kinectMirrorEnabled,     // r8 = 0xA1370
                               s.state.kinectInitLostBone);     // r9 = 0xA1371
    if (wasRecording == 0)
        return;

    // 5. Disable Kinect and register the entire capture as keyframes (0x7FF7CB44C5A1..0x7FF7CB44C604).
    DisableKinect(app); // 0x42A020
    if (model != nullptr)
        RegisterKinectPoseCapture(model, static_cast<unsigned>(s.state.currentFrame)); // [0x1450]
    CurvePanelRepaint(app); // 0x7FF7CB482BB0
    if (model != nullptr)
        SeekModelFrame(*mdl::Mdl(model), s.state.currentFrame,
                       s.PlaybackPhysicsMode()); // 0x7FF7CB4EBD90

    // 6. Finish (0x7FF7CB44C609..0x7FF7CB44C656).
    if (s.state.lastRegisteredFrame < mdl::Mdl(model)->maxFrame)
        s.state.lastRegisteredFrame = mdl::Mdl(model)->maxFrame;
    s.PhysicsResetPending() = 1;              // 0x9FCC1 (second set site)
    s.state.kinectCaptureActive = 0;          // 0xA1373 / x86 0xA03DF
    s.state.fpsLimit = s.state.fpsLimitSaved; // [0xA1904] = [0xA1374]
    PanelPaint(app);                          // 0x7FF7CB480EA0
}

// ---------------------------------------------------------------------------
// x86 0x46DCCF..0x46DD61 / x64 0x7FF7CB44A34C..0x7FF7CB44A3EC -
// ManageKinectRecordGate: immediately follows OpenNIIsTracking detection in the pump prologue (frame_driver.cpp's
// selActive block). The caller guarantees the gate conditions (Kinect enabled and not in frame-by-frame
// playback); this function has no return value.
//
//   1. Depth-map drawing request (0x46DCCF/0x7FF7CB44A34C):
//      OpenNIDrawDepthMap(bool) (export slot 2, x86 [app+0xA03C8] /
//      x64 [app+0xA1348]): pass 0 only when both kinectCaptureActive(0xA03DF) and the capture stage
//      (0xA0D68/0xA1E14) are 0 (fully idle); otherwise pass 1.
//   2. Menu 0x124 maintenance (0x46DCFE..0x46DD61/0x7FF7CB44A37C..0x7FF7CB44A3E4):
//      selActive && non-camera mode(app+0x2F8/0x328) -> EnableMenuItem(0x124,
//      MF_ENABLED(0)); otherwise EnableMenuItem(0x124, MF_GRAYED(1)) +
//      CheckMenuItem(0x124, MF_UNCHECKED) + clear the capture stage (losing tracking
//      disables automatic frame recording and resets its state machine, as in DisableKinect 0x42A044).
// ---------------------------------------------------------------------------
void ManageKinectRecordGate(MMDApp* app, unsigned char selActive) {
    auto& s = *app;

    // 1. Depth map: do not draw while idle (neither file-driven capture nor any recording
    // state-machine stage is active).
    auto drawDepthMap = reinterpret_cast<void(__stdcall*)(unsigned char)>(
        s.OniExportSlot(OniExport::DrawDepthMap)); // x64 [app+0xA1348]
    if (drawDepthMap != nullptr)
        drawDepthMap((s.state.kinectCaptureActive == 0 && // x86 0xA03DF
                      s.state.autoRepeat == 0)            // x86 0xA0D68
                         ? 0
                         : 1);

    // 2. Menu 0x124: enabled only while tracking in non-camera mode.
    HMENU menu = GetMenu(static_cast<HWND>(s.Hwnd()));
    if (selActive != 0 && s.CameraMode() == 0) {
        EnableMenuItem(menu, MDS_CMD_TOGGLE_MOTION_CAPTURE, 0); // MF_ENABLED
        return;
    }
    EnableMenuItem(menu, MDS_CMD_TOGGLE_MOTION_CAPTURE, 1); // MF_GRAYED
    CheckMenuItem(menu, MDS_CMD_TOGGLE_MOTION_CAPTURE, MF_UNCHECKED);
    s.state.autoRepeat = 0; // x64 0xA1E14 / x86 0xA0D68
}

} // namespace mikudancestudio
