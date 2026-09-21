// ===========================================================================
// Model keyframe edit helpers used by the left frame editor.
//   0x0049D410  append one touched bone-key record to the undo slot
//               ( -> AppendBoneKeyToUndo)
//   0x004A0080  SnapshotPoseBeforeFrameChange: snapshot
//               the current pose/selection before changing frame
//   0x004A02C0  SyncModelEditControls: synchronize model
//               manipulation controls
//   0x004A1510  SnapshotSelectedKeysForUndo: create an
//               undo snapshot for selected display keys
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <CommCtrl.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

struct BoneKeyUndoEntry {
    std::int32_t index;
    mdl::BoneKey key;
};
static_assert(sizeof(BoneKeyUndoEntry) == 64, "bone-key undo record");

mdl::UndoRecord& CurrentUndo(unsigned char* model) {
    return mdl::Mdl(model)->undoRings[0].slots[
        mdl::Mdl(model)->undoState[0]];
}

mdl::UndoRecord& UndoAt(unsigned char* model, int index) {
    return mdl::Mdl(model)->undoRings[0].slots[index];
}

mdl::UndoRecord& RedoAt(unsigned char* model, int index) {
    return mdl::Mdl(model)->undoRings[1].slots[index];
}

void SetFrameEdit(unsigned char* model, int frame) {
    char text[0x100];
    sprintf_s(text, sizeof(text), "%d", frame);
    const HWND edit = GetDlgItem(*reinterpret_cast<HWND*>(model), panel::kCurrentFrameEdit);
    const int length = GetWindowTextLengthA(edit);
    SendMessageA(edit, EM_SETSEL, 0, length);
    SendMessageA(edit, EM_REPLACESEL, FALSE,
                 reinterpret_cast<LPARAM>(text));
}

void CaptureAndApplyPose(unsigned char* model,
                         const mdl::BonePoseSnapshot* source,
                         mdl::BonePoseSnapshot* capture, int count) {
    mikudancestudio::mdl::BoneRecord* bones = mikudancestudio::mdl::Bones(model);
    unsigned char* selected = mdl::Mdl(model)->bonePhysicsState;
    for (int i = 0; i < count; ++i) {
        const auto& in = source[i];
        auto& out = capture[i];
        const int index = in.boneIndex;
        auto* bone = &bones[index];
        out.boneIndex = index;
        std::memcpy(out.position, bone->trans, sizeof out.position);
        std::memcpy(out.rotation, bone->rotQuat, sizeof out.rotation);
        out.physicsDisabled = selected[index];
        std::memcpy(bone->trans, in.position, sizeof in.position);
        std::memcpy(bone->rotQuat, in.rotation, sizeof in.rotation);
        selected[index] = in.physicsDisabled;
    }
}

void ApplyPose(unsigned char* model, const mdl::BonePoseSnapshot* source,
               int count) {
    mikudancestudio::mdl::BoneRecord* bones = mikudancestudio::mdl::Bones(model);
    unsigned char* selected = mdl::Mdl(model)->bonePhysicsState;
    for (int i = 0; i < count; ++i) {
        const auto& in = source[i];
        const int index = in.boneIndex;
        auto* bone = &bones[index];
        std::memcpy(bone->trans, in.position, sizeof in.position);
        std::memcpy(bone->rotQuat, in.rotation, sizeof in.rotation);
        selected[index] = in.physicsDisabled;
    }
}

void AdvanceUndo(unsigned char* model) {
    mdl::ModelRecord& record = *mdl::Mdl(model);
    record.undoState[0] += 1;
    record.undoDirty = 1;
    record.redoDirty = 0;
    if (record.undoState[0] >= 0x1E)
        record.undoState[0] = 0;
    record.undoState[1] = record.undoState[0];
}

void ReplaceBuffer(void*& slot, std::size_t bytes) {
    if (slot != nullptr) {
        ::operator delete(slot);
        slot = nullptr;
    }
    slot = ::operator new(bytes);
    std::memset(slot, 0, bytes);
}

