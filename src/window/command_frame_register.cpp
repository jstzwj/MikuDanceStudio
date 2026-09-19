// ===========================================================================
// CommandDispatch family: control notifications 500..567 (0x47E8A0)
// ===========================================================================
// Cases 500..567 (0x1F4..0x237) of the 368-case switch in CommandDispatch
// (0x47E8A0, WM_COMMAND target from MainWndProc 0x4C3A10): the editor-panel
// control-notification block.  Dispatch: id = LOWORD(wParam);
// idx = byte_48F650[id-0xC8]; target = jpt_47E903[idx].  `this` is the
// g_Block global; the switch is on the control id only (the notification
// code in HIWORD(wParam) is never inspected by these cases).
//
// Case behavior table (id -> behavior -> original VA range):
//   500  register-frame button 0x1F4: dirty 0xA0B0D=1, keyframe-register
//        sub_4C2080(model, frame 0x980, 0xA0CC4), 0x9E16C max vs model+0x31B0,
//        PanelPaint(0x414610) + SelectionReeval(0x430510)     0x48026F-0x4802C7
//   501  "all frames" button 0x1F5: BM_SETCHECK(0x1EA,1), re-dispatch
//        WM_COMMAND 0x1EA, model+0x2D90=-1, per-bone 0x2D94[i]=
//        (0x2D98[i]!=0), PostLanguageSweep(0x42F1E0) +
//        PostLanguageSweep2(0x40D070)                         0x47F93D-0x47F9F9
//   502  option-flag checkbox 0x1FC: byte 0x2FC = 0; PostModelReload2
//        (0x40D940) + HandleWindowSize(0x443300) + InvalidateRect(main)
//                                                            0x47EA70-0x47EA77
//   503  checkbox 0x1FC twin: byte 0x2FC = 1; same refresh chain
//                                                            0x47EA79-0x47EA80
//   504..506  no jump-table target; 504 (0x1F8 combo) rides the
//        dispatcher's default CBN_SELCHANGE chain (command_dispatch.cpp
//        DefaultSelChangeChain 0x48EA48); 505/506 (slider/edit) never emit
//        WM_COMMAND - empty here
//   507  morph-row X combo prev 0x1FB: require a selected morph and loaded
//        morph table, step down with wrap, skipping morphs outside panel 1;
//        move combo 0x1F8 cursor back
//        (CB_SETCURSEL 0x14E), spin 0x1F9 = (int)(v*100.0) (TBM_SETPOS
//        0x405), text 0x1FA "%5.4f"                           0x48C735-0x48C93C
//   508  bone X combo next 0x1FC: same gates, step up with wrap 0, cursor
//        wraps (count-1 -> 0), same spin/text echo              0x48C941-0x48CB4B
//   509..511  no jump-table target; 509 (0x1FD combo) rides the default
//        chain (0x48EC56); 510/511 empty here
//   512  bone Y combo prev 0x200: axis 0x2DA0, row 0x1FD/0x1FE/0x1FF
//                                                            0x48CB4E-0x48CD4D
//   513  bone Y combo next 0x201: axis 0x2DA0, row 0x1FD/0x1FE/0x1FF
//                                                            0x48CD50-0x48CF4E
//   514..516  no jump-table target; 514 (0x202 combo) rides the default
//        chain (0x48EE66); 515/516 empty here
//   517  bone Z combo prev 0x205: axis 0x2DA4, row 0x202/0x203/0x204
//                                                            0x48CF51-0x48D14E
//   518  bone Z combo next 0x206: axis 0x2DA4, row 0x202/0x203/0x204
//                                                            0x48D151-0x48D349
//   519..521  no jump-table target; 519 (0x207 combo) rides the default
//        chain (0x48F066); 520/521 empty here
//   522  bone rot combo prev 0x20A: axis 0x2DA8, row 0x207/0x208/0x209
//                                                            0x48D34C-0x48D54D
//   523  bone rot combo next 0x20B: axis 0x2DA8, row 0x207/0x208/0x209
//                                                            0x48D550-0x48D84C
//   524  frame-register X 0x20C: dirty, clear bone(0x26E0 +0x38/0x3C-step
//        kBoneKeyCapacity x 0x3C bytes)/morph(0x26E4 +0x10/0x14-step 0x61A80)/IK(0x26E8 +0x14/
//        0x1C-step 0x6D60) flags, sub_49EEE0(model, 0x2DA0, frame), 0x9E16C
//        max, PanelPaint                                       0x4803B1-0x48048C
//   525  frame-register X 0x20D: same with axis 0x2D9C
//                                                            0x4802CC-0x4803AC
//   526  frame-register X 0x20E: same with axis 0x2DA8
//                                                            0x480571-0x48064C
//   527  frame-register X 0x20F: same with axis 0x2DA4
//                                                            0x480491-0x48056C
//   528  option-flag checkbox 0x210: byte 0x2FD = 0; PostModelReload2 +
//        HandleWindowSize + InvalidateRect                     0x47EA82-0x47EA95
//   529  checkbox 0x211: byte 0x2FD = 1; same refresh chain
//                                                            0x47EA9A-0x47EAA1
//   530  no target - empty
//   531  physics checkbox 0x213: BM_GETCHECK(0x213); checked: 0x340=2,
//        BM_SETCHECK(0x19C,0), ApplyCameraReferenceModeChange(app, old
//        0x340) unless 0x2F8; unchecked: 0x340=0,
//        ApplyCameraReferenceModeChange(app, old)              0x47FA92-0x47FAE2
//   532  button 0x214: 0x441070(app)                          0x48BC87-0x48BC8C
//   533  button 0x215: sub_4414C0(app)                         0x48BC91-0x48BC96
//   534  no target - empty
//   535  display-mode checkbox 0x217: BM_GETCHECK(0x217); checked: 0x9ED98=1,
//        camera floats 0x308/0x30C=0, CheckMenuItem(0xF7, MF_CHECKED),
//        ReloadModels(0x42E640) + 0x411070/0x411B90/0x412330 + slot loop
//        0x413120 + 0x4134E0, 0xA0478=0 + PostModelReload(0x41A650) when
//        slot 0xA0430 matches, PostLanguageSweep2; unchecked: 0x9ED98=0,
//        CheckMenuItem(0xF7, 0), same slot-gated reload           0x48BE6A-0x48BF6A
//   536  combo 0x1B4 sync 0x218: display mode: 0xA042C==0 -> dispatch
//        WM_COMMAND 0x1B3, else CB_SETCURSEL(0x1B4, 0xA042C) +
//        sub_44D940(app); edit mode: CB_SETCURSEL(0x1B4, 0) + sub_44D940
//                                                            0x48BF6F-0x48BFF2
//   537  bone pos-X edit 0x219: display: 0x334=0, RefreshRequest(-1),
//        PostViewRefresh; edit: selected bone 0x2D90, sub_42D6E0(app),
//        boneTable[sel].trans[0] = 0, marks the bone dirty, PostViewRefresh
//                                                            0x48BFF7-0x48C07C
//   538  bone pos-Y edit 0x21A: same, 0x338 / +0x144       0x48C081-0x48C106
//   539  bone pos-Z edit 0x21B: same, 0x33C / +0x148       0x48C10B-0x48C190
//   540  bone rot-X edit 0x21C: display: 0x310=0, RefreshRequest(-1),
//        PostViewRefresh; edit: sub_42D6E0(app), Euler (0, -rY*pi/180,
//        -180/(rZ*pi)) -> Rz*Rx*Ry matrix -> D3DXQuaternionRotationMatrix
//        into boneTable[sel].rotQuat, marks the bone dirty, PostViewRefresh
//                                                            0x48C195-0x48C2DF
//   541  bone rot-Y edit 0x21D: same chain, Euler (rX*pi/180, 0,
//        -180/(rZ*pi))                                         0x48C2E4-0x48C430
//   542  bone rot-Z edit 0x21E: same chain, Euler (rX*pi/180,
//        -180/(rY*pi), 0)                                      0x48C435-0x48C52A
//   543  camera-angle edit 0x21F: display: 0xA08DC=0, RefreshRequest(-1),
//        PostViewRefresh; edit: PostViewRefresh only              0x48C52F-0x48C552
//   544..550  no target - empty
//   551  play checkbox 0x227: byte 0x31E != 0 -> 0x31E=0, CheckMenuItem
//        (0xD3, 0); else 0x31E=1, floats 0x320=0/0x324=0, CheckMenuItem
//        (0xD3, 8)                                            0x48C5AE-0x48C60D
//   552  checkbox 0x228: menu 0x12B state&8 -> CheckMenuItem(0x12B, 0),
//        0xA4420=0; else CheckMenuItem(0x12B, 8), 0xA4420=1
//                                                            0x48C612-0x48C674
//   553  frame edit 0x229 (EN_CHANGE): atol(GetWindowTextA(0x22A, 10)),
//        clamp >= 0, 0x980 = v, sub_432FA0(app), PostViewRefresh, echo
//        "%d" into 0x1A1                                       0x48DECB-0x48DF5A
//   554  no target - empty
//   555  frame edit 0x22B echo: "%d" (0x980) -> SetWindowTextA(0x22A)
//                                                            0x48DE84-0x48DEC6
//   556  checkbox 0x22C (BM_GETCHECK-probed by HandleNotify 0x4398B0):
//        locale 0xA06C4: checked -> renderTargetWidth/renderTargetHeight
//        (0x1D558/0x1D55C) = 0x1000, unchecked ->
//        0x800; release (vtable+8) + null hdrTexture/shadowDepthSurface/
//        shadowSurface (0x1D548/0x1D554/0x1D550)
//                                                            0x48DF5D-0x48E015
//   557  morph-display checkbox 0x22D: byte 0x31D toggle + CheckMenuItem
//        (0xD7, 0/8)                                          0x48C557-0x48C5A9
//   558  frame-reset button 0x22E: 0x980 = 0, sub_432FA0(app),
//        PostViewRefresh, echo "%d" into 0x1A1                0x48C679-0x48C6D1
//   559  frame-end button 0x22F: 0x980 = 0x9E16C, sub_432FA0(app),
//        PostViewRefresh, echo "%d" into 0x1A1                0x48C6D6-0x48C730
//   560..561  no target - empty (WM_HSCROLL pair, see ui_hscroll.cpp)
//   562  coord radio 0x232: BM_SETCHECK 0x232=1/0x233=0/0x234=0,
//        0xA0D30 = 0, RefreshRequest(-3)                      0x48D84F-0x48D8BC
//   563  coord radio 0x233: BM_SETCHECK 0x232=0/0x233=1/0x234=0,
//        0xA0D30 = 1, RefreshRequest(-3)                      0x48D8C1-0x48D92E
//   564  coord radio 0x234: BM_SETCHECK 0x232=0/0x233=0/0x234=1,
//        0xA0D30 = 2, RefreshRequest(-3)                      0x48D933-0x48D9A0
//   565  physics-interval edit 0x235: atof(GetWindowTextA(0x231, 8)),
//        0xA0D2C = (10000-v)/100000, TBM_SETPOS(0x230, 1, (int)v),
//        dirty 0xA0B0D=1, selection-bitmap clear (0x374/0x378/0x37C/0x380
//        + 0x384 slots), sub_411DF0(app, frame), RefreshRequest(-3),
//        PanelPaint                                           0x48D9A5-0x48DAC6
//   566  option-flag checkbox 0x236: byte 0x2FE = 0; refresh chain
//                                                            0x47EAA6-0x47EAAD
//   567  checkbox 0x237: byte 0x2FE = 1; refresh chain
//                                                            0x47EAAF-0x47EAC2
//
// Reference: ../translated/MikuMikuDance/fcn_0047e8a0.cpp
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <commctrl.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// ---- helpers ported in other translation units (declared with original
//      VAs; not yet registered in ported_funcs.hpp) -------------------------
void PanelPaint(MMDApp* app);                                   // VA 0x00414610
void RefreshRequest(int area);                                  // VA 0x00440AC0
void SelectionReeval(MMDApp* app);                              // VA 0x00430510 (stubs.cpp)
void RefreshLightPanel(MMDApp* app);                                    // VA 0x00411070 (ui_frame_refresh.cpp)
void RefreshSelfShadowPanel(MMDApp* app);                                    // VA 0x00411B90 (ui_frame_refresh.cpp)
void ApplyGravityTrack(MMDApp* app);                             // VA 0x00412330 (was
                                                                  //  0x412330; track_apply.cpp)
