// ===========================================================================
// VA 0x004C2A00 - TimelineDrawTicks  (original: sub_4C2A00)
// ===========================================================================
// Timeline tick/scale drawing over the frame strip (GDI): pens/brushes +
// MoveTo/LineTo/Rectangle; called from WM_HSCROLL (0x44AEE0 @ 0x44BABB),
// the editor-click path (0x446A70 @ 0x44A788), the wave-open path
// (0x418500 @ 0x4186E4) and CommandDispatch (0x47E8A0 @ 0x486340) after
// the frame offset changes.
//
// Original signature: BOOL __thiscall sub_4C2A00(HDC* this, int x, char* ho)
//   ECX (this) = app+0xCC  -> the 0x25C wave/timeline subsystem object
//                (app+0xCC audioContext; ctor 0x4C2450 zeroes +0x00/+0x04).
//   x  = frame offset - callers pass app+2428 (current frame, kDword97C).
//   ho = timeline strip width - callers pass app+657096 (sidebar width,
//        kDwordSidebar).  The project placeholder signature types this
//        parameter as HGDIOBJ and names it hdc; it actually carries the
//        WIDTH value.  The real HDC is read from sub+0x08 below.
//
// GDI object order (exact, 1:1 with the binary):
//   1. CreatePen(PS_SOLID,1,0xFFFFFF) white + CreateSolidBrush(0xFFFFFF);
//      SelectObject both (saving the DC's old pen/brush in v9/v11);
//      Rectangle(hdc, 0, 0, width-10, 50)      -- white strip background;
//      SelectObject(oldPen); DeleteObject(whitePen).
//   2. CreatePen(PS_SOLID,1,0xFF0000) red; SelectObject (result discarded);
//      for x in [0, width-10): sample cursor v4 = 13*frame - 101 + x;
//      if 0 <= v4 < sub+0x250 draw the vertical bar
//      MoveToEx(hdc, x, (signed char)bufA[v4]) -> LineTo(hdc, x, (signed
//      char)bufB[v4]); the original carries the column index in two
//      registers (xa/v6) so both endpoints always share column x;
//      SelectObject(oldPen); DeleteObject(redPen).
//   3. CreatePen(PS_SOLID,1,0) black; SelectObject;
//      MoveToEx(hdc, 0, 25) -> LineTo(hdc, width-10, 25)  -- baseline;
//      SelectObject(oldPen); SelectObject(oldBrush);
//      DeleteObject(blackPen); DeleteObject(whiteBrush)
//      (the original returns this final DeleteObject's BOOL).
//
// Reference: ../translated/MikuMikuDance/fcn_004c2a00.cpp
//   NOTE: the translated file deviates from the binary in places (pen
//   colours/order, Rectangle bounds, loop body); this port follows the
//   IDA decompilation of MikuMikuDance.exe 0x4C2A00.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

void TimelineDrawTicks(int frameOffset, int width) {
    // param mapping (original __thiscall sub_4C2A00(HDC* this, int x, char* ho)):
    //   this -> g_Block->state.audioContext    (0x025C wave/timeline subsystem)
    //   x    -> frameOffset (current frame)
    //   ho   -> strip width (sidebar width)
    MMDApp* app = g_Block;
    WaveAudioContext* audio = app->Audio();

    int sampleCursor = 13 * frameOffset - 101;  // 13 samples per frame, -101 offset
    int xMax = width - 10;

    HDC strip = audio->timelineDC;
    char* bufA = reinterpret_cast<char*>(audio->waveformMax);
    char* bufB = reinterpret_cast<char*>(audio->waveformMin);

    // --- phase 1: white background fill ------------------------------------
    HPEN whitePen = CreatePen(PS_SOLID, 1, 0xFFFFFF);       // PS_SOLID=0, width 1, white
    HBRUSH whiteBrush = CreateSolidBrush(0xFFFFFF);
    HGDIOBJ oldPen = SelectObject(strip, whitePen);         // save DC's old pen
    HGDIOBJ oldBrush = SelectObject(strip, whiteBrush);     // save DC's old brush
    Rectangle(strip, 0, 0, xMax, 50);                       // (0,0)-(width-10,50)
    SelectObject(strip, oldPen);
    DeleteObject(whitePen);

    // --- phase 2: red per-column bars (loop) --------------------------------
    HPEN redPen = CreatePen(PS_SOLID, 1, 0xFF0000);         // red
    SelectObject(strip, redPen);                            // result discarded (old pen still in oldPen)
    int xa = 0;                                             // xa = column from previous iteration (v9-register twin)
    int columnX = 0;                                        // current column (eax)
    for (xa = 0; columnX < xMax; xa = columnX) {            // loop bound columnX < width-10
        if (sampleCursor >= 0 && sampleCursor < audio->waveformColumns) {
            // vertical bar at column columnX (xa == columnX here): y from bufA[sampleCursor] to bufB[sampleCursor]
            MoveToEx(strip, columnX, bufA[sampleCursor], nullptr);   // y = (signed char)bufA[sampleCursor]
            LineTo(strip, xa, bufB[sampleCursor]);           // y = (signed char)bufB[sampleCursor]
            columnX = xa;
        }
        ++columnX;
        ++sampleCursor;
    }
    SelectObject(strip, oldPen);
    DeleteObject(redPen);

    // --- phase 3: black baseline at y = 25 ----------------------------------
    HPEN blackPen = CreatePen(PS_SOLID, 1, 0);              // black
    SelectObject(strip, blackPen);
    MoveToEx(strip, 0, 25, nullptr);
    LineTo(strip, xMax, 25);                                // horizontal baseline, x = width-10
    SelectObject(strip, oldPen);
    SelectObject(strip, oldBrush);
    DeleteObject(blackPen);
    DeleteObject(whiteBrush);                               // original returns this BOOL
}