void SnapshotPose(unsigned char* model, mdl::UndoRecord& undo) {
    const std::int32_t count = mdl::Mdl(model)->boneCount;
    void*& slot = reinterpret_cast<void*&>(undo.bonePose);
    ReplaceBuffer(slot, static_cast<std::size_t>(count) *
                            sizeof(mdl::BonePoseSnapshot));

    auto* dst = static_cast<mdl::BonePoseSnapshot*>(slot);
    mikudancestudio::mdl::BoneRecord* pose = mikudancestudio::mdl::Bones(model);
    unsigned char* selected = mdl::Mdl(model)->bonePhysicsState;
    for (std::int32_t i = 0; i < count; ++i) {
        auto& out = dst[i];
        auto* src = &pose[i];
        out.boneIndex = i;
        std::memcpy(out.position, src->trans, sizeof out.position);
        std::memcpy(out.rotation, src->rotQuat, sizeof out.rotation);
        out.physicsDisabled = selected[i];
    }
}

float BoneKeyValue(const mdl::BoneKey* keys, int index, int lane) {
    return keys[index].position[lane];
}

void ResetBoneInterpolation(mdl::BoneKey& record, int lane) {
    record.interpolation[lane] = 20;
    record.interpolation[lane + 4] = 20;
    record.interpolation[lane + 8] = 107;
    record.interpolation[lane + 12] = 107;
}

// VA 0x0049D4D0: regenerate one automatically-smoothed position lane after
// inserting/overwriting a bone key.  Rotation (lane 3) always receives the
// original default Bezier tuple.
void RebuildBoneInterpolation(unsigned char* model, int index, int lane) {
    mdl::BoneKey* const keys = mdl::BoneKeys(model);
    mdl::BoneKey& record = keys[index];
    if (lane >= 3 || index < static_cast<int>(mdl::Mdl(model)->boneCount)) {
        ResetBoneInterpolation(record, lane);
        return;
    }

    const int previous = static_cast<int>(record.previous);
    const float currentValue = BoneKeyValue(keys, index, lane);
    float previousValue = BoneKeyValue(keys, previous, lane);
    if (previousValue == currentValue) {
        ResetBoneInterpolation(record, lane);
        return;
    }

    if (previous < static_cast<int>(mdl::Mdl(model)->boneCount)) {
        record.interpolation[lane] = 64;
        record.interpolation[lane + 4] = 0;
    } else {
        mdl::BoneKey& previousRecord = keys[previous];
        const int beforePrevious = static_cast<int>(previousRecord.previous);
        const float beforeValue = BoneKeyValue(keys, beforePrevious, lane);
        if ((previousValue - beforeValue) *
                (currentValue - previousValue) > 0.0f) {
            previousRecord.interpolation[lane + 8] = 87;
            previousRecord.interpolation[lane + 12] = 87;
            record.interpolation[lane] = 40;
            record.interpolation[lane + 4] = 40;
        } else {
            previousRecord.interpolation[lane + 8] = 64;
            previousRecord.interpolation[lane + 12] = 127;
            record.interpolation[lane] = 64;
            record.interpolation[lane + 4] = 0;
        }
    }

    const int next = static_cast<int>(record.next);
    if (next == 0) {
        record.interpolation[lane + 8] = 64;
        record.interpolation[lane + 12] = 127;
        return;
    }
    mdl::BoneKey& nextRecord = keys[next];
    const float nextValue = BoneKeyValue(keys, next, lane);
    if ((nextValue - currentValue) *
            (currentValue - previousValue) <= 0.0f) {
        record.interpolation[lane + 8] = 64;
        record.interpolation[lane + 12] = 127;
        nextRecord.interpolation[lane] = 64;
        nextRecord.interpolation[lane + 4] = 0;
    } else {
        record.interpolation[lane + 8] = 87;
        record.interpolation[lane + 12] = 87;
        nextRecord.interpolation[lane] = 40;
        nextRecord.interpolation[lane + 4] = 40;
    }
}

