// ===========================================================================
// VA 0x0044AEE0 - HandleHScroll  (original: sub_44AEE0)
// ===========================================================================
// WM_HSCROLL handler, dispatched from WndProc 0x4C3A10 with lParam = control
// HWND (a2) and wParam = scroll code (a3).  Original signature:
//   int __thiscall sub_44AEE0(MMDApp* this /*ecx*/, HWND a2,
//                            unsigned int a3)
// `this` is the g_Block global; the ported signature carries the two stack
// args only.
//
// Controls are matched against GetDlgItem(mainHwnd, id) in this exact order
// (mainHwnd = this+0xA06B8 = 657080):
//
//   505/510/515/520  bone X/Y/Z/rot sliders (0x1F9/0x1FE/0x203/0x208)
//     Gate: current model (slot ptr this+1920[byte this+2320]) per-axis
//     selected bone index (model+0x2D9C/0x2DA0/0x2DA4/0x2DA8) > 0 AND PMX
//     physics byte model+0x38FE (14590) == 2.  On pass: TBM_GETPOS (0x400),
//     pos/100.0 (double divide, float store) written into the 136-byte
//     stride bone record array (pointer field at model+0x26C4, float at
//     136*idx+48) - only when the array pointer is non-null; label echo
//     sprintf_s "%5.4f" via SetWindowTextA into 506/511/516/521.  The
//     original re-derives the model pointer after the SendMessageA (kept).
//   455/456/457  RGB sliders (0x1C7-0x1C9)
//     pos * 0.00390625 (double mul dbl_529680, float store) into
//     this+0x9E1A4/0x9E1A8/0x9E1AC and mirror this+0x9E194/0x9E198/0x9E19C;
//     echo "%3d" of (int)(float*256.0) (double mul dbl_52B9E8, ftol) into
//     461/462/463 via GetWindowTextLengthA + EM_SETSEL (0xB1) + WM_SETTEXT
//     (0xC2); RefreshRequest(-2) (0x440AC0).
//   458/459/460  accessory pos/rot sliders (0x1CA-0x1CC)
//     pos/100.0 (double divide) into this+0x9E174/0x9E178/0x9E17C; echo
//     "%+3.1f" into 464/465/466 (same EM_SETSEL/WM_SETTEXT pattern); mirror
//     into this+0x9E1C0/0x9E1C4/0x9E1C8; then scene object (locale
//     subsystem this+0xA06C4 -> +0x1D4E0) vtable slot 0xCC called
//     __stdcall(obj, 0, this+0x9E180); RefreshRequest(-2).
//   447  FOV slider (0x1BF)
//     pos -> this+0x9E1E8 (float); echo "%3d" into 448 (EM_SETSEL/
//     WM_SETTEXT); fovRad = float(fov * 0.01745329238474369) (double mul
//     dbl_52BB20); D3DXMatrixPerspectiveFovLH(&proj, fovRad, aspect, 1.0f,
//     100000.0f) with aspect = locale+0x1D4EC (float), near = fld1,
//     far = flt_52BB28 = 100000.0f; device->SetTransform(D3DTS_PROJECTION,
//     &proj) (x64 sub_7FF7CB45E060: vtable+0x160 = slot 44; x86 +0xB0/4 =
//     slot 44); RefreshRequest(-1).
//   560  physics-interval ratio slider (0x230)
//     this+0xA0D2C (658732) = (double)(10000-pos)/100000.0 (dbl_52BA00);
//     echo "%d" (raw pos) into 561 via SetWindowTextA; this+0xA0B0D
//     (658189) = 1 (dirty); RefreshRequest(-3).
//   428  timeline strip (0x1AC)
//     switch on the code low 16 bits (cases 0/1/2/3/5, others fall through
//     to the tail):
//       0/1: if this+0xA0D38 (658744) != 0 OR this+4 <= this+0xA0D40
//            (658752): frame this+0x97C (2428) += -1/+1; else camera
//            distance this+0xA08DC (657628) float += +1.0/-1.0 (double add
//            dbl_5294C0, float store)
//       2/3: frame -= / += this+0x970 (2416, timeline SCROLLINFO nPage)
//       5:   frame += HIWORD(wParam) - this+0x974 (2420, timeline nMin)
//     tail: if (uint32)frame > 0xFFFEF920u (cmp/jbe, unsigned) frame = 0;
//     PanelPaint (0x414610); if this+0xA06CC (657100) != 0:
//     TimelineDrawTicks(frame, HGDIOBJ this+0xA06C8 (657096)) (0x4C2A00)
//     and InvalidateRect(main, {6, 95, this+0xA06C8-3, 146}, FALSE).
//
// Notes on 1:1 fidelity:
//   * All float math follows the original x87 shape: integer/double
//     operations in double precision, results stored to float.
//   * The original re-fetches GetDlgItem(main, id) before every use; the
//     handle is deterministic so a single fetch is taken (same convention
//     as ui_scroll_mouse.cpp / HandleVScroll).
//   * D3DXMatrixPerspectiveFovLH is a load-time import in the original
//     (the exe cannot start without d3dx9_32.dll); the port resolves it
//     dynamically through d3dx_dyn.hpp and skips the projection update when
//     absent (d3d_init.cpp convention).
//
// Reference: ../translated/MikuMikuDance/fcn_0044aee0.cpp
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

