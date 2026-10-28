// ===========================================================================
// Left frame-editor refresh and single-frame stepping helpers.
//   0x00430F20 / 0x004312E0  StepFrame(app, true/false) - one frame forward /
//                             back plus the full apply/refresh chain
//   0x00432FA0  RefreshAfterFrameApply - re-apply the current frame to every
//                             model/accessory track and refresh the panels
// (0x0040D070 is ported once, as InvalidateSelectionReadouts in ui_view_refresh.cpp;
//  the 0x40D070 twin that used to live here was that same function.)
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

void RefreshSelfShadowPanel(MMDApp* app);
void ApplyGravityTrack(MMDApp* app);
void ApplyAccessoryTrack(MMDApp* app, int index);
void SyncAccessoryEditPanel(MMDApp* app);
void AviBgOverlayRefresh(MMDApp* app);

namespace {

void SnapshotAndClearPoseEdits(MMDApp* app) {
    // x64 twins sub_7FF7CB4840E0 (0x430F20) / sub_7FF7CB484600 (0x4312E0)
    // sweep all 255 slots (count-down from 0xFF at 0x7FF7CB484121).
    for (int slot = 0; slot < kModelSlotCount; ++slot) {
        mdl::ModelRecord* model = app->ModelSlot(slot);
        if (model == nullptr)
            continue;
        const mdl::ModelRecord& record = *model;
        const std::int32_t count = record.boneCount;
        unsigned char* editFlags = record.bonePhysicsState;
        int index = 0;
        while (index < count && editFlags[index] == 0)
            ++index;
        if (index >= count)
            continue;
        SnapshotPoseBeforeFrameChange(model, app->state.currentFrame);
        for (index = 0; index < count; ++index)
            editFlags[index] = 0;
    }
}

void SetFrameEditText(MMDApp* app) {
    const HWND hwnd = static_cast<HWND>(app->Hwnd());
    const HWND edit = GetDlgItem(hwnd, panel::kCurrentFrameEdit);
    const LRESULT length = GetWindowTextLengthA(edit);
    SendMessageA(edit, EM_SETSEL, 0, length);
    char text[0x100];
    sprintf_s(text, sizeof(text), "%d", app->state.currentFrame);
    SendMessageA(edit, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
}

void ApplyFrameToModels(MMDApp* app) {
    // x64 twins run the apply walk as v16 < 255 (0x7FF7CB48424B..290).
    for (int slot = 0; slot < kModelSlotCount; ++slot) {
        mdl::ModelRecord* model = app->ModelSlot(slot);
        if (model == nullptr)
            continue;
        SeekModelFrame(*model, app->state.currentFrame, app->PlaybackPhysicsMode());
        if (slot == app->SelectedModelSlot())
            SyncModelEditControls(model);
    }
}

void RefreshFrameContext(MMDApp* app) {
    const HWND hwnd = static_cast<HWND>(app->Hwnd());
    if (app->CameraMode() != 0) {
        ReloadModels(app);
        RefreshLightPanel(app);
        RefreshSelfShadowPanel(app);
        ApplyGravityTrack(app);
        for (int slot = 0; slot < 0xFF; ++slot) {
            if (app->AccessorySlot(slot) != nullptr)
                ApplyAccessoryTrack(app, slot);
        }
        SyncAccessoryEditPanel(app);
        return;
    }

    if (app->state.followCameraEnabled != 0) {
        app->ViewOffsetX() = 0.0f;
        app->ViewOffsetY() = 0.0f;
        ReloadModels(app);
        RefreshLightPanel(app);
        RefreshSelfShadowPanel(app);
        ApplyGravityTrack(app);
        for (int slot = 0; slot < 0xFF; ++slot) {
            if (app->AccessorySlot(slot) != nullptr)
                ApplyAccessoryTrack(app, slot);
        }
        SyncAccessoryEditPanel(app);
        const std::int32_t selected = app->CameraParentModel();
        if (selected >= 0) {
            mdl::ModelRecord* model = app->ModelSlot(selected);
            if (model != nullptr) {
                ModelApplyMorphs(model);
                SetPhysicsMode(model, 0, app->ModelSlots(), app->PlaybackPhysicsMode());
            }
        }
        PostModelReload(app);
        return;
    }

    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
}

void RefreshTimeline(MMDApp* app, bool forward) {
    const std::uint32_t frame = static_cast<std::uint32_t>(app->state.currentFrame);
    const std::uint32_t visible = static_cast<std::uint32_t>((app->SidebarWidth() - 0x54) / 0x1A);
    app->state.timelineStartFrame =
        frame <= visible ? 0 : static_cast<std::int32_t>(frame - visible);
    PanelPaint(app);

    if (app->WaveEnabled() != 0) {
        TimelineDrawTicks(app->state.timelineStartFrame, app->SidebarWidth());
        RECT rect{6, 95, app->SidebarWidth() - 3, 146};
        InvalidateRect(static_cast<HWND>(app->Hwnd()), &rect, FALSE);
        if (app->state.wavPlaysOnFrameMove != 0) {
            const bool seek = app->AutomaticFrameAdvanceEnabled() == 0;
            app->AudioSeekReady() = 1;
            if (seek) {
                SetFrameNormalized(app->FrameNormalization());
                const std::int32_t previous = app->state.currentFrame - 1;
                double time = static_cast<double>(static_cast<std::uint32_t>(previous)) / 30.0;
                if (time < 0.0)
                    time = 0.0;
                WaveRestartAt(app->Audio(), time);
            }
        }
    }

    if (forward) {
        app->PhysicsResetPending() = app->PlaybackPhysicsMode() == 3 ? 1 : 0;
        if (app->state.aviBackgroundEnabled == 1)
            AviBgOverlayRefresh(app);
    } else {
        if (app->state.aviBackgroundEnabled == 1)
            AviBgOverlayRefresh(app);
        if (app->PlaybackPhysicsMode() == 3)
            app->PhysicsResetPending() = 1;
    }
}

} // namespace

// Advance / move back one frame and run the full apply/refresh chain.  The
// original x86 pair 0x00430F20 (frame +1, command 419) / 0x004312E0
// (frame -1, command 418) differed only in the frame delta (and the 0-guard
// on the way back), so both are covered by this one parameterized step.
// VA 0x00430F20 / 0x4312E0, VA 0x004312E0 - the one-line
// forwarding wrappers this used to be reached through were removed; callers
// pass the direction flag directly.
void StepFrame(MMDApp* app, bool forward) {
    app->CameraAttachmentTransformSuppressed() = 0;
    SnapshotAndClearPoseEdits(app);
    std::int32_t& frame = app->state.currentFrame;
    if (forward)
        ++frame;
    else if (frame != 0)
        --frame;
    SetFrameEditText(app);
    ApplyFrameToModels(app);
    RefreshFrameContext(app);
    RefreshTimeline(app, forward);
}

// VA 0x00432FA0 - re-apply the current frame to every model
// and accessory track after a frame change / key registration and refresh
// the timeline, panels and physics-reset flag (the "frame-apply refresh
// chain" of the original command tails).
void RefreshAfterFrameApply(MMDApp* app) {
    if (app == nullptr)
        return;
    app->CameraAttachmentTransformSuppressed() = 0;
    SnapshotAndClearPoseEdits(app);

    const std::uint32_t frame = app->state.currentFrame;
    const std::uint32_t visible = static_cast<std::uint32_t>((app->SidebarWidth() - 84) / 26);
    app->state.timelineStartFrame =
        frame <= visible ? 0 : static_cast<std::int32_t>(frame - visible);
    PanelPaint(app);
    if (app->WaveEnabled() != 0) {
        TimelineDrawTicks(app->state.timelineStartFrame, app->SidebarWidth());
        RECT rect{6, 95, app->SidebarWidth() - 3, 146};
        InvalidateRect(static_cast<HWND>(app->Hwnd()), &rect, FALSE);
    }

    ApplyFrameToModels(app);
    RefreshFrameContext(app);
    if (app->state.aviBackgroundEnabled == 1)
        AviBgOverlayRefresh(app);
    app->PhysicsResetPending() = 1;
}

} // namespace mikudancestudio
