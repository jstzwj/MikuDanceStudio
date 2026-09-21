// ===========================================================================
// VA 0x0040D070 - PostLanguageSweep2  (original: sub_40D070)
// ===========================================================================
// Second-stage panel refresh after a language/UI sweep: invalidates two
// regions of the panel window (it does NOT rewrite control labels).  Called
// from the mouse-wheel morph-follow branch (HandleMouseWheel 0x44BD70) with
// the render gate this+0xA442C cleared, and from ~55 other call sites.
//
// Target window selection (shared with PostViewRefresh 0x40D130):
//   this+0xA0D38 (kDwordA0D38) != 0 -> used as an HWND directly (child
//     accessory-panel window; zero in the default state)
//   else                            -> main window this+0xA06B8
//     (MMDApp::Hwnd(), kPtrHwnd)
// GetClientRect() of that window is fetched on both paths.  The horizontal
// base is x = 0 for the child window, x = this+0xA06C8 + 9 (kDwordSidebar,
// min left-panel width) for the main window.
//
// InvalidateRect(hwnd, &r1, FALSE) - panel strip:
//   left = x + 10, top = this+0xA0D44 - 22, right = client.right - 450,
//   bottom = this+0xA0D44 - 1
// InvalidateRect(hwnd, &r2, FALSE) - value column:
//   left = x + 68, right = x + 139, top = this+0xA0D4C,
//   bottom = this+0xA0D4C + 30
// (0xA0D44 / 0xA0D4C are kDwordHideTop / kDwordHideBottom of the hide-rect
// cluster 0xA0D40..0xA0D4C.)
//
// The erase flag is FALSE (push 0) for both calls.  The original returns the
// BOOL of the last InvalidateRect; the declared signature is void.
//
// Reference: ../translated/MikuMikuDance/fcn_0040d070.cpp (the speculative
// offsets in that file are superseded by the IDA disassembly)
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

void PostLanguageSweep2(MMDApp* app) {
    // Target window: this+0xA0D38 doubles as an HWND when non-zero.
    HWND hwnd = reinterpret_cast<HWND>(app->state.floatingWindow);
    RECT client;
    int x;
    if (hwnd != nullptr) {                          // 0x40D113
        GetClientRect(hwnd, &client);
        x = 0;
    } else {                                        // 0x40D089
        hwnd = reinterpret_cast<HWND>(app->state.hwnd);
        GetClientRect(hwnd, &client);
        x = app->state.sidebarWidth + 9;  // 0xA06C8
    }

    // Panel strip: (x+10, hideTop-22) .. (client.right-450, hideTop-1).
    const std::int32_t hideTop =
        app->state.hideTop;  // 0xA0D44
    RECT rc;
    rc.left = x + 10;
    rc.bottom = hideTop - 1;
    rc.top = hideTop - 22;
    rc.right = client.right - 450;
    InvalidateRect(hwnd, &rc, FALSE);               // 0x40D0DD

    // Value column: (x+68, hideBottom) .. (x+139, hideBottom+30).
    rc.top = app->state.hideBottom;  // 0xA0D4C
    rc.left = x + 68;
    rc.right = x + 139;
    rc.bottom = rc.top + 30;
    InvalidateRect(hwnd, &rc, FALSE);               // 0x40D109
}