void BoneKeyOverflow(unsigned char* model) {
    static const char kJpOverflow[] =
        "\x93\x6f\x98\x5e\x83\x7c\x83\x43\x83\x93\x83\x67\x90\x94\x82\xaa%d"
        "\x8c\xc2\x82\xf0\x89\x7a\x82\xa6\x82\xdc\x82\xb5\x82\xbd\n"
        "\x82\xb1\x82\xea\x88\xc8\x8f\xe3\x82\xcc\x93\x6f\x98\x5e\x82\xcd\x8d"
        "\x73\x82\xa6\x82\xdc\x82\xb9\x82\xf1\n"
        "\x81\x75\xcc\xda\xb0\xd1\x95\xd2\x8f\x57\x81\x76\x82\xcc\x81\x75\x95"
        "\x73\x97\x70\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c\x81\x76\x82\xf0\x8e\xc0"
        "\x8d\x73\x82\xb5\x82\xc4\x89\xba\x82\xb3\x82\xa2";
    static const char kJpTitle[] = "\xcc\xda\xb0\xd1\x93\x6f\x98\x5e";
    char text[0x100];
    if (mikudancestudio::mdl::Mdl(model)->physicsFlags != 0) {
        sprintf_s(text, sizeof(text),
                  "You cannot regist over %d point\n"
                  "Please execute 'delete unused frame'",
                  static_cast<int>(mdl::kBoneKeyCapacity));
        MessageBoxA(*reinterpret_cast<HWND*>(model), text,
                    "register frame", 0);
    } else {
        sprintf_s(text, sizeof(text), kJpOverflow,
                  static_cast<int>(mdl::kBoneKeyCapacity));
        MessageBoxA(*reinterpret_cast<HWND*>(model), text, kJpTitle, 0);
    }
}

}  // namespace

void AppendBoneKeyToUndo(unsigned char* model, int index) {  // VA 0x0049D410
    if (model == nullptr)
        return;
    unsigned char* visited = mdl::Mdl(model)->keyVisitMap;
    if (visited[index] != 0)
        return;

    visited[index] = 1;
    auto& undo = CurrentUndo(model);
    std::int32_t& count = undo.dirty;
    auto* records = static_cast<BoneKeyUndoEntry*>(undo.auxiliaryPose);
    records[count].index = index;
    std::memcpy(&records[count].key, &mdl::BoneKeys(model)[index], sizeof(mdl::BoneKey));
    ++count;
}

