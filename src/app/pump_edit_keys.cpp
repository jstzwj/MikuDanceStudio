// Panel focus traversal and edit-key actions used by the keyboard sequencer.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "pump_input.hpp"

namespace mikudancestudio {

namespace {

// The 0x220..0x226 / 0x22A probes run against the floating viewport
// window when one exists (x64 0x44EB00..0x44EB23 / 0x44ECD7).
HWND PanelEditOwner(MMDApp* app) {
    HWND owner = app->FloatingWindow();
    return owner != nullptr ? owner : static_cast<HWND>(app->Hwnd());
}

// Common focus-OK gate recomputed by every segment exactly as the pump
// does (focus == main window || viewport input active byte).
bool FocusOk(MMDApp* app, HWND focus) {
    return focus == static_cast<HWND>(app->Hwnd()) ||
           app->ViewportInputActive() != 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// G12 - panel focus chain (x86 0x471342..0x471EB5, x64 0x44DEF6..0x44ED16).
// `focus` is the GetFocus() snapshot the pump caches before any block runs
// (x64 0x44DEF8).  Returns true when the focus ended up being inside one
// of the panel edits, i.e. the inverse of the r13 flag the segments below
// receive as focusNotInPanelEdit.
// ---------------------------------------------------------------------------
bool PumpPanelFocusChain(MMDApp* app, HWND focus) {
    auto& state = app->state;
    const HWND main = static_cast<HWND>(app->Hwnd());
    const HWND owner = PanelEditOwner(app);
    const bool tab = state.tabKeyState == 1;             // +0x80 cell
    const bool shift = state.shiftModifierState == 3;    // +0x24 cell
    const bool cameraMode = state.optflag[0] != 0;       // +0x2F8 byte
    bool notInEdit = true;                               // bl at 0x44DEF6

    const auto focusIs = [&](HWND on, int id) {
        return GetDlgItem(on, id) == focus;
    };
    // SetFocus + optional select-all + optional beep; the original
    // re-fetches GetDlgItem before every call, kept.
    const auto moveTo = [&](HWND on, int id, bool selectAll, bool beep) {
        SetFocus(GetDlgItem(on, id));
        if (selectAll) {
            const LRESULT len =
                GetWindowTextLengthA(GetDlgItem(on, id));
            SendMessageA(GetDlgItem(on, id), EM_SETSEL, 0, len);
        }
        if (beep)
            MessageBeep(0);
    };
    // The 0x1DA / 0x1C1 / 0x1DB / 0x1C2 rows park focus on the panel root
    // control (id 0) when any arrow key fires (order: LEFT, RIGHT, UP,
    // DOWN - independent ifs, several can fire in one frame).
    const auto arrowsToPanelRoot = [&]() {
        if (state.leftKeyState == 1)
            SetFocus(GetDlgItem(main, 0));
        if (state.rightKeyState == 1)
            SetFocus(GetDlgItem(main, 0));
        if (state.upKeyState == 1)
            SetFocus(GetDlgItem(main, 0));
        if (state.downKeyState == 1)
            SetFocus(GetDlgItem(main, 0));
    };

    // ---- 0x44DF19: frame-scale edits 0x199 <-> 0x19A ----------------------
    if (focusIs(main, 0x199)) {
        if (tab)
            moveTo(main, 0x19A, true, false);
        notInEdit = false;
    }
    if (focusIs(main, 0x19A)) {
        if (tab)
            moveTo(main, 0x199, true, false);
        notInEdit = false;
    }
    // ---- 0x44E00F: play-button row edit 0x198 -> 0x199 (with beep) --------
    if (focusIs(main, 0x198)) {
        if (tab)
            moveTo(main, 0x199, true, true);
        notInEdit = false;
    }
    // ---- 0x44E0B1: frame edit 0x1A1 is gated but has no TAB move ----------
    if (focusIs(main, 0x1A1))
        notInEdit = false;
    // ---- 0x44E0C8 / 0x44E142: 0x1A9 <-> 0x1AA -----------------------------
    if (focusIs(main, 0x1A9)) {
        if (tab)
            moveTo(main, 0x1AA, true, false);
        notInEdit = false;
    }
    if (focusIs(main, 0x1AA)) {
        if (tab)
            moveTo(main, 0x1A9, true, false);
        notInEdit = false;
    }
    // ---- 0x44E1C9: 0x19F -> 0x1A9 (camera mode, select-all) or 0x1A8 ------
    if (focusIs(main, 0x19F)) {
        if (tab) {
            if (cameraMode)
                moveTo(main, 0x1A9, true, true);
            else
                moveTo(main, 0x1A8, false, true);
        }
        notInEdit = false;
    }
    // ---- 0x44E281: 0x1A8 -> 0x1A9 ----------------------------------------
    if (focusIs(main, 0x1A8)) {
        if (tab)
            moveTo(main, 0x1A9, true, false);
        notInEdit = false;
    }

    // ---- 0x44E326 loop: 0x1CD..0x1D2, wrapping 0x1D2 <-> 0x1CD -----------
    for (int id = 0x1CD; id <= 0x1D2; ++id) {
        if (!focusIs(main, id))
            continue;
        if (tab) {
            const int target = shift
                ? (id == 0x1CD ? 0x1D2 : id - 1)
                : (id == 0x1D2 ? 0x1CD : id + 1);
            moveTo(main, target, true, false);
        }
        notInEdit = false;
    }
    // ---- 0x44E4A4 loop: 0x1DE..0x1E4; 0x1E5 commits below ----------------
    for (int id = 0x1DE; id < 0x1E5; ++id) {
        if (!focusIs(main, id))
            continue;
        if (tab) {
            const int target = shift
                ? (id == 0x1DE ? 0x1E5 : id - 1)
                : id + 1;
            moveTo(main, target, true, false);
        }
        notInEdit = false;
    }

    // ---- 0x44E5D0..0x44E65E: probes without TAB moves (bl only) ----------
    if (focusIs(main, 0x1FA) || focusIs(main, 0x1FF) ||
        focusIs(main, 0x204) || focusIs(main, 0x209) ||
        focusIs(main, 0x231))
        notInEdit = false;

    // ---- 0x44E659: 0x1E5 - forward TAB commits the edit -------------------
    if (focusIs(main, 0x1E5)) {
        if (tab) {
            if (shift) {
                moveTo(main, 0x1E4, true, false);
            } else {
                moveTo(main, 0x1DE, true, false);
                CommitEditControl(app, GetDlgItem(main, panel::kAccScaleYEdit));     // 0x463640
            }
        }
        notInEdit = false;
    }

    // ---- 0x44E766 / 0x44E837 / 0x44E908 / 0x44EA1F: camera rows ----------
    // 0x1DA -> 0x1DB (no select-all), 0x1C1 -> 0x1C2 (no select-all),
    // 0x1DB -> 0x1DE (select-all), 0x1C2 -> 0x1C1 (no select-all); each
    // beeps and hands the arrow keys to the panel root.
    if (focusIs(main, 0x1DA)) {
        if (tab)
            moveTo(main, 0x1DB, false, true);
        arrowsToPanelRoot();
        notInEdit = false;
    }
    if (focusIs(main, 0x1C1)) {
        if (tab)
            moveTo(main, 0x1C2, false, true);
        arrowsToPanelRoot();
        notInEdit = false;
    }
    if (focusIs(main, 0x1DB)) {
        if (tab)
            moveTo(main, 0x1DE, true, true);
        arrowsToPanelRoot();
        notInEdit = false;
    }
    if (focusIs(main, 0x1C2)) {
        if (tab)
            moveTo(main, 0x1C1, false, true);
        arrowsToPanelRoot();
        notInEdit = false;
    }

    // ---- 0x44EAF0 loop: 0x220..0x226 on the panel owner ------------------
    // The chain end depends on the mode: 0x226 in camera/accessory mode,
    // 0x225 in model mode (the extra edit only exists there).
    const int chainEnd = cameraMode ? 0x226 : 0x225;
    for (int id = 0x220; id <= 0x226; ++id) {
        if (!focusIs(owner, id))
            continue;
        if (tab) {
            const int target = shift
                ? (id == 0x220 ? chainEnd : id - 1)
                : (id == chainEnd ? 0x220 : id + 1);
            moveTo(owner, target, true, false);
        }
        notInEdit = false;
    }
    // ---- 0x44ECD7: frame edit 0x22A on the panel owner -------------------
    if (focusIs(owner, 0x22A))
        notInEdit = false;

    return !notInEdit;
}

// ---------------------------------------------------------------------------
// G6 - ']' interpolation toggle (x86 0x4721C9, x64 0x44F06A..0x44F106).
// Gate: focusOK && VK221 pressed && !playing && !panel-edit focus.
// Body: flip checkbox 0x212 (530, the register-reset checkbox probed by
// RegisterCameraState).
// ---------------------------------------------------------------------------
void PumpInterpolationToggle(MMDApp* app, HWND focus,
                             bool focusNotInPanelEdit) {
    if (!(FocusOk(app, focus) && app->state.keyState221 == 1 &&
          app->PlaybackActive() == 0 && focusNotInPanelEdit))
        return;
    const HWND main = static_cast<HWND>(app->Hwnd());
    const HWND box = GetDlgItem(main, panel::kPhysicsFrameCheckbox);
    const LRESULT checked = SendMessageA(box, BM_GETCHECK, 0, 0);
    SendMessageA(box, BM_SETCHECK, checked == 1 ? 0 : 1, 0);
}

// ---------------------------------------------------------------------------
// G8 - DELETE model-edit rebuild (x86 0x472246..0x472313,
// x64 0x44F106..0x44F1E8).
// Gate: focus == main && DELETE pressed && !playing && !panel-edit focus.
// Body: a FRESH GetFocus (the chain above may have moved it after the
// gate's snapshot was taken) must not sit in any of the five frame edits
// {0x1AA, 0x1A9, 0x1A1, 0x19A, 0x199}, then DeleteMarkedKeyframes rebuilds the
// model-edit state.
// ---------------------------------------------------------------------------
void PumpDeleteRebuild(MMDApp* app, HWND focus, bool focusNotInPanelEdit) {
    const HWND main = static_cast<HWND>(app->Hwnd());
    if (!(focus == main && app->state.deleteKeyState == 1 &&
          app->PlaybackActive() == 0 && focusNotInPanelEdit))
        return;
    const HWND current = GetFocus();                      // 0x44F13F
    static const int kFrameEdits[] = {0x1AA, 0x1A9, 0x1A1, 0x19A, 0x199};
    for (int id : kFrameEdits) {
        if (GetDlgItem(main, id) == current)
            return;
    }
    DeleteMarkedKeyframes(app);                                       // 0x4316B0
}

// ---------------------------------------------------------------------------
// G5 - TAB / VK226 combo cycle (x86 0x47244E..0x472524 and
// 0x47252B..0x472609, x64 0x44F32D..0x44F42E and 0x44F42E..0x44F52F).
// Gate per half: focusOK && cell pressed && !playing && !panel-edit focus
// && frame-range dialog closed (0xA0B50).  Body: step the model combo
// 0x1B4 (436) selection - Shift steps back with a wrap to count-1,
// plain steps forward with a wrap to 0 - then CB_SETCURSEL and apply via
// ApplyModelComboSelection (ui_model_reload.cpp).
// ---------------------------------------------------------------------------
void PumpTabCycle(MMDApp* app, HWND focus, bool focusNotInPanelEdit) {
    const auto cycle = [&](bool cellPressed) {
        if (!cellPressed || !FocusOk(app, focus) ||
            app->PlaybackActive() != 0 || !focusNotInPanelEdit ||
            app->FrameRangeDialog() != nullptr)
            return;
        const HWND main = static_cast<HWND>(app->Hwnd());
        const HWND combo = GetDlgItem(main, panel::kMainComboModel);
        int index;
        if (app->state.shiftModifierState == 3) {
            index = static_cast<int>(
                         SendMessageA(combo, CB_GETCURSEL, 0, 0)) - 1;
            if (index < 0)
                index = static_cast<int>(
                            SendMessageA(combo, CB_GETCOUNT, 0, 0)) - 1;
        } else {
            index = static_cast<int>(
                         SendMessageA(combo, CB_GETCURSEL, 0, 0)) + 1;
            if (index >= static_cast<int>(
                             SendMessageA(combo, CB_GETCOUNT, 0, 0)))
                index = 0;
        }
        SendMessageA(combo, CB_SETCURSEL, index, 0);
        ApplyModelComboSelection(app);                                   // 0x44D940
    };
    cycle(app->state.tabKeyState == 1);                   // x64 0x44F32D
    cycle(app->state.keyState226 == 1);                   // x64 0x44F42E
}

// ---------------------------------------------------------------------------
// G7 - Alt+Enter fullscreen toggle and ESC exit (x86 0x472633..0x472698,
// x64 0x44F52F..0x44F594 and 0x44F594..0x44F5D4).
// Enter half: frame-step idle && viewport active && !panel-edit focus &&
// Alt held && RETURN pressed -> flip the fullscreen byte 0xA0274 and run
// the fullscreen window manager (ApplyFullscreenWindowState, avi_record_start.cpp) plus
// the device reset (PostDeviceReset).
// ESC half: fullscreen && viewport active && !panel-edit focus -> clear
// the byte and run the same two calls.
// ---------------------------------------------------------------------------
void PumpFullscreenKeys(MMDApp* app, HWND /*focus*/,
                        bool focusNotInPanelEdit) {
    auto& state = app->state;
    if (app->FrameStepPlayback() == 0 &&
        app->ViewportInputActive() != 0 &&
        focusNotInPanelEdit &&
        state.menuKeyState == 3 &&                         // Alt held
        state.enterKeyState == 1) {                                   // RETURN
        app->FullscreenMode() =
            app->FullscreenMode() == 0 ? 1 : 0;            // 0xA0274
        ApplyFullscreenWindowState(app);                                    // 0x4629D0
        PostDeviceReset(app);                              // 0x440DB0
    }
    if (app->FullscreenMode() != 0 &&
        app->ViewportInputActive() != 0 &&
        state.escKeyState == 1 &&
        focusNotInPanelEdit) {
        app->FullscreenMode() = 0;                         // 0xA0274
        ApplyFullscreenWindowState(app);                                    // 0x4629D0
        PostDeviceReset(app);                              // 0x440DB0
    }
}

// ---------------------------------------------------------------------------
// Global-track Enter registration (x64 0x44FB0A..0x44FCDB).
void PumpGlobalEnterRegister(MMDApp* app, HWND focus,
                            bool focusNotInPanelEdit) {
    auto& state = app->state;
    const bool enterPressed = state.enterKeyState == 1;               // +0xBC cell
    const bool altHeld = state.menuKeyState == 3;          // +0xC4 cell

    // ---- G2: camera/accessory mode register ------------------------------
    if (FocusOk(app, focus) && app->PlaybackActive() == 0 &&
        state.optflag[0] != 0 && focusNotInPanelEdit &&
        enterPressed && !altHeld) {
        app->SceneModified() = 1;                          // 0xA0B0D
        for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
            app->CameraKeys()[i].selected = 0;             // 0x374 table
            app->LightKeys()[i].selected = 0;              // 0x378 table
            app->ShadowKeys()[i].selected = 0;             // 0x37C table
            app->GravityKeys()[i].selected = 0;            // 0x380 table
        }
        for (int slot = 0; slot < 0xFF; ++slot)
            for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i)
                app->AccessoryKeys(slot)[i].selected = 0;  // 0x384 table

        const std::int32_t frame = app->CurrentFrame();    // 0x980
        if (app->GlobalTrackSelected(GlobalTimelineTrack::Camera) != 0)
            RegisterCameraState(app, frame);               // 0x410560
        if (app->GlobalTrackSelected(GlobalTimelineTrack::Light) != 0)
            RegisterLightState(app, frame);                // 0x411630
        if (app->GlobalTrackSelected(GlobalTimelineTrack::SelfShadow) != 0)
            RegisterSelfShadowState(app, frame);           // 0x411DF0
        if (app->GlobalTrackSelected(GlobalTimelineTrack::Gravity) != 0)
            RegisterGravityKeyCurrent(app, frame);         // 0x412B20
        for (int slot = 0; slot < 0xFF; ++slot) {
            mdl::AccessoryRecord* accessory = app->AccessorySlot(slot);
            if (accessory != nullptr && accessory->rowSelected != 0)
                RegisterAccessoryKey(app, frame, slot);               // 0x413CB0
        }
        PanelPaint(app);                                   // 0x414610
        SelectionReeval(app);                              // 0x430510
    }

}

// Called only inside the sequencer's model-edit gate, after undo/redo.
void PumpModelEnterRegister(MMDApp* app) {
    if (app->state.enterKeyState == 1 && app->state.menuKeyState != 3)
        SendMessageA(static_cast<HWND>(app->Hwnd()), WM_COMMAND, 0x1F4, 0);
}

// ---------------------------------------------------------------------------
// G4 - ESC stops a frame-step recording (x86 0x4730F8..0x473127,
// x64 0x450186..0x4501B5).  Gate: viewport active && frame-step flag set
// && ESC pressed && !panel-edit focus (no focus-OK, no playing gate).
// ---------------------------------------------------------------------------
void PumpEscStop(MMDApp* app, bool focusNotInPanelEdit) {
    if (app->ViewportInputActive() != 0 &&
        app->FrameStepPlayback() != 0 &&
        app->state.escKeyState == 1 &&
        focusNotInPanelEdit)
        FinishAviRecord(app);                                    // 0x464A00
}

}  // namespace mikudancestudio
