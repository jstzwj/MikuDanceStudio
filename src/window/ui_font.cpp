// ===========================================================================
// VA 0x00466D20 phases 21-23 - CreateUiFont  (from sub_466D20)
// ===========================================================================
// Exact port of the font phase inside UI control creation (verified in the
// raw Ghidra listing at lines ~1700-1760 of fcn_00466d20.c):
//
//   SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, 0x154, &metrics, 0)
//   copy lfMessageFont (15 dwords) -> this+656508
//   english = *(byte*)(this+658252)
//   hFont = CreateFontA(
//       english ? 13 : 12,          // height (MS PGothic 12 / Tahoma 13)
//       0, 0, 0, 400,               // FW_NORMAL
//       0, 0, 0,
//       0x80,                       // SHIFTJIS_CHARSET
//       0, 0, 2,                    // PROOF_QUALITY
//       0x31,                       // FIXED_PITCH | FF_MODERN
//       english ? "Tahoma" : "MS PGothic")
//   this+656572 = hFont
//   ... every subsequent control creation issues
//       SendMessageA(ctrl, WM_SETFONT, hFont, TRUE)
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"
namespace mikudancestudio {

void CreateUiFont(MMDApp* app, HWND hwnd) {
    auto& s = *app;

    NONCLIENTMETRICSA metrics; // 0x467A47
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS /*0x29*/, 0x154, &metrics, 0);
    // lfMessageFont copy (15 dwords = 60 bytes) -> this+656508
    std::memcpy(s.state.logFont, &metrics.lfMessageFont, sizeof(LOGFONTA));

    const bool english = s.EnglishUI() != 0; // 658252
    HFONT font = CreateFontA(english ? 13 : 12, 0, 0, 0, 400 /*FW_NORMAL*/, FALSE, FALSE, FALSE,
                             SHIFTJIS_CHARSET /*0x80*/, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             PROOF_QUALITY /*2*/, FIXED_PITCH | FF_MODERN /*0x31*/,
                             english ? "Tahoma" : "MS PGothic"); // 0x52BD18/0x52B7C4
    s.state.hFontUI = font;                                      // 656572
}

void ApplyUiFontToControl(MMDApp* app, HWND control, int id) {
    // Original sends WM_SETFONT immediately after each creation site.  The
    // frame edit and its six navigation buttons retain their class default.
    switch (id) {
    case panel::kCurrentFrameEdit:
    case panel::kPreviousFrameButton:
    case panel::kNextFrameButton:
    case panel::kNextKeyframeButton:
    case panel::kPreviousKeyframeButton:
    case panel::kFirstFrameButton:
    case panel::kLastFrameButton:
        return;
    default:
        break;
    }
    if (control != nullptr)
        SendMessageA(control, WM_SETFONT, reinterpret_cast<WPARAM>(app->state.hFontUI), TRUE);
}

} // namespace mikudancestudio