// One of the four bone-transform sliders (505/510/515/520).  The original
// repeats the block with different operands; the axis offsets live on the
// current model (per-axis selected bone index), the echo label differs.
void HandleBoneSlider(MMDApp* app, HWND hwnd, int sliderId,
                      std::size_t lane, int echoId) {
    unsigned char* model = app->SelectedModel();
    const std::int32_t boneIdx = mdl::Mdl(model)->selectedMorphs[lane];
    // Gate (0x44AF11): proceed iff (physics == 2 || boneIdx > 0) &&
    // boneIdx >= 0 - the original ORs the physics byte with a positive
    // index; only a negative index unconditionally skips.
    if (boneIdx < 0 ||
        (boneIdx <= 0 && mdl::Mdl(model)->physicsMode != 2)) {
        return;
    }

    const LRESULT pos = SendMessageA(GetDlgItem(hwnd, sliderId),
                                     TBM_GETPOS, 0, 0);
    const double scaled = static_cast<double>(pos) / 100.0;

    model = app->SelectedModel();
    mdl::MorphRecord* records = mdl::Morphs(model);
    if (records != nullptr) {
        const std::int32_t idx = mdl::Mdl(model)->selectedMorphs[lane];
        records[static_cast<std::size_t>(idx)].value =
            static_cast<float>(scaled);
    }

    char buf[256];
    sprintf_s(buf, 0x100, "%5.4f", scaled);
    SetWindowTextA(GetDlgItem(hwnd, echoId), buf);
}

// Edit-control echo sequence used by the RGB/acc/FOV blocks: read the
// current text length, select the whole text (EM_SETSEL 0xB1), then replace
// it (WM_SETTEXT 0xC2).  The original re-fetches the control handle before
// each message; the handle is deterministic so one fetch suffices.
void EchoEditText(HWND hwnd, int id, const char* text) {
    HWND edit = GetDlgItem(hwnd, id);
    SendMessageA(edit, EM_SETSEL, 0,
                 static_cast<LPARAM>(GetWindowTextLengthA(edit)));
    SendMessageA(edit, EM_REPLACESEL, 0,
                 reinterpret_cast<LPARAM>(text));
}

void FormatLegacyOneDecimal(char* text, std::size_t size, float value) {
    // The original VC9 formatter rounds an exact half away from zero;
    // modern UCRT printf follows the active ties-to-even FP rounding mode.
    const double magnitude = std::floor(std::fabs(
        static_cast<double>(value)) * 10.0 + 0.5) / 10.0;
    const double rounded = std::signbit(value) ? -magnitude : magnitude;
    sprintf_s(text, size, "%+3.1f", rounded);
}

void ApplySceneLight(MMDApp* app) {
    app->ApplyTimelineLightState();
    app->Renderer()->device->SetLight(0, &app->SceneLight());
}

}  // namespace

// Forward declarations for functions ported in this wave whose bodies live
// in ui_refresh.cpp / ui_panel_paint.cpp / ui_timeline_gfx.cpp (not yet
// added to ported_funcs.hpp; declared here with their original VAs).
void RefreshRequest(int area);        // VA 0x00440AC0
void PanelPaint(MMDApp* app);         // VA 0x00414610
void TimelineDrawTicks(int frameOffset, int width);  // VA 0x004C2A00

