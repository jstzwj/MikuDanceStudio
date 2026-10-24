// ===========================================================================
// VA 0x0042CEB0 - HandlePaletteChanged  (original: sub_42CEB0, ~0x4ED bytes)
// ===========================================================================
// Timeline-strip painter of the left panel.  Called with the paint DC from
// HandleWindowPaint 0x47C0A0 (wm_paint.cpp @ 0x47C206, right after the
// panel chrome) and from the WndProc 0x4C3A10 message-0x318 branch
// (wndproc.cpp, wParam as HDC).
//
// Sequence (exact, per the IDA disassembly):
//   GetClientRect(this+0xA06B8 = kPtrHwnd) into a local RECT; `bottom` is
//   Rect.bottom everywhere below.
//   4 gradient rows are driven by ColorLerp (0x40AD00) of the colour pair
//   this+0xA05E0 / this+0xA05E4 (the two bottom-panel gradient colours), each
//   with an x87-computed factor truncated to float at the call boundary:
//     t1  = -45.0/((50.0-bottom)-161.0)          (dbl_52C940/190/948)
//     t2  = ((50.0-bottom)-228.0)/((50.0-bottom)-161.0)   (dbl_52C938)
//     v30 = (50.0-bottom)-161.0 (kept double, var_18)
//     t3a = -111.0/v30  (dbl_52C930)   t3b = -94.0/v30 (dbl_52C928)
//     t4  = ((50.0-bottom)-248.0)/((50.0-bottom)-161.0)   (dbl_52C920)
//   each result feeds FillPanelBottom (0x40DF10) as the gradient edge
//   colour; the "from" colours are the colour-table slots 0xA05E0/4,
//   0xA05F0/4, 0xA05E8/C, 0xA05F8/C (colour.txt pair table, see
//   offsets.hpp kDwordCol656xxx) or a previous lerp result.
//   Then three option-flag driven variable-width blocks (flags 0x2F8/0x2F9/
//   0x2FC, 0x2FA/0x2FD, 0x2FE/0x2FB = kByteOptflag0..6) that accumulate an
//   x-offset, followed by three closing rows (colours 0xA0618/C, 0xA0620/4).
//
// The "palette" name is historical (from the message-0x318 stub list); the
// function performs no palette API calls - it is the timeline strip painter.
// Original returns the BOOL of the last FillPanelBottom; the declared
// signature is void.
//
// Reference: ../translated/MikuMikuDance/fcn_0042ceb0.cpp (superseded by
// the IDA disassembly for the exact rect/colour/lerp arguments)
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>

#include "mikudancestudio/bottom_panel_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"