// ===========================================================================
// VA 0x004C2B80 - SetFrameNormalized  (original: sub_4C2B80)
// ===========================================================================
// Frame-number -> normalized position conversion (log10 path) for the
// wave/timeline strip; called from the alpha slider (0x44BB30 @ 0x44BD29),
// frame-set paths (0x446A70 @ 0x44A7F1, 0x44AAA0 @ 0x44AE65, 0x430F20,
// 0x4312E0), the wave-open path (0x418500 @ 0x418736), the frame driver
// (0x46B090 @ 0x46F36C) and CommandDispatch (0x47E8A0 @ 0x4876B3).
//
// Original signature: int __thiscall sub_4C2B80(_DWORD* this, int a2)
//   ECX (this) = app+0xCC (the 0x25C wave/timeline subsystem).
//   a2         = caller-supplied position in 1/100 units (callers pass
//                app+672804, kFrameNormalizationSlot; the placeholder name is
//                "frame").
//
// Formula (exact):
//   v5 = (float)((double)a2 / 100.0)          -- fild/fdiv/fstp float store
//   if v5 < 1.0:
//       if v5 > 0.0:
//           v6 = (float)log10(v5)             -- __CIlog10, stored as float
//           sub+0x258 = (int)(v6 * 33.20000076293945 * 100.0)
//                     -- 33.2f as double (0x40409999A0000000) * 100.0,
//                        truncated via __ftol2_sse
//       else:
//           sub+0x258 = -10000                -- 0xFFFFD8F0
//   else:
//       sub+0x258 = 0
//   every path then calls vtable slot +0x3C (index 15 = SetFrequency, not
//   SetVolume which is slot 17) of the playback object at sub+0x14,
//   __stdcall(obj, sub+0x258), and returns its result;
//   if sub+0x14 == 0 the original returns undefined garbage (port returns).
//
// Reference: ../translated/MikuMikuDance/fcn_004c2b80.cpp
//   NOTE: the translated file misidentifies the divisor (dbl_52B8E0 is
//   100.0, not an FPS value) and the flow; this port follows the IDA
//   decompilation of MikuMikuDance.exe 0x4C2B80.
// =========================================================================//
void SetFrameNormalized(int frame) {
    // param mapping (original __thiscall sub_4C2B80(_DWORD* this, int a2)):
    //   this -> g_Block->state.audioContext    (0x025C wave/timeline subsystem)
    //   a2   -> frame (position in 1/100 units)
    MMDApp* app = g_Block;
    WaveAudioContext* audio = app->Audio();

    IDirectSoundBuffer* player = audio->streamingBuffer;
    if (player == nullptr)                                  // original returns undefined eax here
        return;

    // seconds = (float)((double)frame / 100.0)   (float store/reload like fstp/fld)
    float seconds = static_cast<float>(static_cast<double>(frame) / 100.0);

    if (seconds < 1.0f) {
        if (seconds > 0.0f) {
            // log10 path: log10Val = (float)log10(seconds), then trunc(log10Val * 33.2f * 100.0)
            float log10Val = static_cast<float>(std::log10(static_cast<double>(seconds)));
            audio->volume =
                static_cast<int>(static_cast<double>(log10Val) *
                                 33.20000076293945 *          // (double)33.2f = 0x40409999A0000000
                                 100.0);
            // vtable slot +0x3C x86 / +0x78 x64 (index 15) of the playback
            // object, __stdcall(obj, dword).  x64 twin 0x7FF7CB4FB260 makes
            // one such call at 0x7FF7CB4FB2DC (call [rax+0x78]) for all
            // three branches: slot 15 = SetFrequency, NOT SetVolume (slot
            // 17) - the original's mis-slotted invalid no-op (its WAV
            // volume never took effect), replicated verbatim below and in
            // both sibling branches; DWORD cast = mov edx bit semantics.
            player->SetFrequency(static_cast<DWORD>(audio->volume));
        } else {
            audio->volume = -10000;                         // 0xFFFFD8F0
            player->SetFrequency(static_cast<DWORD>(audio->volume)); // slot 15 no-op, see above
        }
    } else {
        audio->volume = 0;                                   // position >= 1.0
        player->SetFrequency(static_cast<DWORD>(audio->volume)); // slot 15 no-op, see above
    }
}

}  // namespace mikudancestudio
