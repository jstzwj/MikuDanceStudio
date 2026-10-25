// ===========================================================================
// VA 0x00463640 - EditCommit  (original: sub_463640, 0xB2A bytes)
// VA 0x0044BEF0 - EditCommitTail  (original: sub_44BEF0)
// VA 0x00464170 - EditSubclassProc  (original: sub_464170)
// VA 0x0040EC90 - TrackbarSubclassProc  (original: sub_40EC90)
// ===========================================================================
// sub_464170 is the wndproc installed on the 29 numeric EDIT controls of
// sub_466D20 (0x467BE4..0x46A8E7): on VK_RETURN it commits the edit
// (sub_463640) and returns focus to the main window; on WM_KILLFOCUS it
// commits with the newly focused window as the argument.  Everything else
// forwards to the captured EDIT class proc at app+0xA08D0 (stored once, at
// the first install site 0x467BD7 - all 29 controls share the class proc).
//
// sub_40EC90 is the wndproc installed on the 13 msctls_trackbar32 controls:
// WM_LBUTTONUP merely refocuses the main window (keyboard shortcuts keep
// working after a slider drag), everything forwards to the trackbar class
// proc captured once at 0x4686E2 into app+0xA0CDC.
//
// sub_463640 dispatches per control id (argument = the edit's HWND):
//   417  frame number  - atol -> app+0x980; >0x80000000 resets to 0/"0";
//        0x432FA0 + PostViewRefresh tail
//   461-466  center/rotation mirrors - parsed value scaled into the work
//        record (647588..96 / 647540..48) and echoed into the paired
//        trackbar (455..460) via TBM_SETPOS; SelectTimelineTrack(GlobalTimelineTrack::Light) tail
//   448  fov degrees -> app+647656, slider 447 echo, projection rebuild
//        (PerspectiveFovLH fov*0.01745329238474369, aspect from the
//        wrapper at +0x1D4EC, zn 1.0, zf 100000.0) + SetTransform slot 3;
//        SelectTimelineTrack(GlobalTimelineTrack::Camera) tail
//   478-485  accessory fields (gated on accessory != null; index
//        byte app+647536 into the 0x9DD70 array): rgb 532/536/540 direct,
//        direction 544/548/552 as deg*PI/180 (PI = 3.141592025756836 -
//        the float-truncated literal), 556 direct, 485 clamps to [0,1]
//        into +1184 and rewrites the edit text "%3.2f"; tail
//        SelectAccessoryTimelineTrack(acc index)
//   561  -> app+658732 = (10000-v)/100000, slider 560 echo, -3 tail
//   506/511/516/521  morph values into the 136-stride morph records of the
//        active model (slot app+0x910 into app+0x780; index at model+
//        11676/80/84/88, records at model+9924, value at +48), sliders
//        505/510/515/520 echo x100
// Every path ends in sub_44BEF0, which additionally owns the camera/bone
// position-rotation edits 544..550 (bone mode writes the 604-stride bone
// record +320..328/+332 via the Z*X*Y euler composition and the dirty mark
// at model+11672; camera mode writes app+820..828/784..792/657628) and
// finishes with SelectTimelineTrack(GlobalTimelineTrack::Camera)+PostViewRefresh when it handled one.
//
// Reference: ../translated/MikuMikuDance/fcn_00463640.cpp (rough), live
// IDA decompilation + disassembly of 0x463640/0x44BEF0/0x464170/0x40EC90
// (the asm is authoritative for the subclass-proc slots: Hex-Rays prints
// decoy indices for both Block+... reads).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>
#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// Frame-apply refresh and pose-edit snapshot entry points.
void RefreshAfterFrameApply(MMDApp* app); // VA 0x00432FA0
void PushBoneEditUndo(MMDApp* app);       // VA 0x0042D6E0

// Deg->rad literal of 0x463640/0x44BEF0: dbl_52BB20 bits 0x3F91DF46A0000000
// (also used by the projection rebuild at 0x4639D5).
static constexpr double kDegToRad = 0.01745329238474369;
// PI literal of the euler conversions (float-truncated 24-bit mantissa).
static constexpr double kPiLit = 3.141592025756836;

