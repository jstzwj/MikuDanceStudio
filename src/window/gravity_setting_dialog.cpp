// Gravity dialog: initialization, edit subclass, channel updates and registration.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "btBulletDynamicsCommon.h"
#include "dialog_scaffold.hpp"

namespace mikudancestudio {
namespace {

// ===========================================================================
// 0x00423160 - gravity-setting dialog init (x64 sub_7FF7CB4B5F00)
// ===========================================================================
// Echoes the gravity cluster into the dialog controls: "%3.2f" of the
// magnitude (0x9EDC8) into edit 709 and of the direction X/Y/Z (0x9EDBC /
// 0x9EDC0 / 0x9EDC4) into edits 710..712; trackbars 637..639 get range
// -100..100, tick frequency 0x3E8 and pos = value * 100; "%d" of the noise
// dword (0x9EDCC) into edit 713; checkbox 731 mirrors the noise-mode byte
// 0xA0CD4 and gates edit 713 through EnableWindow.
// =========================================================================//
void InitGravityDialog(HWND hDlg) { // VA 0x00423160
    MMDApp* app = g_Block;
    char buf[0x100];
    sprintf_s(buf, 0x100, "%3.2f", static_cast<double>(app->state.gravityMagnitude));
    SetWindowTextA(GetDlgItem(hDlg, panel::kGravityAccelEdit), buf);
    const float direction[3] = {app->state.gravityX, app->state.gravityY, app->state.gravityZ};
    static const int kDirectionEdits[3] = {panel::kGravityDirXEdit, panel::kGravityDirYEdit,
                                           panel::kGravityDirZEdit};
    static const int kDirectionTracks[3] = {panel::kGravityDirXSlider, panel::kGravityDirYSlider,
                                            panel::kGravityDirZSlider};
    for (int axis = 0; axis < 3; ++axis) {
        sprintf_s(buf, 0x100, "%3.2f", static_cast<double>(direction[axis]));
        SetWindowTextA(GetDlgItem(hDlg, kDirectionEdits[axis]), buf);
        SendMessageA(GetDlgItem(hDlg, kDirectionTracks[axis]), TBM_SETRANGEMIN, 0, -100);
        SendMessageA(GetDlgItem(hDlg, kDirectionTracks[axis]), TBM_SETRANGEMAX, 0, 100);
        SendMessageA(GetDlgItem(hDlg, kDirectionTracks[axis]), TBM_SETTICFREQ, 0x3E8, 0);
        SendMessageA(GetDlgItem(hDlg, kDirectionTracks[axis]), TBM_SETPOS, 1,
                     static_cast<int>(static_cast<double>(direction[axis]) * 100.0));
    }
    sprintf_s(buf, 0x100, "%d", app->state.gravityNoise);
    SetWindowTextA(GetDlgItem(hDlg, panel::kGravityNoiseEdit), buf);
    if (app->state.gravityNoiseEnabled != 0) {
        SendMessageA(GetDlgItem(hDlg, panel::kGravityNoiseCheckbox), BM_SETCHECK, 1, 0);
        EnableWindow(GetDlgItem(hDlg, panel::kGravityNoiseEdit), TRUE);
    } else {
        SendMessageA(GetDlgItem(hDlg, panel::kGravityNoiseCheckbox), BM_SETCHECK, 0, 0);
        EnableWindow(GetDlgItem(hDlg, panel::kGravityNoiseEdit), FALSE);
    }
}

// VA 0x0045FD80 - gravity-setting dialog channel setter.  Channels 0..3
// write the gravity cluster (magnitude / direction X / Y / Z, app+0x9EDC4 /
// 0x9EDB8 / 0x9EDBC / 0x9EDC0), normalise the direction (D3DXVec3Normalize,
// in place), scale it by magnitude * dbl 0x52C170 (10.0) and push it into
// the physics world setGravity (world = *(app+0x9EDB0)->world, vtable
// slot 0x34/4 = 13).  An all-zero direction gets Y nudged to
// flt 0x529624 = 0.1f first.  Channel 4 stores (int)value into app+0x9EDC8.
// Ends with SelectTimelineTrack(GlobalTimelineTrack::Gravity) on every path.
void SetGravityChannel(MMDApp* app, int channel, float value) { // VA 0x0045FD80
    switch (channel) {
    case 0:
        app->state.gravityMagnitude = value;
        if (app->state.gravityX == 0.0f && app->state.gravityY == 0.0f &&
            app->state.gravityZ == 0.0f)
            app->state.gravityY = 0.1f; // flt 0x529624
        break;
    case 1:
        app->state.gravityX = value;
        if (value == 0.0f && app->state.gravityY == 0.0f && app->state.gravityZ == 0.0f)
            app->state.gravityY = 0.1f;
        break;
    case 2:
        app->state.gravityY = value;
        if (app->state.gravityX == 0.0f && app->state.gravityZ == 0.0f && value == 0.0f)
            app->state.gravityY = 0.1f;
        break;
    case 3:
        app->state.gravityZ = value;
        if (app->state.gravityX == 0.0f && app->state.gravityY == 0.0f && value == 0.0f)
            app->state.gravityY = 0.1f;
        break;
    default:
        if (channel == 4)
            app->state.gravityNoise = static_cast<std::int32_t>(value);
        SelectTimelineTrack(GlobalTimelineTrack::Gravity); // 0x440AC0
        return;
    }

    // common tail: normalise, scale by magnitude * 10, setGravity
    float dir[3] = {app->state.gravityX, app->state.gravityY, app->state.gravityZ};
    d3dx::Get().vec3Normalize(dir, dir);
    const float mag = app->state.gravityMagnitude;
    float vec[4] = {static_cast<float>(dir[0] * mag * 10.0),
                    static_cast<float>(dir[1] * mag * 10.0),
                    static_cast<float>(dir[2] * mag * 10.0), 0.0f};
    if (PhysicsScene* physicsScene = app->Physics()) {
        if (btDiscreteDynamicsWorld* world = physicsScene->world)
            world->setGravity(btVector3(vec[0], vec[1], vec[2]));
    }
    SelectTimelineTrack(GlobalTimelineTrack::Gravity); // 0x440AC0
}

// VA 0x00466370 - gravity-setting dialog (266) edit subclass: WM_KEYDOWN +
// VK_RETURN reads the edit (0x2C5..0x2C9 = 709..713) as a float and applies
// it through 0x45FD80 with channel 0..4; edits 710..712 additionally park
// the percent value on their trackbars 637..639 (TBM_SETPOS, dbl 0x52B8E0
// = 100.0); everything else goes to the saved proc (app+0xA0CD0).
LRESULT CALLBACK GravitySettingEditSubclassProc( // VA 0x00466370
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MMDApp* app = g_Block;
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        const HWND dlg = app->GravitySettingDialog();
        char buf[0x100];
        for (int ch = 0; ch < 5; ++ch) {
            if (hwnd != GetDlgItem(dlg, panel::kGravityAccelEdit + ch))
                continue;
            GetWindowTextA(hwnd, buf, 0x100);
            const float v = static_cast<float>(atof(buf));
            // Original x64 RVA 0x957A7/0x9583D/0x958E0: X/Y/Z use 637/638/639.
            if (ch >= 1 && ch <= 3)
                SendMessageA(GetDlgItem(dlg, panel::kGravityDirXSlider + (ch - 1)), TBM_SETPOS, 1,
                             static_cast<int>(v * 100.0)); // dbl 0x52B8E0
            SetGravityChannel(app, ch, v);                 // 0x45FD80
            return 0;
        }
    }
    return CallWindowProcA(app->GravitySettingEditProc(), hwnd, msg, wParam, lParam);
}

// VA 0x00460080 - gravity-setting dialog OK ("register"): dirty flag, clear the
// selection marks of the four global frame tables (camera 0x374 +0x48,
// light 0x378 +0x24, self-shadow 0x37C +0x14, gravity 0x380 +0x21) and of
// all 255 accessory tracks (0x384 blobs, +0x18, 0x3C stride), rebuild via
// 0x412B20 at the current frame, then SelectTimelineTrack(GlobalTimelineTrack::Gravity) + PanelPaint.
void ApplyGravitySettingDialog(MMDApp* app) { // VA 0x00460080
    app->SceneModified() = 1;
    mdl::CameraKey* camera = app->CameraKeys();
    mdl::LightKey* light = app->LightKeys();
    mdl::SelfShadowKey* shadow = app->ShadowKeys();
    mdl::GravityKey* gravity = app->GravityKeys();
    for (std::size_t i = 0; i < 10000; ++i) {
        camera[i].selected = 0;
        light[i].selected = 0;
        shadow[i].selected = 0;
        gravity[i].selected = 0;
    }
    for (int slot = 0; slot < 255; ++slot) {
        mdl::AccessoryKey* acc = app->AccessoryKeys(slot);
        if (acc == nullptr)
            continue;
        for (std::size_t i = 0; i < 10000; ++i)
            acc[i].selected = 0;
    }
    RegisterGravityKeyCurrent(app, app->state.currentFrame); // 0x412B20
    SelectTimelineTrack(GlobalTimelineTrack::Gravity);       // 0x440AC0
    PanelPaint(app);                                         // 0x414610
}

} // namespace