void ApplyAccessoryTrack(MMDApp* app, int slot);                  // VA 0x00413120 (was
                                                                  //  0x413120; accessory_paste.cpp)
void SyncAccessoryEditPanel(MMDApp* app);                                    // VA 0x004134E0 (ui_frame_refresh.cpp)
void ReloadModels(MMDApp* app);                    // VA 0x0042E640 (timeline_advance.cpp)

// ---- not-yet-ported original call targets: declarations only; the stub
//      bodies are consolidated in src/app/late_ports.cpp (finishing phase)
// ---------------------------------------------------------------------------
// VA 0x004414C0 - timeline "previous registration" jump (thiscall, app).
void JumpPrevKeyframe(MMDApp* app);
// VA 0x0044D940 - accessory-combo apply helper (thiscall, this = app).
void ApplyModelComboSelection(MMDApp* app);
// VA 0x00441070 - timeline "next registration" jump (thiscall, this = app;
//                 called by case 532 at 0x48BC87 - distinct from sub_411070).
void JumpNextKeyframe(MMDApp* app);
// VA 0x0041ACD0 - camera-reference switch re-anchor (thiscall(app, old mode
//                 byte); ).
void ApplyCameraReferenceModeChange(MMDApp* app, int oldMode);
// VA 0x0042D6E0 - bone-edit undo snapshot push (thiscall, this = app; was
//                 0x42D6E0).
void PushBoneEditUndo(MMDApp* app);
// VA 0x004C2080 - frame keyframe-register (thiscall on the model, frame,
//                 3rd arg = app+0xA0CC4; ).
void RegisterSelectedBoneKeys(unsigned char* model, int frame, int mode);
// VA 0x0049EEE0 - bone-frame register (thiscall on the model: bone index,
//                 frame).
void RegisterMorphKeyCurrent(unsigned char* model, int idx, int frame);
// VA 0x00432FA0 - frame-apply refresh chain (thiscall, this = app).
void RefreshAfterFrameApply(MMDApp* app);  //  (ui_frame_step.cpp)
// VA 0x00411DF0 - frame-scroll apply (thiscall(app, frame 0x980)).
void RegisterSelfShadowState(MMDApp* app, int frame);
                                                       // (ui_frame_refresh.cpp)

