// Bone-pose clipboard: copy, exact-name paste and mirrored paste (controls 496..498).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {
using BoneCopyRecord = mdl::BonePoseClipboardRecord;

// 32-bit multiply with the original's overflow idiom (mul/seto/neg/or):
// returns 0xFFFFFFFF when the product overflows (cases 496/497/498).
static std::uint32_t MulOrMax(std::uint32_t a, std::uint32_t b) {
    const std::uint64_t r = static_cast<std::uint64_t>(a) * b;
    return r > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(r);
}

// Shift-JIS left/right markers: x64 0x7FF7CB54D9BC / 0x7FF7CB54D9C0.
static const char kBoneNameNeedleLeft[] = "\x8D\xB6";
static const char kBoneNameNeedleRight[] = "\x89\x45";

} // namespace

// Copy the selected current poses; no keyframe times are stored.
void CopySelectedBonePose(MMDApp* app, HWND hwnd) {
    mdl::ModelRecord* model = app->SelectedModel();
    const std::int32_t boneCount = model->boneCount;
    std::int32_t count = 0;
    if (boneCount > 0) {
        unsigned char* selected = model->boneSelection;
        for (std::int32_t i = 0; i < boneCount; ++i) {
            if (selected[i] != 0) {
                ++count;
            }
        }
    }
    if (count == 0) {
        return; // jz def_47E903 (no-op)
    }
    EnableWindow(GetDlgItem(hwnd, panel::kBonePasteButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kBoneReversePasteButton), TRUE);
    app->BonePoseClipboardCount() = count;
    auto& records = app->BonePoseClipboard();
    if (records != nullptr) {
        ::operator delete(records);
        records = nullptr;
    }
    auto* block = static_cast<BoneCopyRecord*>(
        ::operator new(MulOrMax(static_cast<std::uint32_t>(count), sizeof(BoneCopyRecord))));
    if (block != nullptr) {
        ConstructArrayElements(block, sizeof(BoneCopyRecord), static_cast<std::uint32_t>(count),
                               &IdentityCtor);
    }
    records = block;
    memset(block, 0, static_cast<std::size_t>(count) * sizeof(BoneCopyRecord));
    app->BonePoseClipboardCount() = 0; // fill cursor
    model = app->SelectedModel();
    const std::int32_t nBones = model->boneCount;
    mikudancestudio::mdl::BoneRecord* const bones = model->boneTable;
    unsigned char* selected = model->boneSelection;
    std::int32_t cursor = 0;
    for (std::int32_t i = 0; i < nBones; ++i) {
        if (selected[i] == 0) {
            continue;
        }
        BoneCopyRecord& copiedPose = records[cursor];
        strcpy_s(copiedPose.name, bones[i].name);
        memcpy(copiedPose.position, bones[i].trans, sizeof copiedPose.position);
        memcpy(copiedPose.rotation, bones[i].rotQuat, sizeof copiedPose.rotation);
        ++cursor;
        // 0x481B1E updates the shared copy count after every emitted
        // record.  Paste (497/498) gates directly on this field.
        app->BonePoseClipboardCount() = cursor;
    }
}

