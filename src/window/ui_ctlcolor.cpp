// ===========================================================================
// VA 0x0040E0E0 - HandleCtlColor  (original: sub_40E0E0)
// ===========================================================================
// WM_CTLCOLORSTATIC handler (sole caller: WndProc 0x004C3A10).
//
// Original signature:  LRESULT __fastcall sub_40E0E0(MMDApp* this /*ecx*/,
// HWND ctrlHwnd /*stack arg 0 = WM_CTLCOLORSTATIC lParam*/, HDC hdc
// /*stack arg 1 = wParam - never read*/);  `this` is the g_Block global
// (the ported signature has no app parameter).
//
// The control handle (lParam) is compared against GetDlgItem(mainHwnd, id)
// for the 11 themed control ids below, in this exact order.  On a match the
// corresponding gradient-brush slot is returned; no match returns 0.
// mainHwnd = this+0xA06B8 (657080).  The 11 HBRUSH slots are the array
// created by the 0x47A5B0 init at 656588 (0xA04CC); slot i = 656588 + 4*i:
//
//     id       id(hex)    slot   app offset
//     447      0x1BF        9     0xA04F0 (656624)
//     534      0x216       10     0xA04F4 (656628)
//     455      0x1C7        0     0xA04CC (656588)
//     456      0x1C8        1     0xA04D0 (656592)
//     457      0x1C9        2     0xA04D4 (656596)
//     458      0x1CA        3     0xA04D8 (656600)
//     459      0x1CB        4     0xA04DC (656604)
//     460      0x1CC        5     0xA04E0 (656608)
//     515|510  0x203/0x1FE  6     0xA04E4 (656612)
//     520|505  0x208/0x1F9  7     0xA04E8 (656616)
//     560      0x230        8     0xA04EC (656620)
//
// The two paired checks (515|510 and 520|505) are NOT short-circuited in
// the original: both GetDlgItem calls execute unconditionally (setz/or
// pattern at 0x40E1DF and 0x40E21D), so the port precomputes both results
// before combining them.
//
// Reference: ../translated/MikuMikuDance/fcn_0040e0e0.cpp
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdlib>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
// Not yet declared in ported_funcs.hpp - external-linkage forward
// declarations so the calls below resolve to the real definitions
// (ui_panel_paint.cpp / coordinator-provided stub); the coordinator
// registers them centrally in the finishing phase.
void PanelPaint(MMDApp* app);      // VA 0x00414610
void SelectionReeval(MMDApp* app); // VA 0x00430510

LRESULT HandleCtlColor(HWND control, HDC dc) {
    // The original does not use the device context.
    MMDApp* app = g_Block;
    (void)dc;
    const HWND ctrl = control;
    const HWND main = reinterpret_cast<HWND>(app->state.hwnd); // 657080
    const auto brushResult = [app](int index) {
        return static_cast<LRESULT>(reinterpret_cast<std::intptr_t>(app->UiBrush(index)));
    };

    if (ctrl == GetDlgItem(main, panel::kFovSlider)) // 0x1BF -> slot 9 (0xA04F0)
        return brushResult(9);

    if (ctrl == GetDlgItem(main, panel::kFrameVolumeSlider)) // 0x216 -> slot 10 (0xA04F4)
        return brushResult(10);

    if (ctrl == GetDlgItem(main, panel::kLightColorSliderR)) // 0x1C7 -> slot 0 (0xA04CC)
        return brushResult(0);

    if (ctrl == GetDlgItem(main, panel::kLightColorSliderG)) // 0x1C8 -> slot 1 (0xA04D0)
        return brushResult(1);

    if (ctrl == GetDlgItem(main, panel::kLightColorSliderB)) // 0x1C9 -> slot 2 (0xA04D4)
        return brushResult(2);

    if (ctrl == GetDlgItem(main, panel::kLightDirSliderX)) // 0x1CA -> slot 3 (0xA04D8)
        return brushResult(3);

    if (ctrl == GetDlgItem(main, panel::kLightDirSliderY)) // 0x1CB -> slot 4 (0xA04DC)
        return brushResult(4);

    if (ctrl == GetDlgItem(main, panel::kLightDirSliderZ)) // 0x1CC -> slot 5 (0xA04E0)
        return brushResult(5);

    // Pair 515|510 (0x203/0x1FE) -> slot 6 (0xA04E4): both GetDlgItem calls
    // run unconditionally in the original (setz/or, no short-circuit).
    const bool hit515 = ctrl == GetDlgItem(main, panel::kMorphSlider2);
    const bool hit510 = ctrl == GetDlgItem(main, panel::kMorphSlider1);
    if (hit515 || hit510)
        return brushResult(6);

    // Pair 520|505 (0x208/0x1F9) -> slot 7 (0xA04E8): same non-short-circuit
    // pattern as the pair above.
    const bool hit520 = ctrl == GetDlgItem(main, panel::kMorphSlider3);
    const bool hit505 = ctrl == GetDlgItem(main, panel::kMorphSlider0);
    if (hit520 || hit505)
        return brushResult(7);

    if (ctrl == GetDlgItem(main, panel::kSelfShadowRangeSlider)) // 0x230 -> slot 8 (0xA04EC)
        return brushResult(8);

    return 0;
}

// ===========================================================================
// VA 0x0044A9A0 - HandleLButtonUp  (original: sub_44A9A0)
// ===========================================================================
// WM_LBUTTONUP handler (sole caller: WndProc 0x004C3A10, after
// ReleaseCapture).
//
// Unconditionally:  this+0xC8 (200) = 0 (drag flag) and this+0xA442C
// (672812) = 1 (paint gate).  The drag-active flag this+0xA0189 (655753)
// is read BEFORE those two stores (cmp at 0x44A9A6, stores at 0x44A9AC /
// 0x44A9B2, branch on the entry value).  When it is non-zero the seven
// drag buffers are released in this exact order - free-if-non-null, then
// the slot is nulled (asm 0x44A9BF..0x44AA68, CRT free thunk 0x509154):
//
//     0xA0410 (656400)   0xA0418 (656408)   0xA03F0 (656368)
//     0xA03F8 (656376)   0xA0428 (656424)   0xA0400 (656384)
//     0xA0420 (656416)
//
// then this+0xA0189 = 0, this+0xA03EB (656363) = 0, PanelPaint (0x414610)
// and SelectionReeval (0x430510).  Finally this+0x9DA09 (645641) = 0 runs
// on every path (drag active or not).
//
// Reference: ../translated/MikuMikuDance/fcn_0044a9a0.cpp
// =========================================================================//
void HandleLButtonUp(MMDApp* app) {
    const bool dragActive = app->SelectionBoxDragging() != 0;
    app->SidebarResizeDragging() = 0;
    app->WindowLayoutReady() = 1;

    if (dragActive) {
        app->ReleaseTimelineSelectionRecords();

        app->SelectionBoxDragging() = 0;
        app->TimelineSelectionChanged() = 0;
        PanelPaint(app);      // 0x414610
        SelectionReeval(app); // 0x430510
    }

    app->PendingTimelineSelectionRow() = TimelineSelectionRow::None;
}

} // namespace mikudancestudio