namespace {

// Active model = slot array app+0x780 indexed by byte app+0x910.
mdl::ModelRecord* SlotModel(MMDApp* app) {
    return app->SelectedModel();
}

// Selected accessory = array app+0x9DD70 indexed by byte app+0x9E170.
mdl::AccessoryRecord* SelectedAccessory(MMDApp* app) {
    return app->AccessorySlot(app->SelectedAccessorySlot());
}

// Z*X*Y euler composition of the 547/548/549 bone-rotation commits:
// D3DXMatrixRotationZ x rotX x rotY, result quaternion into the bone record
// (+332 inside the 604-stride bone array).
void ComposeEulerToBone(MMDApp* app, mdl::ModelRecord* model, int sel) {
    d3dx::D3DXMATRIXF rot{};
    d3dx::D3DXMATRIXF tmp{};
    auto* d3dx = &d3dx::Get();
    d3dx->rotZ(&rot, app->state.eulerZ);
    d3dx->rotX(&tmp, app->state.eulerX);
    d3dx->multiply(&rot, &rot, &tmp);
    d3dx->rotY(&tmp, app->state.eulerY);
    d3dx->multiply(&rot, &rot, &tmp);
    mikudancestudio::mdl::BoneRecord* bones = model->boneTable;
    d3dx->quatFromMatrix(bones[sel].rotQuat, &rot);
    model->bonePhysicsState[sel] = 1;
}

} // namespace