// Paste by exact name, recording the displaced poses for undo.
void PasteCopiedBonePose(MMDApp* app, HWND hwnd) {
    if (app->BonePoseClipboardCount() == 0) {
        return; // jz def_47E903 (no-op)
    }
    mdl::ModelRecord* model = app->SelectedModel();
    const std::int32_t boneCount = model->boneCount;
    unsigned char* selected = model->boneSelection;
    for (std::int32_t i = 0; i < boneCount; ++i) {
        selected[i] = 0;
    }
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    model->undoDirty = 1;
    model->redoDirty = 0;
    ++model->UndoCursor();
    if (model->UndoCursor() >= mdl::kUndoHistoryDepth) {
        model->UndoCursor() = 0;
    }
    const std::int32_t undoCursor = model->UndoCursor();
    model->LatestUndoCursor() = undoCursor;
    auto& undo = model->undoRings[0].slots[undoCursor];
    undo.operation = mdl::UndoOperation::BonePose;
    // count stored 28*(undoCursor+0x164) dwords past the undo-table base
    undo.recordCount = app->BonePoseClipboardCount();
    mdl::BonePoseSnapshot* undoPoses = undo.bonePose;
    if (undoPoses != nullptr) {
        ::operator delete(undoPoses);
        undo.bonePose = nullptr;
    }
    const std::int32_t count = app->BonePoseClipboardCount();
    undoPoses = static_cast<mdl::BonePoseSnapshot*>(
        ::operator new(MulOrMax(static_cast<std::uint32_t>(count), sizeof(mdl::BonePoseSnapshot))));
    if (undoPoses != nullptr) {
        ConstructArrayElements(undoPoses, sizeof(*undoPoses), static_cast<std::uint32_t>(count),
                               &IdentityCtor);
    }
    undo.bonePose = undoPoses;
    memset(undoPoses, 0, static_cast<std::size_t>(count) * sizeof(mdl::BonePoseSnapshot));

    auto& records = app->BonePoseClipboard();
    for (std::int32_t copiedPoseIndex = 0; copiedPoseIndex < count; ++copiedPoseIndex) {
        model = app->SelectedModel();
        const std::int32_t nBones = model->boneCount;
        selected = model->boneSelection;
        mikudancestudio::mdl::BoneRecord* const bones = model->boneTable;
        unsigned char* editFlags = model->bonePhysicsState;
        const BoneCopyRecord& copiedPose = records[copiedPoseIndex];
        // exact name match (the original's inline 2-byte-step compare
        // is byte-identical to strcmp)
        std::int32_t targetBoneIndex = -1;
        for (std::int32_t candidateBoneIndex = 0; candidateBoneIndex < nBones;
             ++candidateBoneIndex) {
            if (strcmp(bones[candidateBoneIndex].name, copiedPose.name) == 0) {
                targetBoneIndex = candidateBoneIndex;
                break;
            }
        }
        if (targetBoneIndex < 0) {
            continue;
        }
        undoPoses = undo.bonePose;
        auto& undoPose = undoPoses[copiedPoseIndex];
        mikudancestudio::mdl::BoneRecord* bone = &bones[targetBoneIndex];
        undoPose.boneIndex = targetBoneIndex;
        selected[targetBoneIndex] = 1;
        // old pos into the undo record
        memcpy(undoPose.position, bone->trans, sizeof undoPose.position);
        // new pos from the copy record
        memcpy(bone->trans, copiedPose.position, sizeof bone->trans);
        // old quat into the undo record, new quat into the bone
        memcpy(undoPose.rotation, bone->rotQuat, sizeof undoPose.rotation);
        memcpy(bone->rotQuat, copiedPose.rotation, sizeof bone->rotQuat);
        undoPose.poseEdited = editFlags[targetBoneIndex];
        editFlags[targetBoneIndex] = 1;
        model->selectedBone = targetBoneIndex;
        RebuildTimelineTrackPanel(app);
        InvalidateSelectionReadouts(app);
    }
    SetFocus(hwnd);
    app->PhysicsResetPending() = 1;
}