// ===========================================================================
// VA 0x0040D130 - PostViewRefresh  (original: sub_40D130)
// ===========================================================================
// Refresh tail shared by the wheel / scroll / editor / frame-driver paths
// (HandleMouseWheel 0x44BD70, HandleMouseMove 0x414610, sub_4341E0, the
// frame driver 0x46B090, ...).  Rewrites the seven selected-item readout
// controls 0x220..0x226 of the panel with the current values.
//
// Target window: same this+0xA0D38 / this+0xA06B8 selection as
// PostLanguageSweep2.
//
// Camera/light mode (this+0x2F8 = kByteOptflag0 != 0):
//   0x220/0x221/0x222: "%1.2f" of this+0x334/0x338/0x33C (kFloatPosx/y/z);
//     the Z value is negated unless this+0xA0430 (kDwordA0430) < 0.
//   0x223: "%1.1f" of -this+0x310 / (double)3.141592f * 180.0, truncated to
//     float, snapped to 0 when |v| < (double)(1e-7f) [dbl_52B758].
//   0x224/0x225: "%1.1f" of this+0x314/0x318 / (double)3.141592f * 180.0
//     (kept in double, no float truncation - 0x40D2D5/0x40D31B store
//     straight into the double vararg slot).
//   0x226: "%1.2f" of -this+0xA08DC (kFloatCamangle) with the same 1e-7f
//     snap when this+0xA0430 < 0, else "%1.2f" of 0.0.
//
// Bone mode (this+0x2F8 == 0): current model = slot array this+0x780
// [byte this+0x910]; selected bone index = model+0x2D90; if < 0, all seven
// controls get "-----".  Otherwise bone record = (model+0x26BC) + sizeof(mikudancestudio::mdl::BoneRecord) *idx
// (604 = 0x25C stride) and the layout is picked by
//     (this+0xA0CC4 == 3) & &bone->hasRigidBody:
//   rotation layout  : "%1.2f" of bone->ikBackup/0x18C/0x190, quat at +0x194
//   position layout  : "%1.2f" of bone->trans/0x144/0x148, quat at +0x14C
// The original re-derives the model pointer before every access (no side
// effects intervene, so one load is taken, as in ui_scroll_mouse.cpp).
//
// Then D3DXMatrixRotationQuaternion (d3dx9_32 import, resolved through
// d3dx_dyn.hpp; the original hard-imports it and calls unconditionally)
// followed by the Euler extraction (0x40D643-0x40D7BF), written to
// this+0xA04C0/0xA04C4/0xA04C8 (rx/ry/rz):
//   rz = atan2(m12, m22);  rx = asin(-m32);  ry = atan2(m31, m33)
//   (the atan2 calls dispatch through the original sub_50791A, an MSVC
//   __cintrindisp2 thunk for the CRT "atan2" - the port calls std::atan2)
//   if |cos(rx)| < 1e-6f (flt_52B740): gimbal lock -
//     rz += m12 > 0 ? +3.141592f : -3.141592f (flt_52B738/73C)
//     ry += m31 > 0 ? +3.141592f : -3.141592f
//   if |angle| < 1e-6f -> angle = 0 (all three).  Mode-3 A/B proves this
//   threshold: the original preserves ordinary -26.4/11.5 degree X/Y values.
//   degrees: rx/pi*180, -ry/pi*180, 180*(-rz/pi) with pi = (double)3.141592f
//   (dbl_52B768) and 180.0 (dbl_52B760)
//   0x223/0x224/0x225: "%1.1f" of the three degrees values, each snapped to
//     0 when |v| < (double)(1e-7f).
// Every float store in the original goes through an x87 fstp dword (float
// truncation); the port mirrors that with explicit static_cast<float>.
//
// The original returns the BOOL of the last SetWindowTextA; the declared
// signature is void.
//
// Reference: ../translated/MikuMikuDance/fcn_0040d130.cpp (superseded by
// the IDA disassembly for the gimbal/zeroing constants and thresholds)
// =========================================================================//
void PostViewRefresh(MMDApp* app) {
    HWND hwnd = reinterpret_cast<HWND>(app->state.floatingWindow);
    if (hwnd == nullptr)                            // 0x40D157
        hwnd = reinterpret_cast<HWND>(app->state.hwnd);

    char text[260];  // CHAR String[260] @ ebp-108h; sprintf_s count 0x100

    if (app->state.optflag[0] != 0) {   // this+0x2F8
        // ---- camera / light readout ----------------------------------------
        sprintf_s(text, 0x100, "%1.2f",
                  static_cast<double>(app->CameraPositionX()));
        SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutPosXEdit), text);
        sprintf_s(text, 0x100, "%1.2f",
                  static_cast<double>(app->CameraPositionY()));
        SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutPosYEdit), text);
        float posz = app->CameraPositionZ();
        if (app->CameraParentModel() >= 0)
            posz = -posz;
        sprintf_s(text, 0x100, "%1.2f", static_cast<double>(posz));
        SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutPosZEdit), text);
        float ang = static_cast<float>(                       // 0x40D25C
            -static_cast<double>(app->CameraPitch()) /
            3.141592f * 180.0f);
        if (std::fabs(static_cast<double>(ang)) < 1e-7f)      // 0x40D26E
            ang = 0.0f;
        sprintf_s(text, 0x100, "%1.1f", static_cast<double>(ang));
        SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutRotXEdit), text);
        sprintf_s(text, 0x100, "%1.1f",                       // 0x40D2E3
                  static_cast<double>(app->CameraYaw()) /
                      3.141592f * 180.0f);
        SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutRotYEdit), text);
        sprintf_s(text, 0x100, "%1.1f",                       // 0x40D329
                  static_cast<double>(app->CameraRoll()) /
                      3.141592f * 180.0f);
        SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutRotZEdit), text);
        if (app->CameraParentModel() < 0) {
            float camAngle = static_cast<float>(
                -static_cast<double>(app->CameraDistance()));
            if (std::fabs(static_cast<double>(camAngle)) < 1e-7f)
                camAngle = 0.0f;
            sprintf_s(text, 0x100, "%1.2f", static_cast<double>(camAngle));
        } else {
            sprintf_s(text, 0x100, "%1.2f", 0.0);
        }
        SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutDistEdit), text);
        return;
    }

    // ---- bone readout ------------------------------------------------------
    unsigned char* model = app->SelectedModel();
    const std::int32_t boneIdx = mikudancestudio::mdl::Mdl(model)->selectedBone;
    if (boneIdx < 0) {                                     // 0x40D401 jl
        for (int id = 0x220; id <= 0x226; ++id)
            SetWindowTextA(GetDlgItem(hwnd, id), "-----");
        return;
    }
    mikudancestudio::mdl::BoneRecord* bone =
        &mikudancestudio::mdl::Bones(model)[boneIdx];
    const float* display;
    const float* quat;
    if (((app->PlaybackPhysicsMode() == 3) &                 // 0x40D417
         (*reinterpret_cast<unsigned char*>(&bone->hasRigidBody) != 0)) != 0) {
        // rotation-mode layout
        display = reinterpret_cast<const float*>(bone->ikBackup);  // +0x18C/+0x190
        quat = reinterpret_cast<const float*>(bone->ikBackup + 3);
    } else {
        // position/scale layout
        display = reinterpret_cast<const float*>(bone->trans);  // +0x144/+0x148
        quat = reinterpret_cast<const float*>(bone->rotQuat);
    }
    sprintf_s(text, 0x100, "%1.2f", static_cast<double>(display[0]));
    SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutPosXEdit), text);
    sprintf_s(text, 0x100, "%1.2f", static_cast<double>(display[1]));
    SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutPosYEdit), text);
    sprintf_s(text, 0x100, "%1.2f", static_cast<double>(display[2]));
    SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutPosZEdit), text);

    d3dx::D3DXMATRIXF m;  // 64 bytes @ ebp-148h; filled by the D3DX call
    auto* d3dx = &d3dx::Get();
    d3dx->matrixRotationQuaternion(&m, quat);       // (x,y,z,w quaternion)

    // Euler extraction (0x40D643..).  Every intermediate is float-truncated
    // exactly as the original x87 sequence (fstp dword) does.
    const float m12 = m.m[0][1];
    const float m22 = m.m[1][1];
    const float m31 = m.m[2][0];
    const float m32 = m.m[2][1];
    const float m33 = m.m[2][2];

    float rz = static_cast<float>(std::atan2(static_cast<double>(m12),
                                             static_cast<double>(m22)));
    app->BoneRotationEditDegreesZ() = rz;               // 0x40D658 (0xA04C8)
    float rx = static_cast<float>(std::asin(-static_cast<double>(m32)));
    app->BoneRotationEditDegreesX() = rx;               // 0x40D671 (0xA04C0)
    float ry = static_cast<float>(std::atan2(static_cast<double>(m31),
                                             static_cast<double>(m33)));
    app->BoneRotationEditDegreesY() = ry;               // 0x40D68C (0xA04C4)
    float c = static_cast<float>(std::cos(static_cast<double>(rx)));

    if (std::fabs(static_cast<double>(c)) < 1e-6f) {    // 0x40D6AD flt_52B740
        // Gimbal lock: add +/- 3.141592f (flt_52B738/73C) by the sign of the
        // matrix element; double add, float store (0x40D6E9/0x40D710).
        rz = static_cast<float>(static_cast<double>(rz) +
                                (m12 > 0.0f ? 3.141592f : -3.141592f));
        ry = static_cast<float>(static_cast<double>(ry) +
                                (m31 > 0.0f ? 3.141592f : -3.141592f));
    }
    // Zeroing (0x40D728/0x40D747/0x40D766).  Treating the comparison operand
    // as |cos(rx)| erased normal sub-radian angles; the original uses 1e-6f.
    if (std::fabs(static_cast<double>(rx)) < 1e-6f)
        rx = 0.0f;
    if (std::fabs(static_cast<double>(ry)) < 1e-6f)
        ry = 0.0f;
    if (std::fabs(static_cast<double>(rz)) < 1e-6f)
        rz = 0.0f;

    // Radians -> degrees in double, pi = (double)3.141592f (dbl_52B768),
    // 180.0 (dbl_52B760); each result stored back as float.
    rx = static_cast<float>(static_cast<double>(rx) / 3.141592f * 180.0f);
    ry = static_cast<float>(-static_cast<double>(ry) / 3.141592f * 180.0f);
    rz = static_cast<float>(180.0f * (-static_cast<double>(rz) / 3.141592f));
    app->BoneRotationEditDegreesX() = rx;               // 0x40D797 (fst)
    app->BoneRotationEditDegreesY() = ry;               // 0x40D7A9
    app->BoneRotationEditDegreesZ() = rz;               // 0x40D7BF

    // Readouts; the original reloads this+0xA04C4/0xA04C8 from the object.
    float d = app->BoneRotationEditDegreesX();
    if (std::fabs(static_cast<double>(d)) < 1e-7f)      // 0x40D7D7 dbl_52B758
        d = 0.0f;
    sprintf_s(text, 0x100, "%1.1f", static_cast<double>(d));
    SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutRotXEdit), text);
    d = app->BoneRotationEditDegreesY();
    if (std::fabs(static_cast<double>(d)) < 1e-7f)
        d = 0.0f;
    sprintf_s(text, 0x100, "%1.1f", static_cast<double>(d));
    SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutRotYEdit), text);
    d = app->BoneRotationEditDegreesZ();
    if (std::fabs(static_cast<double>(d)) < 1e-7f)
        d = 0.0f;
    sprintf_s(text, 0x100, "%1.1f", static_cast<double>(d));
    SetWindowTextA(GetDlgItem(hwnd, panel::kReadoutRotZEdit), text);
}

}  // namespace mikudancestudio