// ---------------------------------------------------------------------------
// VA 0x0044BEF0 - per-edit tail; owns ids 544..550 (see file header).
// ---------------------------------------------------------------------------
void CommitEditControlTail(MMDApp* app, HWND edit) {
    HWND base = app->FloatingWindow();
    if (base == nullptr)
        base = app->MainWindow();
    const bool cameraMode = app->CameraMode() != 0;

    if (edit == GetDlgItem(base, panel::kReadoutPosXEdit)) { // 0x44BF31
        char text[256];
        GetWindowTextA(edit, text, 100);
        const double v = atof(text);
        if (!cameraMode) {
            mdl::ModelRecord* model = SlotModel(app);
            const int sel = model->selectedBone;
            if (sel >= 0) {
                PushBoneEditUndo(app);
                model->boneTable[sel].trans[0] = static_cast<float>(v);
                model->bonePhysicsState[sel] = 1;
            }
            PostViewRefresh(app);
            return;
        }
        app->CameraPositionX() = static_cast<float>(v);
        SelectTimelineTrack(GlobalTimelineTrack::Camera);
        PostViewRefresh(app);
        return;
    }
    if (edit == GetDlgItem(base, panel::kReadoutPosYEdit)) { // 0x44BFE5
        char text[256];
        GetWindowTextA(edit, text, 100);
        const double v = atof(text);
        if (!cameraMode) {
            mdl::ModelRecord* model = SlotModel(app);
            const int sel = model->selectedBone;
            if (sel >= 0) {
                PushBoneEditUndo(app);
                model->boneTable[sel].trans[1] = static_cast<float>(v);
                model->bonePhysicsState[sel] = 1;
            }
            PostViewRefresh(app);
            return;
        }
        app->CameraPositionY() = static_cast<float>(v);
        SelectTimelineTrack(GlobalTimelineTrack::Camera);
        PostViewRefresh(app);
        return;
    }
    if (edit == GetDlgItem(base, panel::kReadoutPosZEdit)) { // 0x44C099
        char text[256];
        GetWindowTextA(edit, text, 100);
        double v = atof(text);
        if (!cameraMode) {
            mdl::ModelRecord* model = SlotModel(app);
            const int sel = model->selectedBone;
            if (sel >= 0) {
                PushBoneEditUndo(app);
                model->boneTable[sel].trans[2] = static_cast<float>(v);
                model->bonePhysicsState[sel] = 1;
            }
            PostViewRefresh(app);
            return;
        }
        if (app->CameraParentModel() >= 0) // attached-camera mirror
            v = -v;
        app->CameraPositionZ() = static_cast<float>(v);
        SelectTimelineTrack(GlobalTimelineTrack::Camera);
        PostViewRefresh(app);
        return;
    }
    if (edit == GetDlgItem(base, panel::kReadoutRotXEdit)) { // 0x44C158
        char text[256];
        GetWindowTextA(edit, text, 100);
        const double v = atof(text);
        if (!cameraMode) {
            mdl::ModelRecord* model = SlotModel(app);
            const int sel = model->selectedBone;
            if (sel >= 0) {
                PushBoneEditUndo(app);
                app->state.eulerX = static_cast<float>(v * kPiLit / 180.0);
                app->state.eulerY =
                    static_cast<float>(-static_cast<double>(app->state.eulerY) * kPiLit / 180.0);
                app->state.eulerZ =
                    static_cast<float>(kPiLit * -static_cast<double>(app->state.eulerZ) / 180.0);
                ComposeEulerToBone(app, model, sel);
            }
            PostViewRefresh(app);
            return;
        }
        app->CameraPitch() = static_cast<float>(-v * kPiLit / 180.0);
        SelectTimelineTrack(GlobalTimelineTrack::Camera);
        PostViewRefresh(app);
        return;
    }
    if (edit == GetDlgItem(base, panel::kReadoutRotYEdit)) { // 0x44C2C7
        char text[256];
        GetWindowTextA(edit, text, 100);
        const double v = atof(text);
        if (cameraMode) {
            app->CameraYaw() = static_cast<float>(v * kPiLit / 180.0);
            SelectTimelineTrack(GlobalTimelineTrack::Camera);
            PostViewRefresh(app);
            return;
        }
        mdl::ModelRecord* model = SlotModel(app);
        const int sel = model->selectedBone;
        if (sel >= 0) {
            PushBoneEditUndo(app);
            app->state.eulerX =     // re-convert stored
                static_cast<float>( // degrees
                    static_cast<double>(app->state.eulerX) * kPiLit / 180.0);
            app->state.eulerY = static_cast<float>(-v * kPiLit / 180.0);
            app->state.eulerZ =
                static_cast<float>(kPiLit * -static_cast<double>(app->state.eulerZ) / 180.0);
            ComposeEulerToBone(app, model, sel);
        }
        PostViewRefresh(app);
        return;
    }
    if (edit == GetDlgItem(base, panel::kReadoutRotZEdit)) { // 0x44C434
        char text[256];
        GetWindowTextA(edit, text, 100);
        const double v = atof(text);
        if (cameraMode) {
            app->CameraRoll() = static_cast<float>(v * kPiLit / 180.0);
            SelectTimelineTrack(GlobalTimelineTrack::Camera);
            PostViewRefresh(app);
            return;
        }
        mdl::ModelRecord* model = SlotModel(app);
        const int sel = model->selectedBone;
        if (sel >= 0) {
            PushBoneEditUndo(app);
            app->state.eulerX =
                static_cast<float>(static_cast<double>(app->state.eulerX) * kPiLit / 180.0);
            app->state.eulerY =
                static_cast<float>(-static_cast<double>(app->state.eulerY) * kPiLit / 180.0);
            app->state.eulerZ = static_cast<float>(kPiLit * -v / 180.0);
            ComposeEulerToBone(app, model, sel);
        }
        PostViewRefresh(app);
        return;
    }
    if (edit == GetDlgItem(base, panel::kReadoutDistEdit) && cameraMode) { // 0x44C561
        char text[256];
        GetWindowTextA(edit, text, 100);
        const double v = atof(text);
        app->CameraDistance() = static_cast<float>(-v);
        SelectTimelineTrack(GlobalTimelineTrack::Camera);
        PostViewRefresh(app);
        return;
    }
    PostViewRefresh(app); // 0x44C5B1 fallthrough
}

