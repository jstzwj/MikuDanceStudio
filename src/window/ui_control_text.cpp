// ===========================================================================
// VA 0x0041A550 - DrawControlText  (original: sub_41A550, 0x100 bytes)
// ===========================================================================
// Bold single-line control label drawing used by HandleNotify (0x4398B0):
//   GetWindowTextA(hwnd, buf[0x100], 0x100) -> SetBkMode(hdc, TRANSPARENT)
//   -> GlobalAlloc(GPTR, 0x3C) zeroed LOGFONTA block; the GPTR zero-init is
//   load-bearing (lfOrientation/lfStrikeOut/lfOutPrecision/lfClipPrecision/
//   lfQuality/lfPitchAndFamily stay 0) -> face and height picked by the
//   EnglishUI flag (this+658252 = 0xA0B4C, MMDApp::EnglishUI()):
//       EN: strcpy_s "Tahoma"              lfHeight = 13 (0x0D)
//       JP: strcpy_s "MS P Gothic" (SJIS)  lfHeight = 12 (0x0C)
//   -> lfWeight = 700 (FW_BOLD), lfEscapement/lfWidth/lfItalic/lfUnderline
//   = 0, lfCharSet = 0x80 (SHIFTJIS_CHARSET) -> CreateFontIndirectA ->
//   SelectObject (old saved) -> DrawTextA(hdc, text, -1, &rc,
//   DT_CENTER|DT_VCENTER|DT_SINGLELINE = 0x25) -> SelectObject(old) ->
//   DeleteObject(font) -> GlobalFree(lf).
// Reference: ../translated/MikuMikuDance/fcn_0041a550.cpp
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

void DrawControlText(MMDApp* app, HWND hwnd, HDC hdc, RECT rc) {
    char text[0x100];
    GetWindowTextA(hwnd, text, 0x100);
    SetBkMode(hdc, TRANSPARENT); // push 1

    // Zero-initialized LOGFONTA on the global heap (GPTR | sizeof(LOGFONTA));
    // fields the original never touches rely on that zero-init.
    LOGFONTA* lf = static_cast<LOGFONTA*>(GlobalAlloc(GPTR, sizeof(LOGFONTA)));
    if (app->EnglishUI() != 0) { // this+0xA0B4C
        strcpy_s(lf->lfFaceName, sizeof(lf->lfFaceName), "Tahoma");
        lf->lfHeight = 13; // 0x0D
    } else {
        // "MS P Gothic" as the original Shift-JIS bytes (0x52B7C4).
        strcpy_s(lf->lfFaceName, sizeof(lf->lfFaceName),
                 "\x82\x6c\x82\x72 \x82\x6f\x83\x53\x83\x56\x83\x62\x83\x4e");
        lf->lfHeight = 12; // 0x0C
    }
    lf->lfWeight = 700; // FW_BOLD (0x2BC)
    lf->lfEscapement = 0;
    lf->lfWidth = 0;
    lf->lfItalic = FALSE;
    lf->lfUnderline = FALSE;
    lf->lfCharSet = 0x80; // SHIFTJIS_CHARSET

    HFONT font = CreateFontIndirectA(lf);
    HGDIOBJ oldFont = SelectObject(hdc, font);
    DrawTextA(hdc, text, -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE); // 0x25
    SelectObject(hdc, oldFont);
    DeleteObject(font);
    GlobalFree(lf);
}

// ===========================================================================
// VA 0x00429770 - HandleTimer100  (original: sub_429770, 23 bytes)
// ===========================================================================
// WM_TIMER id 100 handler.  The caller (MainWndProc 0x4C3A10) has already
// KillTimer'd id 100; this closes the streaming data-file sub-object stored
// at this+204 (0xCC, the 0x25C obj -> sub_4C2680 CloseDataFile) and then
// sets this+656054 (0xA02B6) = 1.
// Reference: ../translated/MikuMikuDance/fcn_00429770.cpp
// =========================================================================//

void HandleTimer100(MMDApp* app) {
    CloseDataFile(app->Audio()); // this+204 (0xCC)
    app->AudioSeekReady() = 1;   // this+656054
}

} // namespace mikudancestudio