int RegisterBonePoseAtFrame(unsigned char* model, int boneIndex,  // VA 0x004B38A0
              std::uint32_t frame, int mode) {
    if (model == nullptr)
        return static_cast<int>(frame);

    const HWND hwnd = *reinterpret_cast<HWND*>(model);
    const bool autoInterpolation =
        SendMessageA(GetDlgItem(hwnd, panel::kPhysicsFrameCheckbox), BM_GETCHECK, 0, 0) == BST_CHECKED;
    mdl::BoneKey* const keys = mdl::BoneKeys(model);
    mdl::BoneRecord* const bone = &mdl::Bones(model)[boneIndex];

    int current = boneIndex;
    if (keys[current].frame < frame) {
        for (;;) {
            const int next = static_cast<int>(keys[current].next);
            if (next == 0)
                break;
            current = next;
            if (keys[current].frame >= frame)
                break;
        }
    }

    auto fill = [&](int recordIndex) {
        mdl::BoneKey& record = keys[recordIndex];
        const bool usePhysicsPose = bone->hasRigidBody != 0 &&
            (mode == 1 || (mode >= 2 && bone->physicsDisabled == 0));
        if (usePhysicsPose) {
            std::memcpy(record.rotation, bone->ikBackup + 3,
                        sizeof(record.rotation));
            std::memcpy(record.position, bone->ikBackup,
                        sizeof(record.position));
            std::memcpy(bone->rotQuat, bone->ikBackup + 3, 0x10);
            std::memcpy(bone->trans, bone->ikBackup, 0x0C);
        } else {
            std::memcpy(record.rotation, bone->rotQuat,
                        sizeof(record.rotation));
            std::memcpy(record.position, bone->trans,
                        sizeof(record.position));
        }

        if (bone->hasRigidBody != 0) {
            if (IsDlgButtonChecked(hwnd, panel::kPhysicsCheckbox) == BST_CHECKED) {
                record.physicsDisabled = 0;
                if (mode >= 2 && bone->physicsDisabled != 0)
                    NotifyBonePhysicsMode(model, boneIndex, 0);
                bone->physicsDisabled = 0;
            } else {
                record.physicsDisabled = 1;
                if (bone->physicsDisabled == 0 && mode >= 2)
                    NotifyBonePhysicsMode(model, boneIndex, 1);
                bone->physicsDisabled = 1;
            }
        }

        if (autoInterpolation) {
            for (int lane = 0; lane < 4; ++lane)
                RebuildBoneInterpolation(model, recordIndex, lane);
        } else {
            for (int lane = 0; lane < 4; ++lane)
                ResetBoneInterpolation(record, lane);
        }
        record.allocated = 1;
    };

    mdl::BoneKey& currentRecord = keys[current];
    if (currentRecord.frame == frame) {
        AppendBoneKeyToUndo(model, current);
        fill(current);
    } else {
        // x64 sub_7FF7CB4E9DB0 @0x7FF7CB4EA46F：空闲槽扫描从持久游标
        // searchCursor（model+0x22B8）起步，命中占用槽时游标随扫描推进、
        // 只增不减——写法与 RegisterBoneKey（key_registrars.cpp）一致。
        int freeIndex = static_cast<int>(mdl::Mdl(model)->searchCursor);
        if (keys[freeIndex].frame != 0) {
            for (;;) {
                ++mdl::Mdl(model)->searchCursor;
                ++freeIndex;
                if (freeIndex >= static_cast<int>(mdl::kBoneKeyCapacity)) {
                    BoneKeyOverflow(model);
                    return 0;
                }
                if (keys[freeIndex].frame == 0) break;
            }
        }

        mdl::BoneKey& fresh = keys[freeIndex];
        if (currentRecord.frame < frame) {
            AppendBoneKeyToUndo(model, current);
            AppendBoneKeyToUndo(model, freeIndex);
            currentRecord.next = static_cast<std::uint32_t>(freeIndex);
            fresh.previous = static_cast<std::uint32_t>(current);
        } else {
            const int previous = static_cast<int>(currentRecord.previous);
            mdl::BoneKey& previousRecord = keys[previous];
            AppendBoneKeyToUndo(model, previous);
            AppendBoneKeyToUndo(model, current);
            AppendBoneKeyToUndo(model, freeIndex);
            previousRecord.next = static_cast<std::uint32_t>(freeIndex);
            fresh.previous = static_cast<std::uint32_t>(previous);
            currentRecord.previous = static_cast<std::uint32_t>(freeIndex);
            fresh.next = static_cast<std::uint32_t>(current);
        }
        fresh.frame = frame;
        fill(freeIndex);
    }

    if (frame > mikudancestudio::mdl::Mdl(model)->maxFrame)
        mikudancestudio::mdl::Mdl(model)->maxFrame = frame;
    return static_cast<int>(frame);
}

void RegisterSelectedBoneKeys(unsigned char* model, int frame, int mode) {  // VA 0x004C2080
    if (model == nullptr)
        return;

    const std::int32_t boneCount = mdl::Mdl(model)->boneCount;
    unsigned char* const selected = mdl::Mdl(model)->boneSelection;
    unsigned char* const secondary = mdl::Mdl(model)->bonePhysicsState;
    std::int32_t selectedCount = 0;
    for (std::int32_t i = 0; i < boneCount; ++i) {
        if (selected[i] != 0)
            ++selectedCount;
    }

    const HWND hwnd = *reinterpret_cast<HWND*>(model);
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    AdvanceUndo(model);

    auto& undo = CurrentUndo(model);
    undo.operation = 2;
    undo.frame = frame;
    SnapshotPose(model, undo);
    undo.dirty = 0;
    void*& keysSlot = undo.auxiliaryPose;
    ReplaceBuffer(keysSlot,
                  static_cast<std::size_t>(selectedCount) * 0xC0);

    std::memset(mdl::Mdl(model)->keyVisitMap, 0,
                sizeof(mdl::Mdl(model)->keyVisitMap));
    mdl::BoneKey* const boneKeys = mdl::BoneKeys(model);
    for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i)
        boneKeys[i].allocated = 0;
    mdl::MorphKey* const morphKeys = mdl::MorphKeys(model);
    for (std::size_t i = 0; i < mdl::kMorphKeyCapacity; ++i)
        morphKeys[i].allocated = 0;
    mdl::DisplayKey* const displayKeys = mdl::DisplayKeys(model);
    for (std::size_t i = 0; i < mdl::kDisplayKeyCapacity; ++i)
        displayKeys[i].allocated = 0;

    for (std::int32_t i = 0; i < boneCount; ++i) {
        if (selected[i] == 0)
            continue;
        RegisterBonePoseAtFrame(model, i, static_cast<std::uint32_t>(frame), mode);
        secondary[i] = 0;
    }
}