// ---------------------------------------------------------------------------
// VA 0x00463640 - edit commit (see file header for the per-id behaviour).
// ---------------------------------------------------------------------------
void CommitEditControl(MMDApp* app, HWND edit) {
    auto& s = *app;
    const HWND main = s.MainWindow();
    s.state.enterKeyState = 1; // 0x463672 dword store

    if (edit == GetDlgItem(main, panel::kCurrentFrameEdit)) { // frame number
        char text[256];
        GetWindowTextA(edit, text, 8);
        const long v = atol(text);
        s.CurrentFrame() = static_cast<std::int32_t>(v);
        if (static_cast<unsigned long>(v) > 0x80000000UL) {
            s.CurrentFrame() = 0;
            SetWindowTextA(edit, "0");
        }
        RefreshAfterFrameApply(app);
        PostViewRefresh(app);
        CommitEditControlTail(app, edit);
        return;
    }
    if (edit == GetDlgItem(main, panel::kLightColorEditR) || // light RGB
        edit == GetDlgItem(main, panel::kLightColorEditG) ||
        edit == GetDlgItem(main, panel::kLightColorEditB)) {
        char text[256];
        GetWindowTextA(edit, text, 8);
        const double v = static_cast<double>(atol(text)) * 0.00390625;
        const int id = edit == GetDlgItem(main, panel::kLightColorEditR) ? panel::kLightColorEditR
                       : edit == GetDlgItem(main, panel::kLightColorEditG)
                           ? panel::kLightColorEditG
                           : panel::kLightColorEditB;
        s.LightColor()[id - panel::kLightColorEditR] = static_cast<float>(v);
        SendMessageA(GetDlgItem(main, panel::kLightColorSliderR + (id - panel::kLightColorEditR)),
                     TBM_SETPOS, 1, static_cast<LPARAM>(static_cast<int>(v * 256.0)));
        SelectTimelineTrack(GlobalTimelineTrack::Light);
        CommitEditControlTail(app, edit);
        return;
    }
    if (edit == GetDlgItem(main, panel::kLightDirEditX) || // light direction XYZ
        edit == GetDlgItem(main, panel::kLightDirEditY) ||
        edit == GetDlgItem(main, panel::kLightDirEditZ)) {
        char text[256];
        GetWindowTextA(edit, text, 8);
        const double v = atof(text);
        const int id = edit == GetDlgItem(main, panel::kLightDirEditX)   ? panel::kLightDirEditX
                       : edit == GetDlgItem(main, panel::kLightDirEditY) ? panel::kLightDirEditY
                                                                         : panel::kLightDirEditZ;
        s.LightDirection()[id - panel::kLightDirEditX] = static_cast<float>(v);
        SendMessageA(GetDlgItem(main, panel::kLightDirSliderX + (id - panel::kLightDirEditX)),
                     TBM_SETPOS, 1, static_cast<LPARAM>(static_cast<int>(v * 100.0)));
        SelectTimelineTrack(GlobalTimelineTrack::Light);
        CommitEditControlTail(app, edit);
        return;
    }
    if (edit == GetDlgItem(main, panel::kFovEdit)) { // fov + projection
        char text[256];
        GetWindowTextA(edit, text, 8);
        const float v = static_cast<float>(atol(text));
        s.CameraFov() = v;
        SendMessageA(GetDlgItem(main, panel::kFovSlider), TBM_SETPOS, 1,
                     static_cast<LPARAM>(static_cast<int>(v)));
        D3DRenderer* r = s.Renderer();
        const float fovRad = static_cast<float>(static_cast<double>(s.CameraFov()) * kDegToRad);
        d3dx::D3DXMATRIXF mat{};
        auto* d3dx = &d3dx::Get();
        d3dx->perspectiveFovLH(&mat, fovRad, r->aspectRatio, 1.0f, 100000.0f);
        IDirect3DDevice9* dev = r->device;
        dev->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(&mat));
        SelectTimelineTrack(GlobalTimelineTrack::Camera);
        CommitEditControlTail(app, edit);
        return;
    }
    // 478..485: accessory fields (id = 0x1DE..0x1E5).
    {
        mdl::AccessoryRecord* acc = SelectedAccessory(app);
        int accessoryControlId = 0;
        if (edit == GetDlgItem(main, panel::kAccPosXEdit))
            accessoryControlId = panel::kAccPosXEdit;
        else if (edit == GetDlgItem(main, panel::kAccPosYEdit))
            accessoryControlId = panel::kAccPosYEdit;
        else if (edit == GetDlgItem(main, panel::kAccPosZEdit))
            accessoryControlId = panel::kAccPosZEdit;
        else if (edit == GetDlgItem(main, panel::kAccRotXEdit))
            accessoryControlId = panel::kAccRotXEdit;
        else if (edit == GetDlgItem(main, panel::kAccRotYEdit))
            accessoryControlId = panel::kAccRotYEdit;
        else if (edit == GetDlgItem(main, panel::kAccRotZEdit))
            accessoryControlId = panel::kAccRotZEdit;
        else if (edit == GetDlgItem(main, panel::kAccScaleEdit))
            accessoryControlId = panel::kAccScaleEdit;
        else if (edit == GetDlgItem(main, panel::kAccOpacityEdit))
            accessoryControlId = panel::kAccOpacityEdit;
        if (accessoryControlId != 0 && acc != nullptr) {
            char text[256];
            GetWindowTextA(edit, text, 8);
            const double v = atof(text);
            if (accessoryControlId <= panel::kAccPosZEdit) {
                acc->position[accessoryControlId - panel::kAccPosXEdit] = static_cast<float>(v);
            } else if (accessoryControlId <= panel::kAccRotZEdit) {
                acc->rotation[accessoryControlId - panel::kAccRotXEdit] =
                    static_cast<float>(v / 180.0 * kPiLit);
            } else if (accessoryControlId == panel::kAccScaleEdit) {
                acc->scale = static_cast<float>(v);
            } else { // 485: clamp + rewrite
                // v7 = 0; if (v < 0 || (v7 = 1, v > 1)) v = v7
                double out = v;
                if (v < 0.0)
                    out = 0.0;
                else if (v > 1.0)
                    out = 1.0;
                acc->opacity = static_cast<float>(out);
                const HWND same = GetDlgItem(main, panel::kAccOpacityEdit);
                const LPARAM len = GetWindowTextLengthA(same);
                SendMessageA(same, EM_SETSEL, 0, len);
                char fmt[256];
                sprintf_s(fmt, 0x100, "%3.2f", out);
                SendMessageA(same, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(fmt));
            }
            SelectAccessoryTimelineTrack(s.SelectedAccessorySlot());
            CommitEditControlTail(app, edit);
            return;
        }
        if (accessoryControlId != 0) { // acc == null: tail only
            SelectAccessoryTimelineTrack(s.SelectedAccessorySlot());
            CommitEditControlTail(app, edit);
            return;
        }
    }
    if (edit == GetDlgItem(main, panel::kSelfShadowRangeEdit)) { // 0x231 shadow range
        char text[256];
        GetWindowTextA(edit, text, 8);
        const double v = atof(text);
        s.state.selfShadowDistance = // 0xA0D2C
            static_cast<float>((10000.0 - v) / 100000.0);
        SendMessageA(GetDlgItem(main, panel::kSelfShadowRangeSlider), TBM_SETPOS, 1,
                     static_cast<LPARAM>(static_cast<int>(v)));
        SelectTimelineTrack(GlobalTimelineTrack::SelfShadow);
        CommitEditControlTail(app, edit);
        return;
    }
    // 506/511/516/521: morph values of the active model.  The four controls
    // address the four selector slots in order; their control IDs are spaced
    // by five, but their model fields are consecutive.
    {
        int morphEdit = 0;
        if (edit == GetDlgItem(main, panel::kMorphEdit0))
            morphEdit = panel::kMorphEdit0;
        else if (edit == GetDlgItem(main, panel::kMorphEdit1))
            morphEdit = panel::kMorphEdit1;
        else if (edit == GetDlgItem(main, panel::kMorphEdit2))
            morphEdit = panel::kMorphEdit2;
        else if (edit == GetDlgItem(main, panel::kMorphEdit3))
            morphEdit = panel::kMorphEdit3;
        if (morphEdit != 0) {
            char text[256];
            GetWindowTextA(edit, text, 8);
            const double v = atof(text);
            mdl::ModelRecord* model = SlotModel(app);
            const int selector = model->selectedMorphs[(morphEdit - panel::kMorphEdit0) / 5];
            if (model->morphs != nullptr && selector >= 0)
                model->morphs[selector].value = static_cast<float>(v);
            SendMessageA(GetDlgItem(main, morphEdit - 1), TBM_SETPOS, 1,
                         static_cast<LPARAM>(static_cast<int>(v * 100.0)));
            CommitEditControlTail(app, edit);
            return;
        }
    }
    CommitEditControlTail(app, edit); // default tail
}

