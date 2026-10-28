// Bone-edit undo snapshot (original VA 0x0042D6E0).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdlib>
#include <cstring>
#include <new>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

void PushBoneEditUndo(MMDApp* app) { // VA 0x0042D6E0
    if (app == nullptr)
        return;
    const unsigned modelSlot = app->state.slotIdx;
    auto* model = app->ModelSlot(modelSlot);
    if (model == nullptr)
        return;

    const int boneCount = model->boneCount;
    auto* selected = model->boneSelection;
    auto* editFlags = model->bonePhysicsState;
    auto* bones = model->boneTable;
    if (boneCount <= 0 || selected == nullptr || bones == nullptr)
        return;

    int selectedCount = 0;
    for (int i = 0; i < boneCount; ++i)
        selectedCount += selected[i] != 0;
    if (selectedCount == 0)
        return;

    HWND window = app->state.hwnd;
    if (window == nullptr)
        window = static_cast<HWND>(app->Hwnd());
    EnableWindow(GetDlgItem(window, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(window, panel::kRedoButton), FALSE);

    // x64 0x7FF7CB447074/0x7FF7CB44708A: undo/redo dirty flags at model+0x3558/
    // +0x3559, immediately after the undoState ring cursor (+0x3550, depth 30). The old x86 offsets
    // 12732/12733 land in unused gap15 padding on x64 and break the Ctrl+Z/Y gates; use
    // typed fields (both architecture offsets are pinned by model_layout.hpp).
    model->undoDirty = 1;
    model->redoDirty = 0;
    auto& ringIndex = model->UndoCursor();
    if (++ringIndex >= mdl::kUndoHistoryDepth)
        ringIndex = 0;
    model->LatestUndoCursor() = ringIndex;

    auto& undo = model->undoRings[0].slots[ringIndex];
    undo.operation = mdl::UndoOperation::BonePose;
    undo.recordCount = selectedCount;
    auto*& oldSnapshot = undo.bonePose;
    if (oldSnapshot != nullptr) {
        ::operator delete(oldSnapshot);
        oldSnapshot = nullptr;
    }

    auto* snapshot = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
        ::operator new(sizeof(mikudancestudio::mdl::BonePoseSnapshot) * selectedCount));
    oldSnapshot = snapshot;
    std::memset(snapshot, 0, sizeof(*snapshot) * selectedCount);

    int output = 0;
    for (int i = 0; i < boneCount; ++i) {
        if (selected[i] == 0)
            continue;
        auto& record = snapshot[output++];
        mikudancestudio::mdl::BoneRecord* bone = &bones[i];
        record.boneIndex = i;
        std::memcpy(record.position, bone->trans, sizeof(record.position));
        std::memcpy(record.rotation, bone->rotQuat, sizeof(record.rotation));
        record.poseEdited = editFlags != nullptr ? editFlags[i] : 0;
    }
}

} // namespace mikudancestudio