namespace {

// Active model = slot array at app+0x780 indexed by byte app+0x910.
// (sub_47E8A0 pattern; the original reloads the pointer at every use, kept.)
static unsigned char* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

// Combo/spin/text echo shared by the morph prev/next handlers (tails at
// 0x48C832 / 0x48CA3E / 0x48CC66 / 0x48CE66 / 0x48D067 / 0x48D466):
//   count = CB_GETCOUNT(combo); cur = CB_GETCURSEL(combo)
//   prev: cur == 0 -> CB_SETCURSEL(count-1), else cur-1
//   next: cur == count-1 -> CB_SETCURSEL(0), else cur+1
//   v = selected morph's current value; update the slider and edit text
//   (dbl_52B8E0 = 100.0); SetWindowTextA(text, "%5.4f").
void SyncMorphRow(MMDApp* app, HWND hwnd, std::size_t lane, int comboId,
                  int spinId, int textId, bool next) {
    const LRESULT count =
        SendMessageA(GetDlgItem(hwnd, comboId), CB_GETCOUNT, 0, 0);
    const LRESULT cur =
        SendMessageA(GetDlgItem(hwnd, comboId), CB_GETCURSEL, 0, 0);
    if (!next) {
        SendMessageA(GetDlgItem(hwnd, comboId), CB_SETCURSEL,
                     (cur == 0) ? count - 1 : cur - 1, 0);
    } else {
        SendMessageA(GetDlgItem(hwnd, comboId), CB_SETCURSEL,
                     (cur == count - 1) ? 0 : cur + 1, 0);
    }
    const mdl::ModelRecord* model = mdl::Mdl(ActiveModel(app));
    const std::int32_t idx = model->selectedMorphs[lane];
    const float v = model->morphs[idx].value;
    SendMessageA(GetDlgItem(hwnd, spinId), TBM_SETPOS, 1,
                 static_cast<LPARAM>(static_cast<std::int32_t>(v * 100.0)));
    char buf[0x100];
    sprintf_s(buf, 0x100u, "%5.4f", static_cast<double>(v));
    SetWindowTextA(GetDlgItem(hwnd, textId), buf);
}

// Morph-selection step for the four facial-panel combo rows (prev at
// 0x48C735, next at 0x48C941; the remaining rows repeat the same logic).
// It wraps at morphCount, skips morphs outside panel 1 and bails after a full
// circle, matching the original saved-index comparison.
void MorphStep(MMDApp* app, HWND hwnd, std::size_t lane, int comboId,
               int spinId, int textId, bool next) {
    unsigned char* model = ActiveModel(app);
    mdl::ModelRecord* modelRecord = mdl::Mdl(model);
    std::int32_t idx = modelRecord->selectedMorphs[lane];
    if (idx < 0)
        return;
    if (modelRecord->physicsMode != 2 && idx == 0)
        return;
    if (modelRecord->morphs == nullptr)
        return;
    const std::int32_t orig = idx;
    for (;;) {
        if (next) {
            ++idx;
            if (idx >= static_cast<std::int32_t>(
                           mdl::Mdl(ActiveModel(app))->morphCount))
                idx = 0;
        } else {
            --idx;
            if (idx < 0)
                idx = static_cast<std::int32_t>(
                          mdl::Mdl(ActiveModel(app))->morphCount) - 1;
        }
        unsigned char* m = ActiveModel(app);
        mdl::Mdl(m)->selectedMorphs[lane] = idx;
        m = ActiveModel(app);
        idx = mdl::Mdl(m)->selectedMorphs[lane];
        if (idx == orig)
            return;
        if (mdl::Mdl(m)->morphs[idx].panel == mdl::MorphPanel::eyebrow)
            break;
    }
    SyncMorphRow(app, hwnd, lane, comboId, spinId, textId, next);
}

// Bone position edit boxes 537..539 (0x219..0x21B): display mode zeroes the
// app float (0x334/0x338/0x33C) and refreshes; edit mode registers the
// keyframe (sub_42D6E0), zeroes the selected bone's local position component
// and marks its per-frame edit flag.
void BonePosEdit(MMDApp* app, float* vec, std::size_t axis) {
    if (app->state.optflag[0] != 0) {
        vec[axis] = 0.0f;
        RefreshRequest(-1);
        PostViewRefresh(app);
        return;
    }
    const std::int32_t sel = mdl::Mdl(ActiveModel(app))->selectedBone;
    if (sel >= 0) {
        PushBoneEditUndo(app);   // 0x42D6E0 bone-edit keyframe register
        unsigned char* m = ActiveModel(app);
        mikudancestudio::mdl::Bones(m)[sel].trans[axis] = 0.0f;
        m = ActiveModel(app);
        mdl::Mdl(m)->bonePhysicsState[sel] = 1;
    }
    PostViewRefresh(app);
}

// Bone rotation edit boxes 540..542 (0x21C..0x21E): display mode zeroes the
// app float (0x310/0x314/0x318) and refreshes; edit mode registers the
// keyframe (sub_42D6E0), rebuilds the Euler angles with the edited axis
// zeroed (the other two convert degrees -> radians; dbl_52B768 = pi,
// dbl_52E678 = float-pi promoted to double), composes
// Rz(a04C8)*Rx(a04C0)*Ry(a04C4) (D3DXMatrixRotationZ/X/Y + Multiply) and
// stores the rotation quaternion into the bone record (+0x14C) via
// D3DXQuaternionRotationMatrix, marking the bone flag 0x2D98.
void BoneRotEdit(MMDApp* app, float* vec, int axis) {
    if (app->state.optflag[0] != 0) {
        vec[axis] = 0.0f;
        RefreshRequest(-1);
        PostViewRefresh(app);
        return;
    }
    const std::int32_t sel = mdl::Mdl(ActiveModel(app))->selectedBone;
    if (sel < 0) {
        PostViewRefresh(app);
        return;
    }
    PushBoneEditUndo(app);   // 0x42D6E0 bone-edit keyframe register
    // capture the pre-edit Euler values (the original keeps them on the x87
    // stack across the stores)
    const float rX = app->BoneRotationEditDegreesX();   // 0xA04C0
    const float rY = app->BoneRotationEditDegreesY();   // 0xA04C4
    const float rZ = app->BoneRotationEditDegreesZ();   // 0xA04C8
    constexpr double kPi = 3.1415926535897931;    // dbl_52B768
    constexpr double kPiF = 3.1415927410125732;   // dbl_52E678 (float pi)
    switch (axis) {
    case 0:  // 540: edited axis = X
        app->BoneRotationEditDegreesX() = 0.0f;
        app->BoneRotationEditDegreesY() =
            static_cast<float>(-(double)rY * kPi / 180.0);
        app->BoneRotationEditDegreesZ() =
            static_cast<float>(-(double)rZ * kPi / 180.0);
        break;
    case 1:  // 541: edited axis = Y
        app->BoneRotationEditDegreesX() =
            static_cast<float>((double)rX * kPiF / 180.0);
        app->BoneRotationEditDegreesY() = 0.0f;
        app->BoneRotationEditDegreesZ() =
            static_cast<float>(-(double)rZ * kPi / 180.0);
        break;
    default:  // 542: edited axis = Z
        app->BoneRotationEditDegreesX() =
            static_cast<float>((double)rX * kPiF / 180.0);
        app->BoneRotationEditDegreesY() =
            static_cast<float>(-(double)rY * kPi / 180.0);
        app->BoneRotationEditDegreesZ() = 0.0f;
        break;
    }
    auto* d3dx = &d3dx::Get();
    if (d3dx->Load()) {
        d3dx::D3DXMATRIXF m, m2;
        d3dx->rotZ(&m, app->BoneRotationEditDegreesZ());
        d3dx->rotX(&m2, app->BoneRotationEditDegreesX());
        d3dx->multiply(&m, &m, &m2);
        d3dx->rotY(&m2, app->BoneRotationEditDegreesY());
        d3dx->multiply(&m, &m, &m2);
        unsigned char* model = ActiveModel(app);
        d3dx->quatFromMatrix(mdl::Mdl(model)->boneTable[sel].rotQuat, &m);
    }
    unsigned char* model = ActiveModel(app);
    mdl::Mdl(model)->bonePhysicsState[sel] = 1;
    PostViewRefresh(app);
}

// PostModelReload2 + HandleWindowSize + InvalidateRect chain shared by the
// option-flag checkboxes (loc_47EA0D / loc_47EA36 / loc_47E989).
void OptionFlagRefresh(MMDApp* app, HWND hwnd) {
    PostModelReload2(app);   // VA 0x0040D940
    HandleWindowSize(app);   // VA 0x00443300
    InvalidateRect(hwnd, nullptr, FALSE);
}

}  // namespace