void SnapshotPoseBeforeFrameChange(unsigned char* model, int frame) {
    if (model == nullptr)
        return;
    const std::int32_t count = mdl::Mdl(model)->boneCount;
    unsigned char* selected = mdl::Mdl(model)->bonePhysicsState;
    std::int32_t first = 0;
    while (first < count && selected[first] == 0)
        ++first;
    if (first >= count)
        return;

    AdvanceUndo(model);
    auto& undo = CurrentUndo(model);
    undo.operation = 3;
    undo.dirty = count;
    undo.frame = frame;
    SnapshotPose(model, undo);
}

void SnapshotSelectedKeysForUndo(unsigned char* model, int frame) {
    if (model == nullptr)
        return;
    mdl::BoneKey* const keys = mdl::BoneKeys(model);
    std::int32_t selectedCount = 0;
    for (std::int32_t group = 0;
         group < static_cast<std::int32_t>(mdl::kBoneKeyCapacity / 6);
         ++group) {
        for (int lane = 0; lane < 6; ++lane) {
            if (keys[group * 6 + lane].allocated != 0)
                ++selectedCount;
        }
    }
    if (selectedCount == 0)
        return;

    const HWND hwnd = *reinterpret_cast<HWND*>(model);
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);

    AdvanceUndo(model);
    auto& undo = CurrentUndo(model);
    undo.operation = 2;
    undo.dirty = 0;
    undo.frame = frame;
    SnapshotPose(model, undo);

    void*& selectedSlot = undo.auxiliaryPose;
    ReplaceBuffer(selectedSlot,
                  static_cast<std::size_t>(selectedCount) * sizeof(BoneKeyUndoEntry));
    std::memset(mdl::Mdl(model)->keyVisitMap, 0,
                sizeof(mdl::Mdl(model)->keyVisitMap));
    for (std::size_t index = 0; index < mdl::kBoneKeyCapacity; ++index) {
        if (keys[index].allocated != 0)
            AppendBoneKeyToUndo(model, index);
    }
}

// Original inline block 0x43F15E..0x43F60D in 0x43E970.  DeleteMarkedKeyframes has
// just made the deletion snapshot at the current cursor.  Type 4 tells
// Undo/Redo to chain that entry with the type-2 snapshot opened here for the
// transformed records which are about to be inserted.
void BeginRangeScaleBoneUndo(unsigned char* model, int frame,
                             int transformedKeyCount) {
    if (model == nullptr || transformedKeyCount <= 0)
        return;

    CurrentUndo(model).operation = 4;

    const HWND hwnd = *reinterpret_cast<HWND*>(model);
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    AdvanceUndo(model);

    auto& undo = CurrentUndo(model);
    undo.operation = 2;
    undo.dirty = 0;
    undo.frame = frame;
    SnapshotPose(model, undo);

    void*& keySlot = undo.auxiliaryPose;
    ReplaceBuffer(keySlot,
                  static_cast<std::size_t>(transformedKeyCount) * 0xC0);
    std::memset(mdl::Mdl(model)->keyVisitMap, 0,
                sizeof(mdl::Mdl(model)->keyVisitMap));
}

