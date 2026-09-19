// ===========================================================================
// VA 0x0044BB30 - HandleVScroll  (original: sub_44BB30)
// ===========================================================================
// WM_VSCROLL handler, dispatched from WndProc 0x4C3A10 with lParam = control
// HWND (a2) and wParam = scroll code (a3).  Two controls are recognized:
//
//   control 427 (timeline strip) - wParam low 16 bits (zero-extended,
//   movzx) is the SB_ code; codes 4 and 6+ are ignored (default):
//     0 = SB_LINEUP / 1 = SB_LINEDOWN : delta = -/+ 1
//     2 = SB_PAGEUP / 3 = SB_PAGEDOWN : delta = -/+ this+2388 (page step)
//     5 = SB_THUMBTRACK               : delta = HIWORD(wParam) - this+2392
//   The adjusted field depends on this+760 (0x2F8, kByteOptflag0):
//     set   -> this+645704 (0x9DA48), clamped to [0, this+645708-2]
//     clear -> current model (slot ptr this+1920[this+2320]) +12716,
//              clamped to [0, model+12712-2]
//   Every path ends with PostLanguageSweep (0x42F1E0) exactly once.
//
//   control 534 (alpha slider) - TBM_GETPOS (0x400) read; 100-pos stored to
//   this+672804 (0xA4424) and passed to SetFrameNormalized (0x4C2B80).
//
// Reference: ../translated/MikuMikuDance/fcn_0044bb30.cpp
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// Forward declaration for a function ported in this wave whose body lives
// in ui_timeline_gfx.cpp (not yet added to ported_funcs.hpp; declared here
// with its original VA).
void SetFrameNormalized(int frame);   // VA 0x004C2B80

void HandleVScroll(LPARAM lParam, WPARAM wParam) {
    MMDApp* app = g_Block;
    HWND hwnd = static_cast<HWND>(app->Hwnd());
    const HWND ctrl = reinterpret_cast<HWND>(lParam);          // a2: control HWND
    const std::uint32_t code = LOWORD(wParam);                 // a3: SB_ code

    if (ctrl == GetDlgItem(hwnd, panel::kTimelineVScroll)) {
        if (app->state.optflag[0] != 0) {
            // mode 0x2F8 set: operate on the app timeline frame counter
            switch (code) {
            case 0:  // SB_LINEUP
                --app->DisplayObjectListScrollPosition();
                break;
            case 1:  // SB_LINEDOWN
                ++app->DisplayObjectListScrollPosition();
                break;
            case 2:  // SB_PAGEUP
                app->DisplayObjectListScrollPosition() -=
                    app->state.scrollNPage;
                break;
            case 3:  // SB_PAGEDOWN
                app->DisplayObjectListScrollPosition() +=
                    app->state.scrollNPage;
                break;
            case 5:  // SB_THUMBTRACK
                app->DisplayObjectListScrollPosition() +=
                    static_cast<std::int32_t>(HIWORD(wParam)) -
                    app->state.scrollNPos;
                break;
            default:
                break;
            }
            if (app->DisplayObjectListScrollPosition() < 0)
                app->DisplayObjectListScrollPosition() = 0;
            const int upper = app->DisplayObjectListMatchCount() - 2;
            if (app->DisplayObjectListScrollPosition() > upper)
                app->DisplayObjectListScrollPosition() = upper;
        } else {
            unsigned char* model = app->SelectedModel();
            mikudancestudio::mdl::ModelRecord& record = *mikudancestudio::mdl::Mdl(model);
            switch (code) {
            case 0:  // SB_LINEUP
                --record.boneListPos;
                break;
            case 1:  // SB_LINEDOWN
                ++record.boneListPos;
                break;
            case 2:  // SB_PAGEUP
                record.boneListPos -=
                    app->state.scrollNPage;
                break;
            case 3:  // SB_PAGEDOWN
                record.boneListPos +=
                    app->state.scrollNPage;
                break;
            case 5:  // SB_THUMBTRACK
                record.boneListPos +=
                    static_cast<std::int32_t>(HIWORD(wParam)) -
                    app->state.scrollNPos;
                break;
            default:
                break;
            }
            if (record.boneListPos < 0)
                record.boneListPos = 0;
            if (record.boneListPos > record.boneListRows - 2)
                record.boneListPos = record.boneListRows - 2;
        }
        PostLanguageSweep(app);                                   // 0x42F1E0
        return;
    }

    if (ctrl == GetDlgItem(hwnd, panel::kFrameVolumeSlider)) {
        HWND slider = GetDlgItem(hwnd, panel::kFrameVolumeSlider);
        LRESULT pos = SendMessageA(slider, TBM_GETPOS, 0, 0);
        app->FrameNormalization() = 100 - static_cast<int>(pos);
        SetFrameNormalized(100 - static_cast<int>(pos));          // 0x4C2B80
    }
}