void CmdControl500(MMDApp* app, HWND hwnd, std::uint16_t id, std::uint16_t notify) {
    (void)notify;  // the original switch never inspects the notify code
    switch (id) {

    // ---- 500: register-frame button 0x1F4 -------------------------------
    case 500: {
        // (0x48026F) dirty flag, then the frame-register helper
        // sub_4C2080(model, frame, app+0xA0CC4).
        // 原版（x64 0x7FF7CB461045..54）对空槽同样无守卫；它靠键盘轮询
        // （每帧把非按下键写 0/2，sub_140012090 对应原版同类扫描）在 G3
        // 读到之前清掉对话路径遗留的 1。内置 MME 的帧内消息分发会让该
        // 清理窗口偶发失效（幽灵 0x1F4/0x1F5，crashdump/ray_*），故此处
        // 按空模型语义（无操作）防御——与原版可观察行为一致。
        if (ActiveModel(app) == nullptr) {
            break;
        }
        app->SceneModified() = 1;
        RegisterSelectedBoneKeys(
            ActiveModel(app),
            app->state.currentFrame,
            app->PlaybackPhysicsMode());
        // 0x9E16C = max(0x9E16C, model+0x31B0)
        const std::int32_t frames =
            static_cast<std::int32_t>(mdl::Mdl(ActiveModel(app))->maxFrame);
        if (app->state.lastRegisteredFrame < frames)
            app->state.lastRegisteredFrame = frames;
        PanelPaint(app);              // 0x414610
        SelectionReeval(app);         // 0x430510
        break;
    }

    // ---- 501: "select all frames" button 0x1F5 --------------------------
    case 501: {
        // (0x47F93D) check the 0x1EA checkbox, re-dispatch WM_COMMAND
        // 0x1EA, then copy the per-bone selection flags 0x2D98 into the
        // 0x2D94 byte array and reset the selected index 0x2D90 to -1.
        // 同 case 500：空槽防御（原版 0x7FF7CB4610AA 同样裸解引用）。
        if (ActiveModel(app) == nullptr) {
            break;
        }
        SendMessageA(GetDlgItem(hwnd, panel::kBoneSelectRadio), BM_SETCHECK, 1, 0);
        SendMessageA(hwnd, WM_COMMAND, 0x1EA, 0);
        mdl::Mdl(ActiveModel(app))->selectedBone = -1;
        const std::int32_t boneCount = mdl::Mdl(ActiveModel(app))->boneCount;
        for (int i = 0; i < boneCount; ++i) {
            unsigned char* m = ActiveModel(app);
            unsigned char* flags = mdl::Mdl(m)->bonePhysicsState;
            unsigned char* sel = mdl::Mdl(m)->boneSelection;
            sel[i] = (flags[i] != 0) ? 1 : 0;
        }
        PostLanguageSweep(app);       // 0x42F1E0
        PostLanguageSweep2(app);      // 0x40D070
        break;
    }

    // ---- 502/503: option-flag checkbox pair 0x1FC (byte 0x2FC) ----------
    case 502:  // 0x47EA70
        app->state.optflag[4] = 0;
        OptionFlagRefresh(app, hwnd);
        break;
    case 503:  // 0x47EA79
        app->state.optflag[4] = 1;
        OptionFlagRefresh(app, hwnd);
        break;

    // ---- 504..506: no jump-table target.  The 0x1F8 morph-row combo is
    //      served by the dispatcher's default CBN_SELCHANGE chain
    //      (command_dispatch.cpp DefaultSelChangeChain, x86 0x48EA48) and
    //      never reaches this switch; the 0x1F9 slider / 0x1FA edit never
    //      emit WM_COMMAND.  Falls through to the no-op default. ---------

    // ---- 507/508: bone X combo prev/next (axis 0x2D9C, row 0x1F8) ------
    case 507:  // 0x48C735
        MorphStep(app, hwnd, 0, 0x1F8, 0x1F9, 0x1FA, false);
        break;
    case 508:  // 0x48C941
        MorphStep(app, hwnd, 0, 0x1F8, 0x1F9, 0x1FA, true);
        break;

    // ---- 509..511: no jump-table target; 0x1FD rides the dispatcher
    //      default chain (x86 0x48EC56) - falls through to the no-op
    //      default. --------------------------------------------------------

    // ---- 512/513: bone Y combo prev/next (axis 0x2DA0, row 0x1FD) ------
    case 512:  // 0x48CB4E
        MorphStep(app, hwnd, 1, 0x1FD, 0x1FE, 0x1FF, false);
        break;
    case 513:  // 0x48CD50
        MorphStep(app, hwnd, 1, 0x1FD, 0x1FE, 0x1FF, true);
        break;

    // ---- 514..516: no jump-table target; 0x202 rides the dispatcher
    //      default chain (x86 0x48EE66) - falls through to the no-op
    //      default. --------------------------------------------------------

    // ---- 517/518: bone Z combo prev/next (axis 0x2DA4, row 0x202) ------
    case 517:  // 0x48CF51
        MorphStep(app, hwnd, 2, 0x202, 0x203, 0x204, false);
        break;
    case 518:  // 0x48D151
        MorphStep(app, hwnd, 2, 0x202, 0x203, 0x204, true);
        break;

    // ---- 519..521: no jump-table target; 0x207 rides the dispatcher
    //      default chain (x86 0x48F066) - falls through to the no-op
    //      default. --------------------------------------------------------

    // ---- 522/523: bone rot combo prev/next (axis 0x2DA8, row 0x207) ----
    case 522:  // 0x48D34C
        MorphStep(app, hwnd, 3, 0x207, 0x208, 0x209, false);
        break;
    case 523:  // 0x48D550
        MorphStep(app, hwnd, 3, 0x207, 0x208, 0x209, true);
        break;

    // ---- 524..527: frame-register buttons 0x20C..0x20F (per-axis) -------
    // Each clears the active model's bone (0x26E0, flag +0x38, 0x3C-step
    // over kBoneKeyCapacity x 0x3C bytes), morph (0x26E4, flag +0x10, 0x14-step over 0x61A80)
    // and IK (0x26E8, flag +0x14, 0x1C-step over 0x6D60) selection flags,
    // then registers a keyframe via sub_49EEE0(model, axisIdx, frame 0x980),
    // updates 0x9E16C against model+0x31B0 and repaints the panel.
    case 524:  // 0x4803B1 (axis 0x2DA0)
    case 525:  // 0x4802CC (axis 0x2D9C)
    case 526:  // 0x480571 (axis 0x2DA8)
    case 527:  // 0x480491 (axis 0x2DA4)
    {
        // lane 0..3 = the per-axis combo rows 0x2D9C/0x2DA0/0x2DA4/0x2DA8;
        // 526 (0x2DA8) is the default chain.
        std::size_t lane = 3;
        switch (id) {
        case 525: lane = 0; break;
        case 524: lane = 1; break;
        case 527: lane = 2; break;
        default: break;
        }
        app->SceneModified() = 1;
        {
            unsigned char* m = ActiveModel(app);
            mdl::BoneKey* boneKeys = mdl::BoneKeys(m);
            for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i)
                boneKeys[i].allocated = 0;
        }
        {
            unsigned char* m = ActiveModel(app);
            mdl::MorphKey* morphKeys = mdl::MorphKeys(m);
            for (std::size_t i = 0; i < mdl::kMorphKeyCapacity; ++i)
                morphKeys[i].allocated = 0;
        }
        {
            unsigned char* m = ActiveModel(app);
            mdl::DisplayKey* displayKeys = mdl::DisplayKeys(m);
            for (std::size_t i = 0; i < mdl::kDisplayKeyCapacity; ++i)
                displayKeys[i].allocated = 0;
        }
        unsigned char* m = ActiveModel(app);
        RegisterMorphKeyCurrent(
            m,
            mdl::Mdl(m)->selectedMorphs[lane],
            app->state.currentFrame);
        const std::int32_t frames =
            static_cast<std::int32_t>(mdl::Mdl(ActiveModel(app))->maxFrame);
        if (app->state.lastRegisteredFrame < frames)
            app->state.lastRegisteredFrame = frames;
        PanelPaint(app);
        break;
    }

    // ---- 528/529: option-flag checkbox pair 0x210 (byte 0x2FD) ----------
    case 528:  // 0x47EA82
        app->state.optflag[5] = 0;
        OptionFlagRefresh(app, hwnd);
        break;
    case 529:  // 0x47EA9A
        app->state.optflag[5] = 1;
        OptionFlagRefresh(app, hwnd);
        break;

    // ---- 530: no jump-table target --------------------------------------
    case 530:
        break;

    // ---- 531: physics checkbox 0x213 -------------------------------------
    case 531: {  // 0x47FA92
        // Checked: mode byte 0x340 = 2, uncheck 0x19C, then
        // ApplyCameraReferenceModeChange(app, old mode) unless display mode
        // (0x2F8).
        if (SendMessageA(GetDlgItem(hwnd, panel::kCameraRefBoneCheckbox), BM_GETCHECK, 0, 0) == 1) {
            const int oldMode = static_cast<int>(app->CameraReferenceMode());
            app->CameraReferenceMode() =
                CameraAttachmentReference::SelectedBone;
            SendMessageA(GetDlgItem(hwnd, panel::kCameraRefModelCheckbox), BM_SETCHECK, 0, 0);
            if (app->state.optflag[0] == 0)
                ApplyCameraReferenceModeChange(app, oldMode);
        } else {
            // Unchecked: mode byte 0x340 = 0,
            // ApplyCameraReferenceModeChange(app, old mode).
            const int oldMode = static_cast<int>(app->CameraReferenceMode());
            app->CameraReferenceMode() = CameraAttachmentReference::None;
            if (app->state.optflag[0] == 0)
                ApplyCameraReferenceModeChange(app, oldMode);
        }
        break;
    }

    // ---- 532/533: button pair 0x214/0x215 --------------------------------
    case 532:  // 0x48BC87
        JumpNextKeyframe(app);   // 0x441070 (not 0x411070)
        break;
    case 533:  // 0x48BC91
        JumpPrevKeyframe(app);   // 0x4414C0
        break;

    // ---- 534: no jump-table target --------------------------------------
    case 534:
        break;

    // ---- 535: display-mode checkbox 0x217 (byte 0x9ED98) -----------------
    case 535: {  // 0x48BE6A
        if (SendMessageA(GetDlgItem(hwnd, panel::kFollowCameraCheckbox), BM_GETCHECK, 0, 0) == 1) {
            // checked: camera cluster 0x308/0x30C = 0, menu 0xF7 checked,
            // full model/slot refresh chain (0x42E640, 0x411070, 0x411B90,
            // 0x412330, per-slot 0x413120, 0x4134E0), then the edit-mode
            // gated PostModelReload (0x41A650) and PostLanguageSweep2.
            app->state.followCameraEnabled = 1;
            app->ViewOffsetX() = 0.0f;
            app->ViewOffsetY() = 0.0f;
            CheckMenuItem(GetMenu(hwnd), 0xF7, MF_CHECKED);
            ReloadModels(app);   // 0x42E640 model-list reset
            RefreshLightPanel(app);
            RefreshSelfShadowPanel(app);
            ApplyGravityTrack(app);
            for (int i = 0; i < 0xFF; ++i) {
                if (app->ObjectSlot(i) != nullptr)
                    ApplyAccessoryTrack(app, i);
            }
            SyncAccessoryEditPanel(app);
            if (app->state.optflag[0] == 0) {
                app->CameraAttachmentTransformSuppressed() = 0;
                PostModelReload(app);  // 0x41A650 post-reload refresh
            }
            PostLanguageSweep2(app);
        } else {
            // unchecked: byte 0x9ED98 = 0, menu 0xF7 unchecked, reload
            // gated on the selected slot (0xA0430 == slot index).
            app->state.followCameraEnabled = 0;
            CheckMenuItem(GetMenu(hwnd), 0xF7, MF_UNCHECKED);
            if (app->state.optflag[0] == 0) {
                const std::int32_t sel = app->CameraParentModel();
                if (sel == app->SelectedModelSlot() &&
                    sel >= 0) {
                    app->CameraAttachmentTransformSuppressed() = 0;
                    PostModelReload(app);
                }
            }
            PostLanguageSweep2(app);
        }
        break;
    }

    // ---- 536: accessory-combo sync 0x218 (combo 0x1B4) ------------------
    case 536: {  // 0x48BF6F
        if (app->state.optflag[0] != 0) {
            // display mode: selected accessory index 0xA042C; when zero the
            // original re-dispatches WM_COMMAND 0x1B3 on the main window.
            const std::int32_t sel =
                app->state.mainModelComboSelection;
            if (sel == 0) {
                SendMessageA(hwnd, WM_COMMAND, 0x1B3, 0);
                break;
            }
            SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_SETCURSEL, sel, 0);
            ApplyModelComboSelection(app);
        } else {
            SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_SETCURSEL, 0, 0);
            ApplyModelComboSelection(app);
        }
        break;
    }

    // ---- 537..539: bone position edit boxes 0x219..0x21B ----------------
    case 537:  // 0x48BFF7 (pos X: app 0x334, record +0x140)
        BonePosEdit(app, app->CameraPosition(), 0);
        break;
    case 538:  // 0x48C081 (pos Y: app 0x338, record +0x144)
        BonePosEdit(app, app->CameraPosition(), 1);
        break;
    case 539:  // 0x48C10B (pos Z: app 0x33C, record +0x148)
        BonePosEdit(app, app->CameraPosition(), 2);
        break;

    // ---- 540..542: bone rotation edit boxes 0x21C..0x21E ----------------
    case 540:  // 0x48C195 (rot X: app 0x310, edited axis X)
        BoneRotEdit(app, app->CameraRotation(), 0);
        break;
    case 541:  // 0x48C2E4 (rot Y: app 0x314, edited axis Y)
        BoneRotEdit(app, app->CameraRotation(), 1);
        break;
    case 542:  // 0x48C435 (rot Z: app 0x318, edited axis Z)
        BoneRotEdit(app, app->CameraRotation(), 2);
        break;

    // ---- 543: camera-angle edit box 0x21F -------------------------------
    case 543: {  // 0x48C52F
        if (app->state.optflag[0] != 0) {
            app->CameraDistance() = 0.0f;
            RefreshRequest(-1);
        }
        PostViewRefresh(app);
        break;
    }

    // ---- 544..550: no jump-table target ---------------------------------
    case 544:
    case 545:
    case 546:
    case 547:
    case 548:
    case 549:
    case 550:
        break;

    // ---- 553: frame edit box 0x22A (EN_CHANGE read-back) ----------------
    case 553: {  // 0x48DECB
        // atol of the 0x22A text (max 10 chars), clamped to >= 0, applied
        // as the frame 0x980; frame-apply chain + echo back into 0x1A1.
        char buf[0x100];
        GetWindowTextA(GetDlgItem(hwnd, panel::kGotoFrameEdit), buf, 0xA);
        long v = atol(buf);
        if (v < 0)
            v = 0;
        app->state.currentFrame = static_cast<std::int32_t>(v);
        RefreshAfterFrameApply(app);   // 0x432FA0 frame-apply chain
        PostViewRefresh(app);
        sprintf_s(buf, 0x100u, "%d", app->state.currentFrame);
        SetWindowTextA(GetDlgItem(hwnd, panel::kCurrentFrameEdit), buf);
        break;
    }

    // ---- 555: frame edit box 0x22B echo ---------------------------------
    case 555: {  // 0x48DE84
        char buf[0x100];
        sprintf_s(buf, 0x100u, "%d", app->state.currentFrame);
        SetWindowTextA(GetDlgItem(hwnd, panel::kGotoFrameEdit), buf);
        break;
    }

    // ---- 556: checkbox 0x22C (quality / BM_GETCHECK probe control) ------
    case 556: {  // 0x48DF5D
        // The checked state of 0x22C is probed by HandleNotify (ui_notify.cpp
        // 0x4398B0) and here drives the locale subsystem (0xA06C4) buffer
        // sizes renderTargetWidth/renderTargetHeight (0x1D558/0x1D55C,
        // 0x1000 checked / 0x800 unchecked) plus the release of the three
        // objects hdrTexture/shadowDepthSurface/shadowSurface (0x1D548 /
        // 0x1D554 / 0x1D550, vtable slot +8 = IUnknown::Release).
        D3DRenderer* locale = app->Renderer();
        const std::uint32_t size =
            (SendMessageA(GetDlgItem(hwnd, panel::kSelfShadowCheckbox), BM_GETCHECK, 0, 0) == 1)
                ? 0x1000
                : 0x800;
        locale->renderTargetWidth = size;   // 0x1D558
        locale->renderTargetHeight = size;  // 0x1D55C
        IUnknown* objects[] = {locale->hdrTexture,        // 0x1D548
                               locale->shadowDepthSurface,  // 0x1D554
                               locale->shadowSurface};      // 0x1D550
        for (IUnknown* obj : objects) {
            if (obj != nullptr) {
                obj->Release();  // vtable slot +8
            }
        }
        locale->hdrTexture = nullptr;
        locale->shadowDepthSurface = nullptr;
        locale->shadowSurface = nullptr;
        break;
    }

    // ---- 558: frame-reset button 0x22E ----------------------------------
    case 558: {  // 0x48C679
        app->state.currentFrame = 0;
        RefreshAfterFrameApply(app);   // 0x432FA0 frame-apply chain
        PostViewRefresh(app);
        char buf[0x100];
        sprintf_s(buf, 0x100u, "%d", app->state.currentFrame);
        SetWindowTextA(GetDlgItem(hwnd, panel::kCurrentFrameEdit), buf);
        break;
    }

    // ---- 559: frame-end button 0x22F ------------------------------------
    case 559: {  // 0x48C6D6
        app->state.currentFrame =
            app->state.lastRegisteredFrame;
        RefreshAfterFrameApply(app);   // 0x432FA0 frame-apply chain
        PostViewRefresh(app);
        char buf[0x100];
        sprintf_s(buf, 0x100u, "%d", app->state.currentFrame);
        SetWindowTextA(GetDlgItem(hwnd, panel::kCurrentFrameEdit), buf);
        break;
    }

    // ---- 562..564: coordinate-system radio group 0x232..0x234 -----------
    case 562: {  // 0x48D84F (0xA0D30 = 0)
        SendMessageA(GetDlgItem(hwnd, panel::kEditOffCheckbox), BM_SETCHECK, 1, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kEditMode1Checkbox), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kEditMode2Checkbox), BM_SETCHECK, 0, 0);
        app->SelfShadowMode() = 0;
        RefreshRequest(-3);
        break;
    }
    case 563: {  // 0x48D8C1 (0xA0D30 = 1)
        SendMessageA(GetDlgItem(hwnd, panel::kEditOffCheckbox), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kEditMode1Checkbox), BM_SETCHECK, 1, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kEditMode2Checkbox), BM_SETCHECK, 0, 0);
        app->SelfShadowMode() = 1;
        RefreshRequest(-3);
        break;
    }
    case 564: {  // 0x48D933 (0xA0D30 = 2)
        SendMessageA(GetDlgItem(hwnd, panel::kEditOffCheckbox), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kEditMode1Checkbox), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kEditMode2Checkbox), BM_SETCHECK, 1, 0);
        app->SelfShadowMode() = 2;
        RefreshRequest(-3);
        break;
    }

    // ---- 565: physics-interval edit box 0x235 ---------------------------
    case 565: {  // 0x48D9A5
        // atof of the 0x231 text (max 8 chars); physics interval float
        // 0xA0D2C = (10000 - v) / 100000 (dbl_52B9F8 / dbl_52BA00); slider
        // 0x230 pos = (int)v; dirty flag; selection-bitmap clear; then the
        // frame-scroll apply sub_411DF0(app, frame), RefreshRequest(-3) and
        // PanelPaint.
        char buf[0x100];
        GetWindowTextA(GetDlgItem(hwnd, panel::kSelfShadowRangeEdit), buf, 8);
        const float v = static_cast<float>(atof(buf));
        app->state.physicsInterval =
            static_cast<float>((10000.0 - (double)v) / 100000.0);
        SendMessageA(GetDlgItem(hwnd, panel::kSelfShadowRangeSlider), TBM_SETPOS, 1,
                     static_cast<LPARAM>(static_cast<std::int32_t>(v)));
        app->SceneModified() = 1;
        // rigid 0x374 (+0x48, 0x54-step), joint 0x378 (+0x24, 0x28-step),
        // IK 0x37C (+0x14, 0x18-step), morph 0x380 (+0x21, 0x24-step),
        // 0x2710 records each
        for (std::size_t key = 0; key < 10000; ++key) {
            app->CameraKeys()[key].selected = 0;
            app->LightKeys()[key].selected = 0;
            app->ShadowKeys()[key].selected = 0;
            app->GravityKeys()[key].selected = 0;
        }
        // 255 accessory slots at app+0x384: flag +0x18, 0x3C-step over
        // 0x927C0 records
        for (int s = 0; s < 0xFF; ++s) {
            auto* rec = reinterpret_cast<unsigned char*>(app->AccessoryKeys(s));
            for (std::size_t off = 0; off < 0x927C0; off += 0x3C)
                rec[off + 0x18] = 0;
        }
        RegisterSelfShadowState(app, app->state.currentFrame);
        RefreshRequest(-3);
        PanelPaint(app);
        break;
    }

    // ---- 551: play checkbox 0x227 (byte 0x31E) --------------------------
    case 551: {  // 0x48C5AE
        if (app->state.fpsOverlayEnabled != 0) {
            app->state.fpsOverlayEnabled = 0;
            CheckMenuItem(GetMenu(hwnd), 0xD3, MF_UNCHECKED);
        } else {
            app->state.fpsOverlayEnabled = 1;
            app->state.fpsOverlayElapsedSeconds = 0.0f;
            app->state.fpsOverlayFrameCount = 0;
            CheckMenuItem(GetMenu(hwnd), 0xD3, MF_CHECKED);
        }
        break;
    }

    // ---- 552: checkbox 0x228 (menu 0x12B mirror) ------------------------
    case 552: {  // 0x48C612
        if (GetMenuState(GetMenu(hwnd), 0x12B, 0) & MF_CHECKED) {
            CheckMenuItem(GetMenu(hwnd), 0x12B, MF_UNCHECKED);
            app->FrameVolumeControlEnabled() = 0;
        } else {
            CheckMenuItem(GetMenu(hwnd), 0x12B, MF_CHECKED);
            app->FrameVolumeControlEnabled() = 1;
        }
        break;
    }

    // ---- 554: no jump-table target --------------------------------------
    case 554:
        break;

    // ---- 557: morph-display checkbox 0x22D (byte 0x31D) -----------------
    case 557: {  // 0x48C557
        if (app->state.groundGridEnabled != 0) {
            app->state.groundGridEnabled = 0;
            CheckMenuItem(GetMenu(hwnd), 0xD7, MF_UNCHECKED);
        } else {
            app->state.groundGridEnabled = 1;
            CheckMenuItem(GetMenu(hwnd), 0xD7, MF_CHECKED);
        }
        break;
    }

    // ---- 560..561: no jump-table target (WM_HSCROLL pair) ---------------
    case 560:
    case 561:
        break;

    // ---- 566/567: option-flag checkbox pair 0x236 (byte 0x2FE) ----------
    case 566:  // 0x47EAA6
        app->state.optflag[6] = 0;
        OptionFlagRefresh(app, hwnd);
        break;
    case 567:  // 0x47EAAF
        app->state.optflag[6] = 1;
        OptionFlagRefresh(app, hwnd);
        break;

    default:
        break;
    }
}

}  // namespace mikudancestudio