// VA 0x004A09E0: delete every marked model-mode key.  The three key arrays
// retain their fixed-capacity slots; non-root records are unlinked and reset,
// while root records keep their track-head role with default values.
void DeleteMarkedModelKeys(unsigned char* model, int frame) {  // VA 0x004A09E0
    if (model == nullptr) return;
    mdl::BoneKey* const boneKeys = mdl::BoneKeys(model);
    mdl::MorphKey* const morphKeys = mdl::MorphKeys(model);
    mdl::DisplayKey* const displayKeys = mdl::DisplayKeys(model);
    const std::size_t boneCount = mikudancestudio::mdl::Mdl(model)->boneCount;
    const std::size_t morphCount = mikudancestudio::mdl::Mdl(model)->morphCount;

    int selectedBoneKeys = 0;
    for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i) {
        if (boneKeys[i].allocated != 0)
            ++selectedBoneKeys;
    }
    if (selectedBoneKeys != 0) {
        const HWND hwnd = *reinterpret_cast<HWND*>(model);
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
        AdvanceUndo(model);
        auto& undo = CurrentUndo(model);
        undo.operation = 2;
        undo.dirty = 0;
        undo.frame = frame;
        SnapshotPose(model, undo);
        void*& keySlot = undo.auxiliaryPose;
        ReplaceBuffer(keySlot,
                      static_cast<std::size_t>(selectedBoneKeys) * 0xC0);
        std::memset(mdl::Mdl(model)->keyVisitMap, 0,
                    sizeof(mdl::Mdl(model)->keyVisitMap));
    }

    // Reset every display/IK key record.
    for (std::size_t i = 0; i < mdl::kDisplayKeyCapacity; ++i) {
        mdl::DisplayKey& rec = displayKeys[i];
        if (rec.allocated == 0) continue;
        const int prev = static_cast<int>(rec.previous);
        const int next = static_cast<int>(rec.next);
        rec.frame = 0;
        if (i != 0) {
            displayKeys[prev].next = static_cast<std::uint32_t>(next);
            displayKeys[next].previous = static_cast<std::uint32_t>(prev);
        }
        rec.allocated = 0;
        rec.visible = 1;
        auto* ik = mdl::IkStates(rec);
        for (std::uint32_t j = 0;
             j < mikudancestudio::mdl::Mdl(model)->ikChainCount; ++j)
            ik[j] = 1;
        auto* selectors = mdl::SelectorStates(rec);
        for (std::uint32_t j = 0;
             j < mikudancestudio::mdl::Mdl(model)->boneOrderCount; ++j) {
            selectors[j].modelIndex = -1;
            selectors[j].boneIndex = 0;
        }
        if (i != 0) rec.next = 0;
        rec.previous = 0;
    }

    // Reset every morph key record.
    for (std::size_t i = 0; i < mdl::kMorphKeyCapacity; ++i) {
        mdl::MorphKey& rec = morphKeys[i];
        if (rec.allocated == 0) continue;
        const int prev = static_cast<int>(rec.previous);
        const int next = static_cast<int>(rec.next);
        rec.frame = 0;
        if (i >= morphCount) {
            morphKeys[prev].next = static_cast<std::uint32_t>(next);
            morphKeys[next].previous = static_cast<std::uint32_t>(prev);
        }
        rec.allocated = 0;
        rec.value = 0.0f;
        if (i >= morphCount) rec.next = 0;
        rec.previous = 0;
    }

    const HWND hwnd = *reinterpret_cast<HWND*>(model);
    const bool autoInterpolation =
        SendMessageA(GetDlgItem(hwnd, panel::kPhysicsFrameCheckbox), BM_GETCHECK, 0, 0) == BST_CHECKED;
    mdl::BoneRecord* const bones = mdl::Bones(model);
    for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i) {
        mdl::BoneKey& rec = boneKeys[i];
        if (rec.allocated == 0) continue;
        const int prev = static_cast<int>(rec.previous);
        const int next = static_cast<int>(rec.next);
        AppendBoneKeyToUndo(model, static_cast<int>(i));
        AppendBoneKeyToUndo(model, prev);
        AppendBoneKeyToUndo(model, next);
        rec.frame = 0;
        if (i >= boneCount) {
            boneKeys[prev].next = static_cast<std::uint32_t>(next);
            boneKeys[next].previous = static_cast<std::uint32_t>(prev);
        }
        rec.allocated = 0;
        rec.position[0] = rec.position[1] = rec.position[2] = 0.0f;
        rec.rotation[0] = rec.rotation[1] = rec.rotation[2] = 0.0f;
        rec.rotation[3] = 1.0f;
        if (autoInterpolation) {
            const int neighbour = next != 0 ? next : prev;
            for (int lane = 0; lane < 4; ++lane)
                RebuildBoneInterpolation(model, neighbour, lane);
        }
        if (i >= boneCount) rec.next = 0;
        rec.previous = 0;
        if (i < boneCount && bones[i].hasRigidBody != 0)
            rec.physicsDisabled = 0;
    }
}