// ===========================================================================
// VA 0x0044BD70 - HandleMouseWheel  (original: sub_44BD70)
// ===========================================================================
// WM_MOUSEWHEEL handler.  delta is the signed 16-bit wheel notch (the
// original signature is __int16; the value is movsx-extended from the
// wParam HIWORD).
//
// While playing (this+4 <= this+657096): drives the timeline strip
// (control 427) with three line-steps per notch - delta <= 0 sends
// SB_LINEDOWN (1) three times, delta > 0 sends SB_LINEUP (0) three times.
// The original re-fetches GetDlgItem(.., 427) before every call (the
// handle is identical each time).
//
// Otherwise the wheel scales a float by delta * 0.05 - x86 runs
// fild/fmul dbl_52D738/fadd/fstp dword (double multiply, one rounding at
// the float store); the x64 twin sub_7FF7CB45F300 is pure SSE single
// precision (movsx/movd/cvtdq2ps, mulss against the dword slot
// 0x7FF7CB552CB0 whose image value IS 0x3D4CCCCD = 0.05f, addss the
// field, movss back) - the port follows the x64 form:
//   camera mode (this+760 set AND this+656432 >= 0) -> this+828 (0x33C)
//   morph-follow special branch (this+760 == 0 && this+650648 &&
//       this+816 == 0 && this+656432 == this+2320 && this+656432 >= 0):
//       this+656849 = 1; this+672812 = 0; PostLanguageSweep2 (0x40D070);
//       this+672812 = 1; this+656850 = 1; then PostViewRefresh (0x40D130)
//       and return
//   otherwise -> this+657628 (0xA08DC)
//   camera-row switch (x64 0x7FF7CB45F36F / 0x7FF7CB45F434, on both the
//       distance and the angle branch): when the camera track flag
//       this+656356 (0xA03E4) is already set the tail reduces to
//       PostViewRefresh (0x40D130) only; otherwise a DWORD store sets
//       656356 = 1 and clears the light / self-shadow / gravity flags
//       656357..656359, the rowSelected byte (x86 +0x4AC / x64 +0x4BC)
//       of all 255 display-object slots is cleared, and the tail runs
//       PostLanguageSweep (0x42F1E0) + PostViewRefresh (0x40D130).  The
//       x64 original never calls RefreshRequest from this handler.
//
// Reference: ../translated/MikuMikuDance/fcn_0044bd70.cpp
// =========================================================================//
void HandleMouseWheel(int delta) {
    MMDApp* app = g_Block;
    HWND hwnd = static_cast<HWND>(app->Hwnd());
    const std::int16_t wheel = static_cast<std::int16_t>(delta);  // original __int16

    if (app->MouseX() <=
        app->SidebarWidth()) {
        // playing: three line-steps on the timeline strip per notch
        if (wheel <= 0) {
            HandleVScroll(reinterpret_cast<LPARAM>(GetDlgItem(hwnd, panel::kTimelineVScroll)), 1);
            HandleVScroll(reinterpret_cast<LPARAM>(GetDlgItem(hwnd, panel::kTimelineVScroll)), 1);
            HandleVScroll(reinterpret_cast<LPARAM>(GetDlgItem(hwnd, panel::kTimelineVScroll)), 1);
        } else {
            HandleVScroll(reinterpret_cast<LPARAM>(GetDlgItem(hwnd, panel::kTimelineVScroll)), 0);
            HandleVScroll(reinterpret_cast<LPARAM>(GetDlgItem(hwnd, panel::kTimelineVScroll)), 0);
            HandleVScroll(reinterpret_cast<LPARAM>(GetDlgItem(hwnd, panel::kTimelineVScroll)), 0);
        }
        return;
    }

    const std::int32_t axis = app->CameraParentModel();
    const std::uint8_t mode = app->state.optflag[0];  // 760

    if ((mode & (axis >= 0 ? 1u : 0u)) != 0) {
        // camera distance: this+828 (float) += wheel * 0.05
        // (x64 0x7FF7CB45F340: cvtdq2ps/mulss 0.05f/addss/movss)
        app->CameraPosition()[2] =
            static_cast<float>(wheel) * 0.05f + app->CameraPosition()[2];
        if (app->GlobalTrackSelected(GlobalTimelineTrack::Camera) == 0) {
            // switch the edit selection to the camera row (x64 0x7FF7CB45F36F:
            // DWORD store - camera 656356 = 1, light / self-shadow / gravity
            // 656357..656359 = 0), drop every accessory rowSelected mark
            // (byte +0x4AC x86 / +0x4BC x64, 255 slots, 0x7FF7CB45F379) and
            // sweep the label column.
            app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Camera);
            for (int slot = 0; slot < 0xFF; ++slot) {
                mdl::AccessoryRecord* accessory = app->AccessorySlot(slot);
                if (accessory != nullptr)
                    accessory->rowSelected = 0;
            }
            PostLanguageSweep(app);                             // 0x42F1E0
        }
    } else {
        const bool morphFollow =
            mode == 0 &&
            app->state.followCameraEnabled != 0 &&
            app->PlaybackActive() == 0 &&
            axis == static_cast<int>(app->SelectedModelSlot()) &&
            axis >= 0;
        if (morphFollow) {
            app->state.viewDirty = 1;                       // 656849 (0xA05D1)
            app->WindowLayoutReady() = 0;
            PostLanguageSweep2(app);                              // 0x40D070
            app->WindowLayoutReady() = 1;
            app->state.b6568482 = 1;                       // 656850 (0xA05D2)
            PostViewRefresh(app);                                 // 0x40D130
            return;
        }
        // view angle: this+657628 (float) += wheel * 0.05
        // (x64 0x7FF7CB45F410: same single-precision chain)
        app->CameraDistance() =
            static_cast<float>(wheel) * 0.05f + app->CameraDistance();
        if (app->GlobalTrackSelected(GlobalTimelineTrack::Camera) == 0) {
            // same camera-row switch on the angle branch (x64 DWORD store
            // 0x7FF7CB45F434, slot loop 0x7FF7CB45F43E)
            app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Camera);
            for (int slot = 0; slot < 0xFF; ++slot) {
                mdl::AccessoryRecord* accessory = app->AccessorySlot(slot);
                if (accessory != nullptr)
                    accessory->rowSelected = 0;
            }
            PostLanguageSweep(app);                             // 0x42F1E0
        }
    }
    PostViewRefresh(app);     // 0x40D130 (x64 sub_7FF7CB440DD0)
}

}  // namespace mikudancestudio
