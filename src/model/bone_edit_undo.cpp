// Bone-edit undo snapshot (original VA 0x0042D6E0; ).
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

void PushBoneEditUndo(MMDApp* app) {  // VA 0x0042D6E0
    if (app == nullptr)
        return;
    const unsigned modelSlot = app->state.slotIdx;
    auto* model = static_cast<unsigned char*>(
        app->state.modelSlots[modelSlot]);
    if (model == nullptr)
        return;

    const int boneCount = mikudancestudio::mdl::Mdl(model)->boneCount;
    auto* selected = mikudancestudio::mdl::Mdl(model)->boneSelection;
    auto* dirty = mikudancestudio::mdl::Mdl(model)->bonePhysicsState;
    auto* bones = mikudancestudio::mdl::Bones(model);
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

    // x64 0x7FF7CB447074/0x7FF7CB44708A：undo/redo 脏标志 = model+0x3558/
    // +0x3559，紧跟 undoState 环游标（+0x3550，环深 30）。x86 旧偏移
    // 12732/12733 在 x64 落进 gap15 死垫片会让 Ctrl+Z/Y 门失效，必须走
    // typed 字段（双架构偏移由 model_layout.hpp 钉死）。
    mikudancestudio::mdl::Mdl(model)->undoDirty = 1;
    mikudancestudio::mdl::Mdl(model)->redoDirty = 0;
    auto& ringIndex = mikudancestudio::mdl::Mdl(model)->undoState[0];
    if (++ringIndex >= 30)
        ringIndex = 0;
    mikudancestudio::mdl::Mdl(model)->undoState[1] = ringIndex;

    auto& undo = mikudancestudio::mdl::Mdl(model)->undoRings[0].slots[ringIndex];
    undo.operation = 1;
    undo.dirty = selectedCount;
    auto*& oldSnapshot = undo.bonePose;
    if (oldSnapshot != nullptr) {
        ::operator delete(oldSnapshot);
        oldSnapshot = nullptr;
    }

    auto* snapshot = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
        ::operator new(sizeof(mikudancestudio::mdl::BonePoseSnapshot) * selectedCount));
    oldSnapshot = snapshot;
    std::memset(snapshot, 0, static_cast<std::size_t>(36) * selectedCount);

    int output = 0;
    for (int i = 0; i < boneCount; ++i) {
        if (selected[i] == 0)
            continue;
        auto& record = snapshot[output++];
        mikudancestudio::mdl::BoneRecord* bone = &bones[i];
        record.boneIndex = i;
        std::memcpy(record.position, bone->trans, sizeof(record.position));
        std::memcpy(record.rotation, bone->rotQuat, sizeof(record.rotation));
        record.physicsDisabled = dirty != nullptr ? dirty[i] : 0;
    }
}

}  // namespace mikudancestudio