// VA 0x004A1870: undo one ring entry, capturing the displaced state into the
// parallel redo ring at 0x2A34. Type 4 entries are chained recursively by MMD.
void UndoModelEdit(unsigned char* model, std::int32_t& frame) {  // VA 0x004A1870
    if (model == nullptr) return;
    int cursor = static_cast<int>(mdl::Mdl(model)->undoState[0]);
    auto& undo = UndoAt(model, cursor);
    const int type = undo.operation;
    if (type == 0) {
        EnableWindow(GetDlgItem(*reinterpret_cast<HWND*>(model), panel::kUndoButton), FALSE);
        mdl::Mdl(model)->undoDirty = 0;
        return;
    }

    auto& redo = RedoAt(model, cursor);
    const int count = undo.dirty;
    redo.dirty = count;
    if (type == 1 || type == 3) {
        redo.operation = 1;
        void*& redoPose = reinterpret_cast<void*&>(redo.bonePose);
        ReplaceBuffer(redoPose, static_cast<std::size_t>(count) *
                                    sizeof(mdl::BonePoseSnapshot));
        CaptureAndApplyPose(model, undo.bonePose, redo.bonePose, count);
        if (type == 3) {
            frame = static_cast<int>(undo.frame);
            SetFrameEdit(model, frame);
        }
    } else if (type == 2 || type == 4) {
        redo.operation = 2;
        void*& redoKeys = redo.auxiliaryPose;
        ReplaceBuffer(redoKeys, static_cast<std::size_t>(count) * sizeof(BoneKeyUndoEntry));
        std::memset(mdl::Mdl(model)->keyVisitMap, 0,
                    sizeof(mdl::Mdl(model)->keyVisitMap));
        const int boneCount = static_cast<int>(mdl::Mdl(model)->boneCount);
        void*& redoPose = reinterpret_cast<void*&>(redo.bonePose);
        ReplaceBuffer(redoPose, static_cast<std::size_t>(boneCount) *
                                    sizeof(mdl::BonePoseSnapshot));
        frame = static_cast<int>(undo.frame);
        SetFrameEdit(model, frame);
        CaptureAndApplyPose(model, undo.bonePose, redo.bonePose, boneCount);
        mdl::BoneKey* const keys = mdl::BoneKeys(model);
        const auto* source = static_cast<const BoneKeyUndoEntry*>(
            undo.auxiliaryPose);
        auto* capture = static_cast<BoneKeyUndoEntry*>(redoKeys);
        for (int i = 0; i < count; ++i) {
            const int index = source[i].index;
            capture[i].index = index;
            std::memcpy(&capture[i].key, &keys[index], sizeof(mdl::BoneKey));
            std::memcpy(&keys[index], &source[i].key, sizeof(mdl::BoneKey));
        }
    }

    cursor = cursor == 0 ? 29 : cursor - 1;
    mdl::Mdl(model)->undoState[0] = cursor;
    if (UndoAt(model, cursor).operation == 4)
        UndoModelEdit(model, frame);
}

// VA 0x004A2490: redo the parallel-ring entry selected by advancing the
// cursor. Type 2/4 restores complete 60-byte key records byte-for-byte.
void RedoModelEdit(unsigned char* model, std::int32_t& frame) {  // VA 0x004A2490
    if (model == nullptr) return;
    int cursor = static_cast<int>(mdl::Mdl(model)->undoState[0]) + 1;
    if (cursor >= 30) cursor = 0;
    mdl::Mdl(model)->undoState[0] = cursor;
    auto& redo = RedoAt(model, cursor);
    const int type = redo.operation;
    if (type == 0) {
        EnableWindow(GetDlgItem(*reinterpret_cast<HWND*>(model), panel::kRedoButton), FALSE);
        mdl::Mdl(model)->redoDirty = 0;
        return;
    }
    const int count = redo.dirty;
    if (type == 1) {
        ApplyPose(model, redo.bonePose, count);
    } else if (type == 2 || type == 4) {
        mdl::BoneKey* const keys = mdl::BoneKeys(model);
        const auto* source = static_cast<const BoneKeyUndoEntry*>(
            redo.auxiliaryPose);
        for (int i = 0; i < count; ++i) {
            const int index = source[i].index;
            std::memcpy(&keys[index], &source[i].key, sizeof(mdl::BoneKey));
        }
        frame = static_cast<int>(UndoAt(model, cursor).frame);
        SetFrameEdit(model, frame);
    }
    if (UndoAt(model, cursor).operation == 4)
        RedoModelEdit(model, frame);
}