namespace mikudancestudio {
namespace {

// Shift-JIS (cp932) label bytes from .rdata, used by the Japanese-UI branch
// of HandlePaletteChanged2 (0x42C140).  Bytes match the original exactly
// (byte_52C6DC / byte_52C728 / byte_52C7E0 / byte_52C7EC / byte_52C808 /
// byte_52C8A4 / byte_52C8B0 / byte_52C8C0).
constexpr char kJpPlaying[] = "\x8d\xc4\x90\xb6\x92\x86";                      // playing
constexpr char kJpAngle[] = "\x8a\x70\x93\x78";                                // angle
constexpr char kJpBonePosition[] = "\x83\x7b\x81\x5b\x83\x93\x88\xca\x92\x75"; // bone position
constexpr char kJpCameraFollowMode[] =
    "\x83\x4a\x83\x81\x83\x89\x83\x7b\x81\x5b\x83\x93\x92\xc7\x8f\x5d\x83\x82\x81\x5b\x83\x68\x92\x86"; // camera bone-follow mode active
constexpr char kJpCameraFollowModeHint[] =
    "\x83\x4a\x83\x81\x83\x89\x83\x7b\x81\x5b\x83\x93\x92\xc7\x8f\x5d\x83\x82\x81\x5b\x83\x68\x92\x86\x20\x28\x92\xc7\x8f\x5d\x83\x7b\x83\x5e\x83\x93\x82\xf0\x89\xf0\x8f\x9c\x82\xb5\x82\xc4\x89\xba\x82\xb3\x82\xa2\x29"; // camera bone-follow mode active (release the follow button)
constexpr char kJpCameraCenter[] = "\x83\x4a\x83\x81\x83\x89\x92\x86\x90\x53"; // camera center
constexpr char kJpCamLightAccessoryPanel[] =
    "\xb6\xd2\xd7\xa5\x8f\xc6\x96\xbe\xa5\xb1\xb8\xbe\xbb\xd8"; // camera/lighting/accessory
constexpr char kJpCamLightAccessoryPanelFmt[] =
    "\xb6\xd2\xd7\xa5\x8f\xc6\x96\xbe\xa5\xb1\xb8\xbe\xbb\xd8\x20\x2f\x20\x25\x73"; // camera/lighting/accessory / %s

} // namespace

void HandlePaletteChanged(HDC hdc) {
    MMDApp* app = g_Block; // original `this` (ecx)

    RECT rc;
    GetClientRect(reinterpret_cast<HWND>(app->state.hwnd),
                  &rc); // 0x42CEC5 (this+0xA06B8)
    const std::int32_t bottom = rc.bottom;
    const std::int32_t side = app->SidebarWidth();
    const COLORREF colA = app->ThemeColor(UiThemeColor::ControlLight);
    const COLORREF colB = app->ThemeColor(UiThemeColor::ControlDark);

    // Row 1 (0x42CEF0..0x42CF1F): t1 = -45.0/((50.0-bottom)-161.0), all
    // double math with a float truncation at the call boundary (x87
    // fstp dword; the fld/fstp pair is register shuffling only).
    float t = static_cast<float>(-45.0 / ((50.0 - static_cast<double>(bottom)) - 161.0));
    std::uint32_t lerp = ColorLerp(colA, colB, t);            // 0x42CEFD (0x40AD00)
    FillPanelBottom(hdc, 3, 50, side - 3, 95, colA, lerp, 1); // 0x42CF1F (0x40DF10)

    // Row 2 (0x42CF24..0x42CF78): t2 = ((50.0-bottom)-228.0)/((50.0-bottom)-161.0).
    t = static_cast<float>(((50.0 - static_cast<double>(bottom)) - 228.0) /
                           ((50.0 - static_cast<double>(bottom)) - 161.0));
    const std::uint32_t lerp2 = ColorLerp(colA, colB, t);         // 0x42CF5E
    FillPanelBottom(hdc, 3, 95, 6, bottom - 228, lerp, lerp2, 1); // 0x42CF78

    // Row 3 (0x42CF7D..0x42CFF6): the span (50.0-bottom)-161.0 (original
    // decompiler name v30) is kept as a
    // double (fst var_18 keeps it on the x87 stack); both quotients are
    // double, each truncated to float for its ColorLerp.
    const double row3Span = (50.0 - static_cast<double>(bottom)) - 161.0;
    const std::uint32_t lerpNeg111 =
        ColorLerp(colA, colB, static_cast<float>(-111.0 / row3Span)); // 0x42CFB5
    const std::uint32_t lerpNeg94 =
        ColorLerp(colA, colB, static_cast<float>(-94.0 / row3Span));               // 0x42CFD5
    FillPanelBottom(hdc, side - 19, 144, side - 3, 161, lerpNeg94, lerpNeg111, 1); // 0x42CFF6

    // Row 4 (0x42CFFB..0x42D05C): t4 = ((50.0-bottom)-248.0)/((50.0-bottom)-161.0).
    t = static_cast<float>(((50.0 - static_cast<double>(bottom)) - 248.0) /
                           ((50.0 - static_cast<double>(bottom)) - 161.0));
    lerp = ColorLerp(colA, colB, t);                                              // 0x42D036
    FillPanelBottom(hdc, 3, bottom - 248, side - 3, bottom - 161, lerp, colB, 1); // 0x42D05C

    // Fixed rows from the colour-table pairs (colour.txt).
    FillPanelBottom(hdc, 3, bottom - 157, 215, bottom - 3, app->ThemeColorAt(6),
                    app->ThemeColorAt(7),
                    1); // 0x42D089
    FillPanelBottom(hdc, 3, 3, side - 3, 47, app->ThemeColorAt(4), app->ThemeColorAt(5),
                    1); // 0x42D0B1
    FillPanelBottom(hdc, 218, bottom - 157, 358, bottom - 3, app->ThemeColorAt(8),
                    app->ThemeColorAt(9),
                    1); // 0x42D0E1

    // Block A (0x42D0E6..0x42D179): IK/FK-style width switch; left 0x169,
    // top bottom-157, bottom bottom-3, colours 0xA0600/0xA0604.
    const COLORREF colC = app->ThemeColorAt(10);
    const COLORREF colD = app->ThemeColorAt(11);
    const BottomPanelLayout panelLayout = ComputeBottomPanelLayout(app);
    std::int32_t xOff = panelLayout.leading;
    if (app->CameraMode() != 0) {                                                        // 0x2F8
        if (app->PanelExpanded(UiPanel::Camera) != 0) {                                  // 0x2F9
            FillPanelBottom(hdc, 0x169, bottom - 157, 0x202, bottom - 3, colC, colD, 1); // 0x42D123
        } else {
            FillPanelBottom(hdc, 0x169, bottom - 157, 0x17B, bottom - 3, colC, colD, 1); // 0x42D13B
        }
    } else if (app->PanelExpanded(UiPanel::Bone) != 0) {                             // 0x2FC
        FillPanelBottom(hdc, 0x169, bottom - 157, 0x22E, bottom - 3, colC, colD, 1); // 0x42D15C
    } else {
        FillPanelBottom(hdc, 0x169, bottom - 157, 0x17B, bottom - 3, colC, colD, 1); // 0x42D174
    }

    // Block B (0x42D17E..0x42D218): colours 0xA0608/0xA060C; the offset is
    // subtracted from both edges and optionally grown.
    const COLORREF colE = app->ThemeColorAt(12);
    const COLORREF colF = app->ThemeColorAt(13);
    if (app->CameraMode() != 0) {                      // 0x2F8
        if (app->PanelExpanded(UiPanel::Light) != 0) { // 0x2FA
            FillPanelBottom(hdc, 0x1F3 - xOff, bottom - 157, 0x2A7 - xOff, bottom - 3, colE, colF,
                            1); // 0x42D1C1
        } else {
            FillPanelBottom(hdc, 0x1F3 - xOff, bottom - 157, 0x205 - xOff, bottom - 3, colE, colF,
                            1); // 0x42D1D5
        }
    } else if (app->PanelExpanded(UiPanel::Morph) != 0) { // 0x2FD
        FillPanelBottom(hdc, 0x254 - xOff, bottom - 157, 0x361 - xOff, bottom - 3, colE, colF,
                        1); // 0x42D1FF
    } else {
        FillPanelBottom(hdc, 0x254 - xOff, bottom - 157, 0x266 - xOff, bottom - 3, colE, colF,
                        1); // 0x42D213
    }
    xOff = panelLayout.afterLightOrFace;

    // Block C (0x42D21E..0x42D27E): only when 0x2F8 set; note the swapped
    // colour roles - colour 0xA0628, edge 0xA0604 (push ho=0xA0604 first).
    if (app->CameraMode() != 0) { // 0x2F8
        const COLORREF colG = app->ThemeColorAt(20);
        if (app->PanelExpanded(UiPanel::SelfShadow) != 0) { // 0x2FE
            FillPanelBottom(hdc, 0x2AA - xOff, bottom - 157, 0x35B - xOff, bottom - 3, colG, colD,
                            1); // 0x42D265
        } else {
            FillPanelBottom(hdc, 0x2AA - xOff, bottom - 157, 0x2BC - xOff, bottom - 3, colG, colD,
                            1); // 0x42D279
        }
    }
    xOff = panelLayout.afterSelfShadow;

    // Block D (0x42D284..0x42D2E0): only when 0x2F8 set; colours 0xA0610/4.
    if (app->CameraMode() != 0) { // 0x2F8
        const COLORREF colH = app->ThemeColorAt(14);
        const COLORREF colI = app->ThemeColorAt(15);
        if (app->PanelExpanded(UiPanel::Accessory) != 0) { // 0x2FB
            FillPanelBottom(hdc, 0x35E - xOff, bottom - 157, 0x422 - xOff, bottom - 3, colH, colI,
                            1); // 0x42D2C7
        } else {
            FillPanelBottom(hdc, 0x35E - xOff, bottom - 157, 0x370 - xOff, bottom - 3, colH, colI,
                            1); // 0x42D2DB
        }
    }

    // Final offset (0x42D2E6..0x42D2FA): 0x2F8 set -> -180, else +13.
    const std::int32_t xFinal = panelLayout.final;

    // Three closing rows (0x42D2FA..0x42D38E).
    FillPanelBottom(hdc, 0x371 - xFinal, bottom - 157, 0x412 - xFinal, bottom - 0x52,
                    app->ThemeColorAt(16), app->ThemeColorAt(17),
                    1); // 0x42D330
    FillPanelBottom(hdc, 0x371 - xFinal, bottom - 0x4F, 0x3F3 - xFinal, bottom - 3,
                    app->ThemeColorAt(18), app->ThemeColorAt(19),
                    1); // 0x42D361
    FillPanelBottom(hdc, 0x3F6 - xFinal, bottom - 0x4F, 0x412 - xFinal, bottom - 3,
                    app->ThemeColorAt(18), app->ThemeColorAt(19),
                    1); // 0x42D38E
}

// ===========================================================================
// VA 0x0042C140 - HandlePaletteChanged2  (original: sub_42C140, 0x6C5 bytes)
// ===========================================================================
// Status-overlay painter of the panel top: two gradient background bars, a
// 49x24 "playing" icon blit, and the model/camera status text lines.
// Called with the paint DC from HandleWindowPaint 0x47C0A0 (wm_paint.cpp
// @ 0x47E70A), MicWndProc 0x466A10 (@ 0x466B60 / 0x466CBC) and the WndProc
// 0x4C3A10 message-0x318 branch (wndproc.cpp, wParam as HDC).
//
// Sequence (exact, per the IDA disassembly):
//   Target window: this+0xA0D38 (kDwordA0D38, child panel) when non-zero,
//   else the main window this+0xA06B8 (kPtrHwnd); x base is 0 vs
//   this+0xA06C8 (kDwordSidebar) + 9; `right` = client.right both paths.
//   Two FillPanelBottom gradient bars:
//     (0x969696, 0x646464) at hideTop-25 .. hideTop-1     (0xA0D44 = kDwordHideTop)
//     (0x787878, 0xC8C8C8) at hideBottom .. hideBottom+32 (0xA0D4C = kDwordHideBottom)
//   CreateCompatibleDC(0) + SelectObject of the handle at this+0x2F4 (756,
//   the 49x24 playing-icon bitmap DC) + BitBlt SRCCOPY to
//   (right-58, hideTop-24, 49, 24) + DeleteDC - no restore of the selected
//   object, exactly like the original.
//   Text section (DrawGlyph 0x40E290) branches on this+0x2F8 (kByteOptflag0
//   camera/light vs bone mode) x EnglishUI (0xA0B4C):
//     - camera/light: slot array this+0x9DD70 [byte this+0x9E170] accessory
//       records; name at record+0x238, or the fixed "camera light accessary"
//       / JP "camera/lighting/accessory" literal; formatted via "camera light accessary
//       / %s" / JP "camera/lighting/accessory / %s" into the 256-byte Buffer.
//     - bone: model slots this+0x780 [byte this+0x910]; name at model+0x227A
//       (EN) / model+0x2248 (JP); bone index model+0x2D90; bone record =
//       (model+0x26BC) + sizeof(mikudancestudio::mdl::BoneRecord) *index, name at +0x14 (EN, "%s : %s").  The JP
//       branch keeps the original quirk: sprintf_s with a "%s" format and
//       TWO extra arguments, the second (bone record, no +0x14) is ignored.
//     - trace-mode line pair when
//       byte 0x9ED98 & (slotidx == this+0xA0430) & (this+0xA0430 >= 0);
//       second line (r=0xFF, g=b=0x32) at x+1/y-1 repeats the text.
//   "Playing" is drawn as "Playing"/JP "playing" when (this+0x9ED90 == 0) &
//   (this+0x330 != 0) - the play flag test - else the name/accessory text.
//   Each mode ends with two 14px label lines; EN paths return early, JP
//   paths fall through to the shared "angle" (kJpAngle) tail call.
//
// The "palette" name is historical (from the message-0x318 stub list); the
// function performs no palette API calls - it is the status overlay painter.
// Original returns the LONG of the last DrawGlyph; the declared signature
// is void.
//
// Reference: ../translated/MikuMikuDance/fcn_0042c140.cpp (superseded by
// the IDA disassembly for strings/coordinates)
// =========================================================================//
void HandlePaletteChanged2(HDC hdc) {
    MMDApp* app = g_Block; // original `this` (ecx)

    // Target window and x base (0x42C160..0x42C1A6).
    RECT rc;
    std::int32_t xBase;
    HWND hwnd = app->FloatingWindow();
    if (hwnd != nullptr) {
        GetClientRect(hwnd, &rc);
        xBase = 0;
    } else {
        hwnd = reinterpret_cast<HWND>(app->state.hwnd); // 0xA06B8
        GetClientRect(hwnd, &rc);
        xBase = app->SidebarWidth() + 9;
    }
    const LONG right = rc.right; // ebp
    const LONG blitX = rc.right; // v26 (BitBlt x base)

    const std::int32_t hideTop = app->state.hideTop;       // 0xA0D44
    const std::int32_t hideBottom = app->state.hideBottom; // 0xA0D4C

    FillPanelBottom(hdc, xBase, hideTop - 25, right, hideTop - 1, 0x969696u, 0x646464u,
                    1); // 0x42C1C7
    FillPanelBottom(hdc, xBase, hideBottom, right, hideBottom + 32, 0x787878u, 0xC8C8C8u,
                    1); // 0x42C1E8

    // 49x24 playing-icon blit from the offscreen DC handle at this+0x2F4.
    HDC compat = CreateCompatibleDC(nullptr); // 0x42C1EF
    SelectObject(compat,
                 reinterpret_cast<HGDIOBJ>(app->state.bmpRes119)); // this+0x2F4 (756)
    BitBlt(hdc, blitX - 58, hideTop - 24, 49, 24, compat, 0, 0,
           0xCC0020u); // 0x42C226 (SRCCOPY)
    DeleteDC(compat);  // 0x42C22D

    // Play flag test, recomputed by the original in each branch
    // (this+0x330 loaded once at 0x42C233, this+0x9ED90 per branch; no side
    // effects intervene, so a single hoisted evaluation is taken).
    const bool playing = (app->FrameStepPlayback() == 0) & (app->PlaybackActive() != 0);

    char buffer[256]; // Buffer @ ebp-104h; sprintf_s count 0x100

    if (app->CameraMode() != 0) {    // 0x2F8: camera/light mode
        if (app->EnglishUI() != 0) { // 0xA0B4C
            if (playing) {
                DrawGlyph(app, "Playing", hdc, 20, xBase + 10, hideTop - 22, 0xFF, 0xFF, 0xFF,
                          1); // 0x42C287
            } else {
                // Accessory slot array this+0x9DD70 [byte this+0x9E170].
                mdl::AccessoryRecord* acc = app->AccessorySlot(app->SelectedAccessorySlot());
                if (acc == nullptr) {
                    DrawGlyph(app, "camera light accessary", hdc, 20, xBase + 10, hideTop - 22,
                              0xFF, 0xFF, 0xFF, 1); // 0x42C316
                } else {
                    sprintf_s(buffer, 0x100, "camera light accessary / %s",
                              acc->name); // 0x42C2BC
                    DrawGlyph(app, buffer, hdc, 20, xBase + 10, hideTop - 22, 0xFF, 0xFF, 0xFF,
                              1); // 0x42C2EB
                }
            }
            DrawGlyph(app, "camera", hdc, 14, xBase + 90, hideBottom + 10, 0, 0, 0, 1); // 0x42C33B
            DrawGlyph(app, "angle", hdc, 14, xBase + 365, hideBottom + 10, 0, 0, 0, 1); // 0x42C361
            return;
        }
        if (playing) {
            DrawGlyph(app, kJpPlaying, hdc, 16, xBase + 10, hideTop - 20, 0xFF, 0xFF, 0xFF,
                      1); // 0x42C39A
        } else {
            mdl::AccessoryRecord* acc = app->AccessorySlot(app->SelectedAccessorySlot());
            if (acc == nullptr) {
                DrawGlyph(app, kJpCamLightAccessoryPanel, hdc, 16, xBase + 10, hideTop - 20, 0xFF,
                          0xFF, 0xFF, 1); // 0x42C429
            } else {
                sprintf_s(buffer, 0x100, kJpCamLightAccessoryPanelFmt, acc->name); // 0x42C3CF
                DrawGlyph(app, buffer, hdc, 16, xBase + 10, hideTop - 20, 0xFF, 0xFF, 0xFF,
                          1); // 0x42C3FE
            }
        }
        DrawGlyph(app, kJpCameraCenter, hdc, 14, xBase + 72, hideBottom + 11, 0, 0, 0,
                  1); // 0x42C44C
    } else {          // bone mode
        if (app->EnglishUI() != 0) {
            if (playing) {
                DrawGlyph(app, "Playing", hdc, 20, xBase + 10, hideTop - 22, 0xFF, 0xFF, 0xFF,
                          1); // 0x42C492
            } else {
                unsigned char* model = app->SelectedModel();
                if (model != nullptr) { // 0x42C4AD
                    const mikudancestudio::mdl::ModelRecord& record =
                        *mikudancestudio::mdl::Mdl(model);
                    const std::int32_t boneIdx = record.selectedBone;
                    if (boneIdx < 0) {
                        sprintf_s(buffer, 0x100, "%s", record.nameEn);
                    } else {
                        sprintf_s(buffer, 0x100, "%s : %s", record.nameEn,
                                  record.boneTable[boneIdx].nameEn);
                    }
                    const int width = DrawGlyph(app, buffer, hdc, 20, xBase + 10, hideTop - 22,
                                                0xFF, 0xFF, 0xFF, 1); // 0x42C545
                    // Trace-mode line pair: byte 0x9ED98 & (slot==0xA0430)
                    // & (0xA0430 >= 0).
                    const std::int32_t traceTarget = app->CameraParentModel();
                    const bool traceActive = (app->state.followCameraEnabled != 0) &
                                             (app->SelectedModelSlot() == traceTarget) &
                                             (traceTarget >= 0); // 0x42C553..0x42C572
                    if (traceActive) {
                        if (app->state.viewDirty != 0) { // 0xA05D1
                            DrawGlyph(app, "camera bone trace mode (release trace button)", hdc, 16,
                                      xBase + width + 30, hideTop - 22, 0xFF, 0xFF, 0xFF,
                                      1); // 0x42C5A7
                            DrawGlyph(app, "camera bone trace mode (release trace button)", hdc, 16,
                                      xBase + width + 29, hideTop - 23, 0xFF, 0x32, 0x32,
                                      1); // 0x42C5CE
                        } else {
                            DrawGlyph(app, "camera bone trace mode", hdc, 16, xBase + width + 30,
                                      hideTop - 22, 0xFF, 0xFF, 0xFF, 1); // 0x42C5E9
                        }
                    }
                }
            }
            DrawGlyph(app, "bone place", hdc, 14, xBase + 66, hideBottom + 10, 0, 0, 0,
                      1);                                                               // 0x42C60C
            DrawGlyph(app, "angle", hdc, 14, xBase + 365, hideBottom + 10, 0, 0, 0, 1); // 0x42C361
            return;
        }
        if (playing) {
            DrawGlyph(app, kJpPlaying, hdc, 20, xBase + 10, hideTop - 22, 0xFF, 0xFF, 0xFF,
                      1); // 0x42C645
        } else {
            unsigned char* model = app->SelectedModel();
            if (model != nullptr) { // 0x42C660
                const mikudancestudio::mdl::ModelRecord& record = *mikudancestudio::mdl::Mdl(model);
                sprintf_s(buffer, 0x100, "%s", record.name);
                const int width = DrawGlyph(app, buffer, hdc, 16, xBase + 10, hideTop - 20, 0xFF,
                                            0xFF, 0xFF, 1); // 0x42C6F4
                const std::int32_t traceTarget = app->CameraParentModel();
                const bool traceActive = (app->state.followCameraEnabled != 0) &
                                         (app->SelectedModelSlot() == traceTarget) &
                                         (traceTarget >= 0); // 0x42C6F9..0x42C721
                if (traceActive) {
                    if (app->state.viewDirty != 0) { // 0xA05D1
                        DrawGlyph(app, kJpCameraFollowModeHint, hdc, 16, xBase + width + 30,
                                  hideTop - 20, 0xFF, 0xFF, 0xFF, 1); // 0x42C756
                        DrawGlyph(app, kJpCameraFollowModeHint, hdc, 16, xBase + width + 29,
                                  hideTop - 21, 0xFF, 0x32, 0x32, 1); // 0x42C77D
                    } else {
                        DrawGlyph(app, kJpCameraFollowMode, hdc, 16, xBase + width + 30,
                                  hideTop - 20, 0xFF, 0xFF, 0xFF, 1); // 0x42C798
                    }
                }
            }
        }
        DrawGlyph(app, kJpBonePosition, hdc, 14, xBase + 68, hideBottom + 11, 0, 0, 0,
                  1); // 0x42C7BD
    }

    // Shared tail of the JP paths: "angle" at xBase+0x171 (369).
    DrawGlyph(app, kJpAngle, hdc, 14, xBase + 369, hideBottom + 11, 0, 0, 0, 1); // 0x42C7E3
}

} // namespace mikudancestudio