void HandleHScroll(LPARAM lParam, WPARAM wParam) {
    MMDApp* app = g_Block;
    HWND hwnd = static_cast<HWND>(app->Hwnd());               // 657080
    const HWND ctrl = reinterpret_cast<HWND>(lParam);         // a2
    const std::uint32_t code = LOWORD(wParam);                // a3 low 16

    // ---- 505/510/515/520: bone X/Y/Z/rot sliders -------------------------
    if (ctrl == GetDlgItem(hwnd, panel::kMorphSlider0)) {
        HandleBoneSlider(app, hwnd, 505, 0, 506);
    } else if (ctrl == GetDlgItem(hwnd, panel::kMorphSlider1)) {
        HandleBoneSlider(app, hwnd, 510, 1, 511);
    } else if (ctrl == GetDlgItem(hwnd, panel::kMorphSlider2)) {
        HandleBoneSlider(app, hwnd, 515, 2, 516);
    } else if (ctrl == GetDlgItem(hwnd, panel::kMorphSlider3)) {
        HandleBoneSlider(app, hwnd, 520, 3, 521);
    }
    // ---- 455/456/457: RGB sliders -----------------------------------------
    else if (ctrl == GetDlgItem(hwnd, panel::kLightColorSliderR)) {
        const LRESULT pos = SendMessageA(GetDlgItem(hwnd, panel::kLightColorSliderR),
                                         TBM_GETPOS, 0, 0);
        const float v =
            static_cast<float>(static_cast<double>(pos) * 0.00390625);
        app->LightColor()[0] = v;
        app->SceneLight().Ambient.r = v;
        char buf[256];
        sprintf_s(buf, 0x100, "%3d",
                  static_cast<int>(static_cast<double>(
                                       app->LightColor()[0]) *
                                   256.0));
        EchoEditText(hwnd, 461, buf);
        RefreshRequest(-2);
    } else if (ctrl == GetDlgItem(hwnd, panel::kLightColorSliderG)) {
        const LRESULT pos = SendMessageA(GetDlgItem(hwnd, panel::kLightColorSliderG),
                                         TBM_GETPOS, 0, 0);
        const float v =
            static_cast<float>(static_cast<double>(pos) * 0.00390625);
        app->LightColor()[1] = v;
        app->SceneLight().Ambient.g = v;
        char buf[256];
        sprintf_s(buf, 0x100, "%3d",
                  static_cast<int>(static_cast<double>(
                                       app->LightColor()[1]) *
                                   256.0));
        EchoEditText(hwnd, 462, buf);
        RefreshRequest(-2);
    } else if (ctrl == GetDlgItem(hwnd, panel::kLightColorSliderB)) {
        const LRESULT pos = SendMessageA(GetDlgItem(hwnd, panel::kLightColorSliderB),
                                         TBM_GETPOS, 0, 0);
        const float v =
            static_cast<float>(static_cast<double>(pos) * 0.00390625);
        app->LightColor()[2] = v;
        app->SceneLight().Ambient.b = v;
        char buf[256];
        sprintf_s(buf, 0x100, "%3d",
                  static_cast<int>(static_cast<double>(
                                       app->LightColor()[2]) *
                                   256.0));
        EchoEditText(hwnd, 463, buf);
        RefreshRequest(-2);
    }
    // ---- 458/459/460: accessory pos/rot sliders ---------------------------
    else if (ctrl == GetDlgItem(hwnd, panel::kLightDirSliderX)) {
        const LRESULT pos = SendMessageA(GetDlgItem(hwnd, panel::kLightDirSliderX),
                                         TBM_GETPOS, 0, 0);
        app->LightDirection()[0] =
            static_cast<float>(static_cast<double>(pos) / 100.0);
        char buf[256];
        FormatLegacyOneDecimal(buf, 0x100, app->LightDirection()[0]);
        EchoEditText(hwnd, 464, buf);
        ApplySceneLight(app);
        RefreshRequest(-2);
    } else if (ctrl == GetDlgItem(hwnd, panel::kLightDirSliderY)) {
        const LRESULT pos = SendMessageA(GetDlgItem(hwnd, panel::kLightDirSliderY),
                                         TBM_GETPOS, 0, 0);
        app->LightDirection()[1] =
            static_cast<float>(static_cast<double>(pos) / 100.0);
        char buf[256];
        FormatLegacyOneDecimal(buf, 0x100, app->LightDirection()[1]);
        EchoEditText(hwnd, 465, buf);
        ApplySceneLight(app);
        RefreshRequest(-2);
    } else if (ctrl == GetDlgItem(hwnd, panel::kLightDirSliderZ)) {
        const LRESULT pos = SendMessageA(GetDlgItem(hwnd, panel::kLightDirSliderZ),
                                         TBM_GETPOS, 0, 0);
        app->LightDirection()[2] =
            static_cast<float>(static_cast<double>(pos) / 100.0);
        char buf[256];
        FormatLegacyOneDecimal(buf, 0x100, app->LightDirection()[2]);
        EchoEditText(hwnd, 466, buf);
        ApplySceneLight(app);
        RefreshRequest(-2);
    }
    // ---- 447: FOV slider ---------------------------------------------------
    else if (ctrl == GetDlgItem(hwnd, panel::kFovSlider)) {
        const LRESULT pos = SendMessageA(GetDlgItem(hwnd, panel::kFovSlider),
                                         TBM_GETPOS, 0, 0);
        app->CameraFov() = static_cast<float>(pos);
        char buf[256];
        sprintf_s(buf, 0x100, "%3d", static_cast<int>(app->CameraFov()));
        EchoEditText(hwnd, 448, buf);

        // fovRad = float(fovDeg * dbl_52BB20), dbl_52BB20 = 0.01745329238474369
        const float fovRad = static_cast<float>(
            static_cast<double>(app->CameraFov()) * 0.01745329238474369);
        D3DRenderer* locale = app->Renderer();
        const float aspect = locale->aspectRatio;  // 0x1D4EC

        auto* d3dx = &d3dx::Get();
        if (d3dx->Load()) {
            d3dx::D3DXMATRIXF proj;
            // near = fld1 (1.0f), far = flt_52BB28 (100000.0f)
            d3dx->perspectiveFovLH(&proj, fovRad, aspect, 1.0f, 100000.0f);
            // 原版 sub_7FF7CB45E060 @ 0x7FF7CB45EE03: `call [rax+160h]` ——
            // x64 vtable 偏移 +0x160（352 = 8×44，槽 44），即
            // IDirect3DDevice9::SetTransform(dev, 3 /*D3DTS_PROJECTION*/,
            // &proj)；x86 原版偏移 +0xB0（176 = 4×44，同为槽 44）。裸字节
            // 偏移在 x64 移植构建下会落到槽 22（CreateVolumeTexture），
            // 故用类型化虚调用保证架构无关。
            locale->device->SetTransform(
                D3DTS_PROJECTION,
                reinterpret_cast<const D3DMATRIX*>(&proj));
        }
        RefreshRequest(-1);
    }
    // ---- 560: physics-interval ratio slider --------------------------------
    else if (ctrl == GetDlgItem(hwnd, panel::kSelfShadowRangeSlider)) {
        const LRESULT pos = SendMessageA(GetDlgItem(hwnd, panel::kSelfShadowRangeSlider),
                                         TBM_GETPOS, 0, 0);
        app->state.physicsInterval = static_cast<float>(
            static_cast<double>(10000 - static_cast<int>(pos)) / 100000.0);
        char buf[256];
        sprintf_s(buf, 0x100, "%d", static_cast<int>(pos));
        SetWindowTextA(GetDlgItem(hwnd, panel::kSelfShadowRangeEdit), buf);
        app->SceneModified() = 1;
        RefreshRequest(-3);
    }
    // ---- 428: timeline strip ------------------------------------------------
    else if (ctrl == GetDlgItem(hwnd, panel::kTimelineHScroll)) {
        switch (code) {
        case 0:  // SB_LINEUP
            if (app->FloatingWindow() != nullptr ||
                app->MouseX() <=
                    app->state.hideRight) {
                --app->state.timelineStartFrame;
            } else {
                app->CameraDistance() = static_cast<float>(
                    static_cast<double>(app->CameraDistance()) + 1.0);
            }
            break;
        case 1:  // SB_LINEDOWN
            if (app->FloatingWindow() != nullptr ||
                app->MouseX() <=
                    app->state.hideRight) {
                ++app->state.timelineStartFrame;
            } else {
                app->CameraDistance() = static_cast<float>(
                    static_cast<double>(app->CameraDistance()) - 1.0);
            }
            break;
        case 2:  // SB_PAGEUP
            app->state.timelineStartFrame -=
                app->state.timelineScrollNPage;  // timeline SCROLLINFO nPage
            break;
        case 3:  // SB_PAGEDOWN
            app->state.timelineStartFrame +=
                app->state.timelineScrollNPage;
            break;
        case 5:  // SB_THUMBPOSITION
            // x64 0x7FF7CB45EF76：读的是内嵌 SCROLLINFO 的 nPos（app+0x1444）
            // 而非 nMin——按住滑块拖动时按增量 abs 新位置 - 上次位置移动。
            app->state.timelineStartFrame +=
                static_cast<std::int32_t>(HIWORD(wParam)) -
                app->state.timelineScrollNPos;  // timeline SCROLLINFO nPos
            break;
        default:
            break;
        }
        // unsigned wrap (asm: cmp/jbe against 0xFFFEF920)
        if (static_cast<std::uint32_t>(app->state.timelineStartFrame) >
            0xFFFEF920u) {
            app->state.timelineStartFrame = 0;
        }
        PanelPaint(app);                                        // 0x414610
        if (app->state.waveEnabled != 0) {
            TimelineDrawTicks(app->state.timelineStartFrame,
                              app->SidebarWidth());
            RECT rc;
            rc.left = 6;
            rc.top = 95;
            rc.right = app->SidebarWidth() - 3;
            rc.bottom = 146;
            InvalidateRect(hwnd, &rc, FALSE);
        }
    }
}

}  // namespace mikudancestudio