// Paste the mirrored copied pose, preserving each target pose in the undo slot.
void PasteMirroredBonePose(MMDApp* app, HWND hwnd) {
    if (app->BonePoseClipboardCount() == 0) {
        return; // jz def_47E903 (no-op)
    }
    mdl::ModelRecord* model = app->SelectedModel();
    const std::int32_t boneCount = model->boneCount;
    unsigned char* selected = model->boneSelection;
    for (std::int32_t i = 0; i < boneCount; ++i) {
        selected[i] = 0;
    }
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    model->undoDirty = 1;
    model->redoDirty = 0;
    ++model->UndoCursor();
    if (model->UndoCursor() >= mdl::kUndoHistoryDepth) {
        model->UndoCursor() = 0;
    }
    const std::int32_t undoCursor = model->UndoCursor();
    model->LatestUndoCursor() = undoCursor;
    auto& undo = model->undoRings[0].slots[undoCursor];
    undo.operation = mdl::UndoOperation::BonePose;
    undo.recordCount = app->BonePoseClipboardCount();
    mdl::BonePoseSnapshot* undoPoses = undo.bonePose;
    if (undoPoses != nullptr) {
        ::operator delete(undoPoses);
        undo.bonePose = nullptr;
    }
    const std::int32_t count = app->BonePoseClipboardCount();
    undoPoses = static_cast<mdl::BonePoseSnapshot*>(
        ::operator new(MulOrMax(static_cast<std::uint32_t>(count), sizeof(mdl::BonePoseSnapshot))));
    if (undoPoses != nullptr) {
        ConstructArrayElements(undoPoses, sizeof(*undoPoses), static_cast<std::uint32_t>(count),
                               &IdentityCtor);
    }
    undo.bonePose = undoPoses;
    memset(undoPoses, 0, static_cast<std::size_t>(count) * sizeof(mdl::BonePoseSnapshot));

    auto& records = app->BonePoseClipboard();
    for (std::int32_t copiedPoseIndex = 0; copiedPoseIndex < count; ++copiedPoseIndex) {
        model = app->SelectedModel();
        const std::int32_t nBones = model->boneCount;
        selected = model->boneSelection;
        mikudancestudio::mdl::BoneRecord* const bones = model->boneTable;
        unsigned char* editFlags = model->bonePhysicsState;
        const BoneCopyRecord& src = records[copiedPoseIndex];
        // Resolve the copied name before looking for its opposite-side suffix.
        std::int32_t sourceBoneIndex = -1;
        for (std::int32_t k = 0; k < nBones; ++k) {
            if (strcmp(bones[k].name, src.name) == 0) {
                sourceBoneIndex = k;
                break;
            }
        }
        if (sourceBoneIndex < 0) {
            continue; // no exact-name bone: record skipped
        }
        std::int32_t targetBoneIndex = -1;
        const char* leftMarker = strstr(bones[sourceBoneIndex].name, kBoneNameNeedleLeft);
        if (leftMarker != nullptr) {
            // Left to right: compare the suffix after the two-byte side marker.
            const char* nameSuffix = leftMarker + 2;
            for (std::int32_t k = 0; k < nBones; ++k) {
                const char* candidateMarker = strstr(bones[k].name, kBoneNameNeedleRight);
                if (candidateMarker != nullptr && strcmp(candidateMarker + 2, nameSuffix) == 0) {
                    targetBoneIndex = k;
                    break;
                }
            }
        }
        if (targetBoneIndex < 0) {
            // Right to left uses the same exact-name bone (x64 RVA 0x4463C..0x44651).
            const char* nameSuffix = strstr(bones[sourceBoneIndex].name, kBoneNameNeedleRight);
            if (nameSuffix != nullptr) {
                nameSuffix += 2;
                for (std::int32_t k = 0; k < nBones; ++k) {
                    const char* candidateMarker = strstr(bones[k].name, kBoneNameNeedleLeft);
                    if (candidateMarker != nullptr &&
                        strcmp(candidateMarker + 2, nameSuffix) == 0) {
                        targetBoneIndex = k;
                        break;
                    }
                }
            }
        }
        if (targetBoneIndex < 0) {
            targetBoneIndex = sourceBoneIndex; // fallback to the exact-name match
        }
        undoPoses = undo.bonePose;
        auto& rec = undoPoses[copiedPoseIndex];
        mikudancestudio::mdl::BoneRecord* bone = &bones[targetBoneIndex];
        rec.boneIndex = targetBoneIndex;
        selected[targetBoneIndex] = 1;
        // old pos into the undo record
        memcpy(rec.position, bone->trans, sizeof rec.position);
        // new pos from the copy record - X negated (fchs)
        bone->trans[0] = -src.position[0];
        bone->trans[1] = src.position[1];
        bone->trans[2] = src.position[2];
        // old quat into the undo record
        memcpy(rec.rotation, bone->rotQuat, sizeof rec.rotation);
        // new quat from the copy record - Y and Z negated (fchs)
        bone->rotQuat[0] = src.rotation[0];
        bone->rotQuat[1] = -src.rotation[1];
        bone->rotQuat[2] = -src.rotation[2];
        bone->rotQuat[3] = src.rotation[3];
        rec.poseEdited = editFlags[targetBoneIndex];
        editFlags[targetBoneIndex] = 1;
        model->selectedBone = targetBoneIndex;
        RebuildTimelineTrackPanel(app);
        InvalidateSelectionReadouts(app);
    }
    SetFocus(hwnd);
    app->PhysicsResetPending() = 1;
}

} // namespace mikudancestudio