void ResetDisplayKeyCursor(unsigned char* model) {  // VA 0x004A4A00
    if (model == nullptr)
        return;
    mdl::DisplayKey* const displayKeys = mdl::DisplayKeys(model);
    std::int32_t& freeIndex =
        mdl::Mdl(model)->searchCursor;
    freeIndex = 1;
    if (displayKeys == nullptr)
        return;
    while (freeIndex < 1000 && displayKeys[freeIndex].frame != 0) {
        ++freeIndex;
    }
}

void SyncModelEditControls(unsigned char* model) {
    if (model == nullptr)
        return;
    const HWND hwnd = *reinterpret_cast<HWND*>(model);
    SendMessageA(GetDlgItem(hwnd, panel::kModelVisibleCheckbox), BM_SETCHECK,
                 mikudancestudio::mdl::Mdl(model)->loadComplete != 0 ? BST_CHECKED : BST_UNCHECKED, 0);

    mdl::IkChain* chains = mdl::IkChains(model);
    if (chains != nullptr) {
        const LRESULT sel =
            SendMessageA(GetDlgItem(hwnd, panel::kIkChainCombo), CB_GETCURSEL, 0, 0);
        const bool enabled = chains[sel].enabled != 0;
        CheckRadioButton(hwnd, 0x1BC, 0x1BD, enabled ? 0x1BC : 0x1BD);
    }

    mdl::MorphRecord* morphs = mdl::Morphs(model);
    if (morphs == nullptr)
        return;
    static constexpr int kSliderIds[4] = {0x1F9, 0x1FE, 0x203, 0x208};
    static constexpr int kEditIds[4] = {0x1FA, 0x1FF, 0x204, 0x209};
    char text[0x100];
    for (int lane = 0; lane < 4; ++lane) {
        const std::int32_t index = mdl::Mdl(model)->selectedMorphs[lane];
        if (index < 0)
            continue;
        const float value =
            morphs[static_cast<std::size_t>(index)].value;
        SendMessageA(GetDlgItem(hwnd, kSliderIds[lane]), TBM_SETPOS, TRUE,
                     static_cast<LPARAM>(static_cast<std::int32_t>(value * 100.0f)));
        sprintf_s(text, sizeof(text), "%5.4f", static_cast<double>(value));
        SetWindowTextA(GetDlgItem(hwnd, kEditIds[lane]), text);
    }
}

// Exported entry for the frame-line delete command (0x43A650) - the body
// above is the verbatim port of VA 0x0049D4D0 but lives in the anonymous
// namespace.  VA 0x0049D4D0.
void RebuildBoneKeyInterpolation(unsigned char* model, int index, int lane) {  // VA 0x0049D4D0
    RebuildBoneInterpolation(model, index, lane);
}

void mdl::SelectPhysicsOnBoneKeys(ModelRecord& model) {
    for (std::size_t i = 0; i < kMorphKeyCapacity; ++i)
        model.morphKeys[i].allocated = 0;
    for (std::size_t i = 0; i < kDisplayKeyCapacity; ++i)
        model.displayKeys[i].allocated = 0;
    for (std::size_t i = 0; i < kBoneKeyCapacity; ++i)
        model.boneKeys[i].allocated = 0;

    for (std::uint32_t bone = 0; bone < model.boneCount; ++bone) {
        if (model.boneTable[bone].hasRigidBody == 0)
            continue;
        BoneKey* key = &model.boneKeys[bone];
        for (;;) {
            if (key->physicsDisabled == 0)
                key->allocated = 1;
            // The original treats the link as signed when testing the sentinel.
            if (static_cast<std::int32_t>(key->next) <= 0)
                break;
            key = &model.boneKeys[key->next];
        }
    }
}

}  // namespace mikudancestudio