// ---------------------------------------------------------------------------
// VA 0x00464170 - EDIT subclass proc.
// ---------------------------------------------------------------------------
LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    MMDApp* app = g_Block;
    if (Msg == WM_KEYDOWN) {
        if (wParam == 13) { // VK_RETURN
            CommitEditControl(app, hWnd);
            SetFocus(app->MainWindow());
            return 0;
        }
    } else if (Msg == WM_KILLFOCUS) {
        CommitEditControl(app, reinterpret_cast<HWND>(wParam));
    }
    return CallWindowProcA(app->OriginalEditProc(), hWnd, Msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// VA 0x0040EC90 - trackbar subclass proc.
// ---------------------------------------------------------------------------
LRESULT CALLBACK TrackbarSubclassProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    MMDApp* app = g_Block;
    if (Msg == WM_LBUTTONUP)
        SetFocus(app->MainWindow());
    return CallWindowProcA(app->OriginalTrackbarProc(), hWnd, Msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// The 42 SetWindowLongPtrA(GWLP_WNDPROC) install sites of sub_466D20
// (0x467BE4..0x46A8E7), in original per-site order.  The first EDIT (417)
// additionally captures the EDIT class proc into app+0xA08D0 (0x467BD7) and
// the first trackbar (447) captures the trackbar class proc into
// app+0xA0CDC (0x4686E2); every later site only installs.
// ---------------------------------------------------------------------------
void InstallControlSubclasses(MMDApp* app, HWND hwnd) {
    static const int kEditIds[] = {
        panel::kCurrentFrameEdit,    panel::kFovEdit,
        panel::kLightColorEditR,     panel::kLightColorEditG,
        panel::kLightColorEditB,     panel::kLightDirEditX,
        panel::kLightDirEditY,       panel::kLightDirEditZ,
        panel::kAccPosXEdit,         panel::kAccPosYEdit,
        panel::kAccPosZEdit,         panel::kAccRotXEdit,
        panel::kAccRotYEdit,         panel::kAccRotZEdit,
        panel::kAccScaleEdit,        panel::kAccOpacityEdit,
        panel::kMorphEdit0,          panel::kMorphEdit1,
        panel::kMorphEdit2,          panel::kMorphEdit3,
        panel::kReadoutPosXEdit,     panel::kReadoutPosYEdit,
        panel::kReadoutPosZEdit,     panel::kReadoutRotXEdit,
        panel::kReadoutRotYEdit,     panel::kReadoutRotZEdit,
        panel::kReadoutDistEdit,     panel::kGotoFrameEdit,
        panel::kSelfShadowRangeEdit,
    };
    static const int kTrackIds[] = {
        panel::kFovSlider,
        panel::kLightColorSliderR,
        panel::kLightColorSliderG,
        panel::kLightColorSliderB,
        panel::kLightDirSliderX,
        panel::kLightDirSliderY,
        panel::kLightDirSliderZ,
        panel::kMorphSlider0,
        panel::kMorphSlider1,
        panel::kMorphSlider2,
        panel::kMorphSlider3,
        panel::kFrameVolumeSlider,
        panel::kSelfShadowRangeSlider,
    };

    HWND firstEdit = GetDlgItem(hwnd, panel::kCurrentFrameEdit);
    app->OriginalEditProc() =
        reinterpret_cast<WNDPROC>(GetWindowLongPtrA(firstEdit, GWLP_WNDPROC)); // 0x467BD7
    SetWindowLongPtrA(firstEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditSubclassProc));

    HWND firstTrack = GetDlgItem(hwnd, panel::kFovSlider);
    app->OriginalTrackbarProc() =
        reinterpret_cast<WNDPROC>(GetWindowLongPtrA(firstTrack, GWLP_WNDPROC)); // 0x4686E2
    SetWindowLongPtrA(firstTrack, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TrackbarSubclassProc));

    for (int id : kEditIds) {
        if (id == panel::kCurrentFrameEdit)
            continue;
        SetWindowLongPtrA(GetDlgItem(hwnd, id), GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(EditSubclassProc));
    }
    for (int id : kTrackIds) {
        if (id == panel::kFovSlider)
            continue;
        SetWindowLongPtrA(GetDlgItem(hwnd, id), GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(TrackbarSubclassProc));
    }
}

