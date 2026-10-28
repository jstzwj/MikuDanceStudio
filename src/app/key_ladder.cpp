// Keyboard command order follows the MMD 9.32 pump: x64 0x7FF7CB44DEF6..0x7FF7CB450Bxx.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "pump_input.hpp"

namespace mikudancestudio {

namespace {

// The 0x22A read and the edit-focus probe run on the floating viewport
// window when one exists (x86 0x472731 / 0x471E67).
HWND LadderOwner(MMDApp* app) {
    HWND owner = app->FloatingWindow();
    return owner != nullptr ? owner : static_cast<HWND>(app->Hwnd());
}

// The ladder re-dispatches menu command ids through the main window
// (SendMessageA(main, WM_COMMAND, id, 0) at e.g. 0x471F84).
void SendMenuCommand(MMDApp* app, int id) {
    SendMessageA(static_cast<HWND>(app->Hwnd()), WM_COMMAND, id, 0);
}

} // namespace

namespace {

void PumpDisplayShortcuts(MMDApp* app, const KeyboardInputContext& input) {
    auto& state = app->state;
    const auto pressed = [&](LetterKey key) { return app->LetterKeyState(key) == 1; };
    const HWND main = static_cast<HWND>(app->Hwnd());
    const bool focusInEdit = !input.focusNotInPanelEdit;
    const auto focusOK = [&] { return input.focus == main || app->ViewportInputActive() != 0; };
    const auto shift = [&] { return state.shiftModifierState == 3; };
    const auto ctrl = [&] { return state.ctrlModifierState == 3; };
    const auto playing = [&] { return app->PlaybackActive() != 0; };
    const auto modelMode = [&] { return app->CameraMode() == 0; };

    // ---- 'B' (0x471EB5): toggle background color black/white ------------
    // Body = command case 282 minus its letterKeyState[4] write: flip
    // app+0xA0194 and mirror menu item 0x11A.
    if (focusOK() && !focusInEdit && pressed(LetterKey::B)) {
        HMENU menu = GetMenu(main);
        if (state.blackBackgroundEnabled != 0) {
            state.blackBackgroundEnabled = 0;
            CheckMenuItem(menu, MDS_CMD_TOGGLE_BLACK_BACKGROUND, MF_UNCHECKED);
        } else {
            state.blackBackgroundEnabled = 1;
            CheckMenuItem(menu, MDS_CMD_TOGGLE_BLACK_BACKGROUND, MF_CHECKED);
        }
    }

    // ---- 'V' (0x471F37): transparent models / outline / paste ------------
    if (focusOK() && !focusInEdit && pressed(LetterKey::V)) {
        if (ctrl() && !playing()) {
            SendMenuCommand(app, panel::kPasteButton); // case 421 paste
        } else if (shift()) {
            // Body = command case 283 minus letterKeyState[5]: flip the
            // outline-suppression byte and menu 0x11B.
            HMENU menu = GetMenu(main);
            if (state.modelNonDisplayMode != 0) {
                state.modelNonDisplayMode = 0;
                CheckMenuItem(menu, MDS_CMD_TOGGLE_MODEL_NON_DISPLAY, MF_UNCHECKED);
            } else {
                state.modelNonDisplayMode = 1;
                CheckMenuItem(menu, MDS_CMD_TOGGLE_MODEL_NON_DISPLAY, MF_CHECKED);
            }
        } else {
            // Body = command case 0xD6 minus the command bookkeeping: flip
            // app+0x9EB7E, mirror menu 0xD6 and push the flag into every
            // loaded model's displayState (the pump walks the whole slot
            // array, kModelSlotCount wide in both layouts).
            HMENU menu = GetMenu(main);
            state.characterTransparentMode = state.characterTransparentMode != 0 ? 0 : 1;
            CheckMenuItem(menu, MDS_CMD_TOGGLE_MODEL_TRANSPARENCY,
                          state.characterTransparentMode != 0 ? MF_CHECKED : MF_UNCHECKED);
            for (int slot = 0; slot < kModelSlotCount; ++slot) {
                mdl::ModelRecord* model = app->ModelSlot(slot);
                if (model != nullptr)
                    model->displayState = state.characterTransparentMode;
            }
        }
    }

    // ---- 'C' (0x4720B7): copy frames / bone-select toggle ----------------
    if (focusOK() && !playing() && !focusInEdit && pressed(LetterKey::C)) {
        if (ctrl()) {
            SendMenuCommand(app, panel::kCopyFramesButton); // case 420 copy
        } else if (modelMode()) {
            SendMessageA(GetDlgItem(main, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
            SendMessageA(GetDlgItem(main, panel::kBoneMoveRadio), BM_SETCHECK, 0, 0);
            SendMessageA(GetDlgItem(main, panel::kBoneRotateRadio), BM_SETCHECK, 0, 0);
            if (IsDlgButtonChecked(main, panel::kBoneSelectRadio) != 1) {
                app->EditMode() = ViewportEditMode::Bone;
                SendMessageA(GetDlgItem(main, panel::kBoneSelectRadio), BM_SETCHECK, 1, 0);
            } else {
                app->EditMode() = ViewportEditMode::None;
                SendMessageA(GetDlgItem(main, panel::kBoneSelectRadio), BM_SETCHECK, 0, 0);
            }
        }
    }
}

void PumpFrameLineShortcuts(MMDApp* app, const KeyboardInputContext& input) {
    auto& state = app->state;
    const auto pressed = [&](LetterKey key) { return app->LetterKeyState(key) == 1; };
    const HWND main = static_cast<HWND>(app->Hwnd());
    const bool focusInEdit = !input.focusNotInPanelEdit;
    const auto playing = [&] { return app->PlaybackActive() != 0; };
    const auto modelMode = [&] { return app->CameraMode() == 0; };

    // ---- 'H' (0x472324): register all facials (clear all frames, 0xE5) ---
    if (input.focus == main && modelMode() && !playing() && !focusInEdit && pressed(LetterKey::H)) {
        SendMenuCommand(app, MDS_CMD_REGISTER_ALL_MORPH_FRAMES); // case 229
    }

    // ---- 'I' (0x472375): insert bone/camera frame line -------------------
    if (input.focus == main && !playing() && !focusInEdit && pressed(LetterKey::I))
        InsertBoneCameraFrameLine(app); // 0x439E40

    // ---- 'K' (0x4723AB): delete bone/camera frame line -------------------
    if (input.focus == main && !playing() && !focusInEdit && pressed(LetterKey::K))
        DeleteBoneCameraFrameLine(app); // 0x43A650

    // ---- 'U' (0x4723E1): insert facial/light frame line ------------------
    if (input.focus == main && !playing() && !focusInEdit && pressed(LetterKey::U))
        InsertFacialLightFrameLine(app); // 0x43B720

    // ---- 'J' (0x472417): delete facial/light frame line ------------------
    if (input.focus == main && !playing() && !focusInEdit && pressed(LetterKey::J))
        DeleteFacialLightFrameLine(app); // 0x43BB30
}

void PumpViewAndPlaybackShortcuts(MMDApp* app, const KeyboardInputContext& input) {
    auto& state = app->state;
    const auto pressed = [&](LetterKey key) { return app->LetterKeyState(key) == 1; };
    const HWND main = static_cast<HWND>(app->Hwnd());
    const bool focusInEdit = !input.focusNotInPanelEdit;
    const auto focusOK = [&] { return input.focus == main || app->ViewportInputActive() != 0; };
    const auto shift = [&] { return state.shiftModifierState == 3; };
    const auto ctrl = [&] { return state.ctrlModifierState == 3; };
    const auto playing = [&] { return app->PlaybackActive() != 0; };
    const auto modelMode = [&] { return app->CameraMode() == 0; };

    // ---- 'G' (0x4726B2): seek frame / shadow map / fine shadow -----------
    if (focusOK() && !focusInEdit && pressed(LetterKey::G)) {
        if (shift()) {
            // Shift+G: flip the self-shadow map display byte (x86 +0xA0D28,
            // x64 +0xA1DC8).  The render pass reads it together with
            // selfShadowMode > 0 to composite the shadow map texture into
            // the viewport (x64 0x7FF7CB44A5CD).
            app->SelfShadowCompositionEnabled() = app->SelfShadowCompositionEnabled() != 0 ? 0 : 1;
        } else if (ctrl()) {
            FineShadowModeNotice(app); // 0x4726EE
        } else {
            // Same body as command case 553: read the frame edit 0x22A on
            // the floating window when present, atol + clamp >= 0, apply
            // as the current frame (0x980), run the frame-apply chain and
            // echo "%d" into 0x1A1 on the main window.
            char buf[0x100];
            GetWindowTextA(GetDlgItem(LadderOwner(app), panel::kGotoFrameEdit), buf, 0xA);
            long value = std::atol(buf);
            if (value < 0)
                value = 0;
            app->CurrentFrame() = static_cast<std::int32_t>(value);
            RefreshAfterFrameApply(app); // 0x432FA0
            PostViewRefresh(app);        // 0x40D130
            sprintf_s(buf, 0x100u, "%d", app->CurrentFrame());
            SetWindowTextA(GetDlgItem(main, panel::kCurrentFrameEdit), buf);
        }
    }

    // ---- 'L' (0x472804): cycle the transform-channel selector ------------
    // ++app+0x9ED9C, wrapping mod 3 in camera/accessory mode, mod 2 in
    // model mode (the consumer is the mouse-wheel channel chain,
    // ModeCameraAdjust / frame_modes.cpp coordinateSystem).
    if (focusOK() && !focusInEdit && pressed(LetterKey::L)) {
        ++state.coordinateSystem;
        const int limit = modelMode() ? 2 : 3;
        if (state.coordinateSystem >= limit)
            state.coordinateSystem = 0;
    }

    // x86 0x47283F..0x472A49: the arrow-key camera/light navigation (with
    // hold auto-repeat) sits here in the pump, between 'L' and Ctrl+'S'.
    ConsumeArrowKeyNavigation(app, input);

    // ---- Ctrl+'S' (0x472A65): save + bell (no edit-focus gate) -----------
    if (focusOK() && pressed(LetterKey::S) && ctrl()) {
        SendMenuCommand(app, MDS_CMD_SAVE_PROJECT); // case 0xCF save
        MessageBeep(0x40);
    }

    // ---- 'P' (0x472A95): play/stop via the play button 0x198 -------------
    if (focusOK() && app->FrameStepPlayback() == 0 && !focusInEdit && pressed(LetterKey::P)) {
        SendMessageA(GetDlgItem(main, panel::kPlayButton), BM_SETCHECK, playing() ? 0 : 1, 0);
        SendMenuCommand(app, panel::kPlayButton); // case 408 play
        SetFocus(main);
    }

    // ---- 'F' (0x472B2A): paste keyframes to another bone (0xFA) ----------
    if (focusOK() && app->FrameStepPlayback() == 0 && !focusInEdit && pressed(LetterKey::F)) {
        SendMenuCommand(app, MDS_CMD_PASTE_AT_DIFFERENT_FRAME); // case 250
    }
}

void PumpModelShortcuts(MMDApp* app, const KeyboardInputContext& input) {
    auto& state = app->state;
    const auto pressed = [&](LetterKey key) { return app->LetterKeyState(key) == 1; };
    const HWND main = static_cast<HWND>(app->Hwnd());
    const bool focusInEdit = !input.focusNotInPanelEdit;
    const auto focusOK = [&] { return input.focus == main || app->ViewportInputActive() != 0; };
    const auto ctrl = [&] { return state.ctrlModifierState == 3; };
    const auto playing = [&] { return app->PlaybackActive() != 0; };
    const auto modelMode = [&] { return app->CameraMode() == 0; };

    // 0x44FCDB..0x450186: one gate encloses redo, undo, model register,
    // select-all, select-unregistered and the model-offset dialog.
    if (focusOK() && modelMode() && !playing() && !focusInEdit) {
        // ---- 'X' (0x472D5C): redo / rotate mode ------------------------------
        if (pressed(LetterKey::X)) {
            mdl::ModelRecord* model = app->SelectedModel();
            if (ctrl() && model != nullptr && model->redoDirty != 0) {
                // Redo one ring entry (0x4A2490), then enable the undo button
                // / disable redo exactly like the pump tail, and re-seek the
                // frame when the new ring head is not a pose-only record.
                // The original passes &app+0x980 so the redo can move the
                // current frame itself.
                RedoModelEdit(model, app->CurrentFrame());
                if (model->UndoCursor() == model->LatestUndoCursor()) {
                    EnableWindow(GetDlgItem(main, panel::kRedoButton), FALSE);
                    model->redoDirty = 0;
                }
                EnableWindow(GetDlgItem(main, panel::kUndoButton), TRUE);
                model->undoDirty = 1;
                PanelPaint(app);      // 0x414610
                SelectionReeval(app); // 0x430510
                if (model->undoRings[0].slots[model->UndoCursor()].operation !=
                    mdl::UndoOperation::BonePose) {
                    SeekModelFrame(*model, static_cast<int>(app->CurrentFrame()),
                                   app->PlaybackPhysicsMode());
                }
            } else if (!ctrl()) {
                SendMessageA(GetDlgItem(main, panel::kBoneSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneMoveRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneRotateRadio), BM_SETCHECK, 1, 0);
                app->EditMode() = ViewportEditMode::Rotate; // 0x914 = 3
            }
        }

        // ---- 'Z' (0x472ED7): undo / move mode -------------------------------
        if (pressed(LetterKey::Z)) {
            mdl::ModelRecord* model = app->SelectedModel();
            if (ctrl() && model != nullptr && model->undoDirty != 0) {
                // Undo one ring entry (0x4A1870), then the mirror-image button
                // tail of the X branch.  Like the redo, the frame pointer aims
                // straight at app+0x980.
                UndoModelEdit(model, app->CurrentFrame());
                if (model->undoRings[0].slots[model->UndoCursor()].operation ==
                    mdl::UndoOperation::Empty) {
                    EnableWindow(GetDlgItem(main, panel::kUndoButton), FALSE);
                    model->undoDirty = 0;
                }
                if (model->UndoCursor() == model->LatestUndoCursor()) {
                    EnableWindow(GetDlgItem(main, panel::kUndoButton), FALSE);
                    model->undoDirty = 0;
                }
                EnableWindow(GetDlgItem(main, panel::kRedoButton), TRUE);
                model->redoDirty = 1;
                PanelPaint(app);      // 0x414610
                SelectionReeval(app); // 0x430510
            } else if (!ctrl()) {
                SendMessageA(GetDlgItem(main, panel::kBoneSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneRotateRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneMoveRadio), BM_SETCHECK, 1, 0);
                app->EditMode() = ViewportEditMode::Move; // 0x914 = 4
            }
        }

        PumpModelEnterRegister(app);

        // ---- 'A' (0x473092): select all bones (0x1EE) ------------------------
        if (pressed(LetterKey::A))
            SendMenuCommand(app, panel::kSelectAllBonesButton); // case 494

        // ---- plain 'S' (0x4730B1): select unregistered bones (0x1F5) ---------
        if (pressed(LetterKey::S) && !ctrl())
            SendMenuCommand(app, panel::kSelectUnregisteredBonesButton); // case 501

        // ---- 'D' (0x4730D9): model-offset dialog (0xDB) ----------------------
        if (pressed(LetterKey::D))
            SendMenuCommand(app, MDS_CMD_CENTER_POSITION_BIAS); // case 219
    }
}

void PumpFrameDialogShortcut(MMDApp* app, const KeyboardInputContext& input) {
    auto& state = app->state;
    const auto pressed = [&](LetterKey key) { return app->LetterKeyState(key) == 1; };
    const HWND main = static_cast<HWND>(app->Hwnd());
    const bool focusInEdit = !input.focusNotInPanelEdit;
    const auto focusOK = [&] { return input.focus == main || app->ViewportInputActive() != 0; };
    const auto modelMode = [&] { return app->CameraMode() == 0; };

    // ---- 'R' (0x473989): frame control dialog (0xFB) ---------------------
    if (focusOK() && modelMode() && app->FrameStepPlayback() == 0 && !focusInEdit &&
        pressed(LetterKey::R)) {
        SendMenuCommand(app, MDS_CMD_MULTIPLY_BONE_FRAMES); // case 251
    }
}

} // namespace

void ConsumeKeyboardInput(MMDApp* app, const KeyboardInputContext& input) {
    PumpDisplayShortcuts(app, input);
    PumpInterpolationToggle(app, input.focus, input.focusNotInPanelEdit);
    PumpDeleteRebuild(app, input.focus, input.focusNotInPanelEdit);
    PumpFrameLineShortcuts(app, input);
    PumpTabCycle(app, input.focus, input.focusNotInPanelEdit);
    PumpFullscreenKeys(app, input.focus, input.focusNotInPanelEdit);
    PumpViewAndPlaybackShortcuts(app, input);
    PumpGlobalEnterRegister(app, input.focus, input.focusNotInPanelEdit);
    PumpModelShortcuts(app, input);
    PumpEscStop(app, input.focusNotInPanelEdit);
    ConsumeNumpadViewPresets(app, input);
    PumpFrameDialogShortcut(app, input);
}

void ConsumeKeyboardInput(MMDApp* app) {
    KeyboardInputContext input{};
    input.focus = GetFocus();
    input.focusNotInPanelEdit = !PumpPanelFocusChain(app, input.focus);
    ConsumeKeyboardInput(app, input);
}

} // namespace mikudancestudio
