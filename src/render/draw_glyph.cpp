// ===========================================================================
// VA 0x0040E290 - DrawGlyph  (original: sub_40E290, 0x13D bytes)
// ===========================================================================
// Exact port.  __thiscall(MMDApp* this, LPCSTR text, HDC hdc, int size,
//                        int x, int y, u8 r, u8 g, u8 b, int bold)
//   LOGFONT on a 0x3C GlobalAlloc block:
//     face    = english(this+658252) ? "Arial" : "MS PGothic" (0x52B7C4)
//     height  = -size, weight = bold ? 700 : 400, charset 0x80 (SJIS)
//   CreateFontIndirectA -> select, SetBkMode(TRANSPARENT), SetTextColor,
//   TextOutA, GetTextExtentPoint32A, restore, DeleteObject, GlobalFree.
//   Returns the text extent cx (consumed by callers for layout advance).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

int DrawGlyph(MMDApp* app, const char* text, HDC hdc, int size, int x, int y, unsigned char r,
              unsigned char g, unsigned char b, int bold) {
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE /*0x40*/, 0x3C);
    auto* lf = static_cast<LOGFONTA*>(GlobalLock(mem));
    const bool english = app->EnglishUI() != 0; // 658252
    strcpy_s(lf->lfFaceName, 0x20, english ? "Arial" : "MS PGothic");
    lf->lfWidth = 0;
    lf->lfEscapement = 0;
    lf->lfOrientation = 0;
    lf->lfItalic = FALSE;
    lf->lfUnderline = FALSE;
    lf->lfStrikeOut = FALSE;
    lf->lfCharSet = SHIFTJIS_CHARSET /*0x80*/;
    lf->lfWeight = bold == 1 ? 700 : 400;
    lf->lfHeight = -size;

    HFONT font = CreateFontIndirectA(lf);
    HGDIOBJ prev = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT /*1*/);
    SetTextColor(hdc, RGB(r, g, b)); // a7|a8<<8|a9<<16
    const int len = static_cast<int>(strlen(text));
    TextOutA(hdc, x, y, text, len);
    SIZE extent;
    GetTextExtentPoint32A(hdc, text, len, &extent);
    SelectObject(hdc, prev);
    DeleteObject(font);
    GlobalUnlock(mem);
    GlobalFree(mem);
    return extent.cx;
}

} // namespace mikudancestudio