void InstallControlSubclass(MMDApp* app, HWND control, int id) {
    static const int kEditIds[] = {
        panel::kCurrentFrameEdit,    panel::kFovEdit,
        panel::kLightColorEditR,     panel::kLightColorEditG,
        panel::kLightColorEditB,     panel::kLightDirEditX,
        panel::kLightDirEditY,       panel::kLightDirEditZ,
        panel::kAccPosXEdit,         panel::kAccPosYEdit,
        panel::kAccPosZEdit,         panel::kAccRotXEdit,
        panel::kAccRotYEdit,         panel::kAccRotZEdit,
        panel::kAccScaleEdit,        panel::kAccOpacityEdit,
        panel::kMorphEdit0,          panel::kMorphEdit1,
        panel::kMorphEdit2,          panel::kMorphEdit3,
        panel::kReadoutPosXEdit,     panel::kReadoutPosYEdit,
        panel::kReadoutPosZEdit,     panel::kReadoutRotXEdit,
        panel::kReadoutRotYEdit,     panel::kReadoutRotZEdit,
        panel::kReadoutDistEdit,     panel::kGotoFrameEdit,
        panel::kSelfShadowRangeEdit,
    };
    static const int kTrackIds[] = {
        panel::kFovSlider,
        panel::kLightColorSliderR,
        panel::kLightColorSliderG,
        panel::kLightColorSliderB,
        panel::kLightDirSliderX,
        panel::kLightDirSliderY,
        panel::kLightDirSliderZ,
        panel::kMorphSlider0,
        panel::kMorphSlider1,
        panel::kMorphSlider2,
        panel::kMorphSlider3,
        panel::kFrameVolumeSlider,
        panel::kSelfShadowRangeSlider,
    };
    if (control == nullptr)
        return;
    for (int editId : kEditIds) {
        if (id != editId)
            continue;
        if (id == panel::kCurrentFrameEdit) {
            app->OriginalEditProc() =
                reinterpret_cast<WNDPROC>(GetWindowLongPtrA(control, GWLP_WNDPROC));
        }
        SetWindowLongPtrA(control, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditSubclassProc));
        return;
    }
    for (int trackId : kTrackIds) {
        if (id != trackId)
            continue;
        if (id == panel::kFovSlider) {
            app->OriginalTrackbarProc() =
                reinterpret_cast<WNDPROC>(GetWindowLongPtrA(control, GWLP_WNDPROC));
        }
        SetWindowLongPtrA(control, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TrackbarSubclassProc));
        return;
    }
}

} // namespace mikudancestudio