// ===========================================================================
// 0x479E90 - case 266 gravity-setting dialog (template caption "gravity
// setting" / gravity settings; 0x323 EN / 0x322 JP), modeless
// at app+0xA0CCC.  WM_INITDIALOG: top-most gate; subclass edits 709..713
// with sub_466370 (old proc of 709 saved at app+0xA0C50); sub_423160 init.
// WM_COMMAND: checkbox 731 mirrors app+0xA0CD4 and enables edit 713;
// EN_UPDATE on 709..713 -> atof + sub_45FD80(idx, f) (710..712 also sync
// their trackbars 637..639); id 1 -> sub_460080; id 2 -> DestroyWindow,
// app+0xA0CCC = 0, sub_412330, return 1.  WM_HSCROLL on trackbars
// 637/638/639 -> TBM_GETPOS/100 -> "%3.2f" into 710/711/712 + sub_45FD80.
// Reference: MikuMikuDance.exe sub_479E90.
// ===========================================================================
INT_PTR CALLBACK GravitySettingDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                       LPARAM lParam) { // VA 0x00479E90
    MMDApp* app = g_Block;
    const HWND hCtrl = reinterpret_cast<HWND>(lParam);
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        // Original x64 0x7FF7CB4B5682 / 0x7FF7CB4B5AFF use the same saved proc.
        app->GravitySettingEditProc() = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrA(GetDlgItem(hDlg, panel::kGravityAccelEdit), GWLP_WNDPROC));
        for (int i = panel::kGravityAccelEdit; i <= panel::kGravityNoiseEdit; ++i) {
            SetWindowLongPtrA(GetDlgItem(hDlg, i), GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(GravitySettingEditSubclassProc));
        }
        InitGravityDialog(hDlg); // 0x423160
        return 0;
    }
    if (msg == WM_COMMAND) {
        const WORD id = LOWORD(wParam);
        if (id == panel::kGravityNoiseCheckbox) {
            if (IsDlgButtonChecked(hDlg, panel::kGravityNoiseCheckbox) == 1) {
                app->state.gravityNoiseEnabled = 1;
                EnableWindow(GetDlgItem(hDlg, panel::kGravityNoiseEdit), TRUE);
            } else {
                app->state.gravityNoiseEnabled = 0;
                EnableWindow(GetDlgItem(hDlg, panel::kGravityNoiseEdit), FALSE);
            }
        } else if (HIWORD(wParam) == EN_UPDATE) {
            char buf[0x100];
            if (hCtrl == GetDlgItem(app->GravitySettingDialog(), panel::kGravityAccelEdit)) {
                GetWindowTextA(hCtrl, buf, 256);
                SetGravityChannel(app, 0, static_cast<float>(atof(buf))); // 0x45FD80
            } else if (hCtrl == GetDlgItem(app->GravitySettingDialog(), panel::kGravityDirXEdit)) {
                GetWindowTextA(hCtrl, buf, 256);
                const float f = static_cast<float>(atof(buf));
                SendMessageA(GetDlgItem(app->GravitySettingDialog(), panel::kGravityDirXSlider),
                             TBM_SETPOS, 1, (int)(f * 100.0));
                SetGravityChannel(app, 1, f); // 0x45FD80
            } else if (hCtrl == GetDlgItem(app->GravitySettingDialog(), panel::kGravityDirYEdit)) {
                GetWindowTextA(hCtrl, buf, 256);
                const float f = static_cast<float>(atof(buf));
                SendMessageA(GetDlgItem(app->GravitySettingDialog(), panel::kGravityDirYSlider),
                             TBM_SETPOS, 1, (int)(f * 100.0));
                SetGravityChannel(app, 2, f); // 0x45FD80
            } else if (hCtrl == GetDlgItem(app->GravitySettingDialog(), panel::kGravityDirZEdit)) {
                GetWindowTextA(hCtrl, buf, 256);
                const float f = static_cast<float>(atof(buf));
                SendMessageA(GetDlgItem(app->GravitySettingDialog(), panel::kGravityDirZSlider),
                             TBM_SETPOS, 1, (int)(f * 100.0));
                SetGravityChannel(app, 3, f); // 0x45FD80
            } else if (hCtrl == GetDlgItem(app->GravitySettingDialog(), panel::kGravityNoiseEdit)) {
                GetWindowTextA(hCtrl, buf, 256);
                SetGravityChannel(app, 4, static_cast<float>(atof(buf))); // 0x45FD80
            }
        } else if (id == IDOK) {
            ApplyGravitySettingDialog(app); // 0x460080
        } else if (id == IDCANCEL) {
            DestroyWindow(hDlg);
            app->GravitySettingDialog() = 0;
            ApplyGravityTrack(app); // 0x412330
            return 1;
        }
        return 0;
    }
    if (msg != WM_HSCROLL) {
        return 0;
    }
    if (hCtrl == GetDlgItem(hDlg, panel::kGravityDirXSlider)) {
        const double v = static_cast<double>(SendMessageA(hCtrl, TBM_GETPOS, 0, 0)) / 100.0;
        char buf[0x100];
        sprintf_s(buf, 0x100, "%3.2f", v);
        SetWindowTextA(GetDlgItem(hDlg, panel::kGravityDirXEdit), buf);
        SetGravityChannel(app, 1, static_cast<float>(v)); // 0x45FD80
    } else if (hCtrl == GetDlgItem(hDlg, panel::kGravityDirYSlider)) {
        const double v = static_cast<double>(SendMessageA(hCtrl, TBM_GETPOS, 0, 0)) / 100.0;
        char buf[0x100];
        sprintf_s(buf, 0x100, "%3.2f", v);
        SetWindowTextA(GetDlgItem(hDlg, panel::kGravityDirYEdit), buf);
        SetGravityChannel(app, 2, static_cast<float>(v)); // 0x45FD80
    } else if (hCtrl == GetDlgItem(hDlg, panel::kGravityDirZSlider)) {
        const double v = static_cast<double>(SendMessageA(hCtrl, TBM_GETPOS, 0, 0)) / 100.0;
        char buf[0x100];
        sprintf_s(buf, 0x100, "%3.2f", v);
        SetWindowTextA(GetDlgItem(hDlg, panel::kGravityDirZEdit), buf);
        SetGravityChannel(app, 3, static_cast<float>(v)); // 0x45FD80
    }
    return 1;
}

} // namespace mikudancestudio
