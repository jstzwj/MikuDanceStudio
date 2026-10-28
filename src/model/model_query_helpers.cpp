// Model queries, display-bone navigation, accessory navigation and key polling.
// Model display groups and their bone/morph entries are runtime objects;
// their indices and collapsed state are distinct from physics rigid bodies.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <io.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

// VA 0x0040E3D0 - single-key edge detector, inlined into PollKeyboardStates below
// (its only caller).  State cells hold 0=up, 1=just pressed, 3=held,
// 2=just released; any 0->1 / active->2 transition latches app+658796=1.
inline void KeyEdgeScan(MMDApp* app, int vk, std::uint32_t* cell) {
    if ((GetKeyState(vk) & 0x80) == 0x80) { // 0x40E3E0 key down
        if (*cell != 0u) {
            *cell = 3; // 0x40E401 held repeat
        } else {
            *cell = 1;                  // 0x40E3ED press edge
            app->state.messageSeen = 1; // 0x40E3F3
        }
    } else if (*cell == 1u || *cell == 3u) {
        *cell = 2;                  // 0x40E425 release edge
        app->state.messageSeen = 1; // 0x40E42B
    } else {
        *cell = 0; // 0x40E41B stayed up
    }
}

} // namespace

// ===========================================================================
// VA 0x0041A1A0 - ReadBeWord  (__stdcall, no `this`)
// ===========================================================================
// Reads `nbytes` single bytes from `fh` and accumulates them big-endian
// (v = byte + (v << 8), 0x41A1C9) into *outVal; nbytes <= 0 just stores 0.
// Returns the last _read() result (or the outVal pointer in the empty case,
// mirroring the original - no caller uses the value).
// =========================================================================//
int ReadBeWord(int fh, int* outVal, int nbytes) {
    std::int32_t acc = 0;
    int result;
    if (nbytes <= 0) {                                                       // 0x41A1AA
        result = static_cast<int>(reinterpret_cast<std::uintptr_t>(outVal)); // original returns a2
        *outVal = 0;                                                         // 0x41A1E1
    } else {
        int byteBuf = 0;
        result = 0;
        do {
            result = _read(fh, &byteBuf, 1);                       // 0x41A1B9
            acc = static_cast<std::uint8_t>(byteBuf) + (acc << 8); // 0x41A1C9
            --nbytes;
        } while (nbytes != 0);
        *outVal = acc; // 0x41A1D6
    }
    return result;
}

// ===========================================================================
// VA 0x0041A1F0 - ReadFixedString  (__stdcall, no `this`)
// ===========================================================================
// Reads `len` bytes one at a time into a 1000-byte scratch buffer,
// NUL-terminates and sprintf_s("%s")-copies into `out` (capacity 0x3E8).
// (The original has no bounds check between len and the 1000-byte scratch;
// ported 1:1.)  Returns the sprintf_s result.
// =========================================================================//
int ReadFixedString(int fh, char* out, int len) {
    char byteBuf = 0;
    char scratch[1000]; // v6 @0x41A1F0
    int i = 0;
    for (; i < len; ++i) {
        _read(fh, &byteBuf, 1); // 0x41A22B
        scratch[i] = byteBuf;   // 0x41A234
    }
    scratch[i] = 0;                               // 0x41A253
    return sprintf_s(out, 0x3E8u, "%s", scratch); // 0x41A267
}

// ===========================================================================
// VA 0x0042D3A0 - PollKeyboardStates  (__thiscall, this = MMDApp)
// ===========================================================================
// Frame-driver key polling: 63 edge scans pairing virtual keys with the
// dword state cells at app+20..+196 (decompile's this[N] dword indices).
// Several cells are fed by two keys (menu accelerator + letter twin); the
// tables below cover every original 0x42D3AA..0x42D6B6 pairing, regrouped
// into named cells / letter twins / numpad twins - observationally
// identical because each scan writes only its own cell plus the
// idempotent messageSeen latch.  Called once per frame from 0x46FF02.
// =========================================================================//
void PollKeyboardStates(MMDApp* app) {
    struct KeyCell {
        int vk;
        std::int32_t MMDAppState::* cell;
    };
    static constexpr KeyCell kNamedCells[] = {
        // 0x42D3AA..0x42D428 and the 0x42D5D6..0x42D6B6 non-numpad entries
        {VK_UP, &MMDAppState::upKeyState},
        {VK_DOWN, &MMDAppState::downKeyState},
        {VK_LEFT, &MMDAppState::leftKeyState},
        {VK_RIGHT, &MMDAppState::rightKeyState},
        {VK_SHIFT, &MMDAppState::shiftModifierState},
        {VK_SPACE, &MMDAppState::spaceKeyState},
        {VK_CONTROL, &MMDAppState::ctrlModifierState},
        {VK_DELETE, &MMDAppState::deleteKeyState},
        {VK_ESCAPE, &MMDAppState::escKeyState},
        {VK_TAB, &MMDAppState::tabKeyState},
        {VK_LBUTTON, &MMDAppState::leftMouseButtonState},
        {VK_RBUTTON, &MMDAppState::rightMouseButtonState},
        {VK_MBUTTON, &MMDAppState::middleMouseButtonState},
        {VK_RETURN, &MMDAppState::enterKeyState},
        {VK_MENU, &MMDAppState::menuKeyState},
        {221, &MMDAppState::keyState221},
        {226, &MMDAppState::keyState226},
    };
    for (const KeyCell& e : kNamedCells)
        KeyEdgeScan(app, e.vk, reinterpret_cast<std::uint32_t*>(&(app->state.*e.cell)));

    // 0x42D435..0x42D525 (two keys per cell): letter hotkeys share the
    // dialog re-entry guard ints (MMDAppState::letterKeyState, cells
    // app+48..+116); lowercase/uppercase VK pairs fold onto one slot.
    // Pairings verified against both binaries' poll tables: 'g' lands on
    // slot 7 (x86 0x42D4CE writes 'g' to app+0x4C) and 's' on slot 8
    // (0x42D4E5 writes 's' to app+0x50); the pump's frame-seek /
    // shadow / fine-shadow three-way branch (x86 0x4726B2) gates on the
    // 'G' cell.  'h' (slot 10) is scanned before 'i' (slot 9).
    // frame_modes.cpp kLetterKeys carries the same pairing.
    static constexpr struct {
        int key;
        LetterKey letterKey;
    } kLetterKeys[] = {
        {'x', LetterKey::X}, {'X', LetterKey::X}, {'z', LetterKey::Z}, {'Z', LetterKey::Z},
        {'c', LetterKey::C}, {'C', LetterKey::C}, {'v', LetterKey::V}, {'V', LetterKey::V},
        {'d', LetterKey::D}, {'D', LetterKey::D}, {'a', LetterKey::A}, {'A', LetterKey::A},
        {'b', LetterKey::B}, {'B', LetterKey::B}, {'g', LetterKey::G}, {'G', LetterKey::G},
        {'s', LetterKey::S}, {'S', LetterKey::S}, {'h', LetterKey::H}, {'H', LetterKey::H},
        {'i', LetterKey::I}, {'I', LetterKey::I}, {'k', LetterKey::K}, {'K', LetterKey::K},
        {'p', LetterKey::P}, {'P', LetterKey::P}, {'u', LetterKey::U}, {'U', LetterKey::U},
        {'j', LetterKey::J}, {'J', LetterKey::J}, {'f', LetterKey::F}, {'F', LetterKey::F},
        {'r', LetterKey::R}, {'R', LetterKey::R}, {'l', LetterKey::L}, {'L', LetterKey::L},
    };
    for (const auto& e : kLetterKeys)
        KeyEdgeScan(app, e.key,
                    reinterpret_cast<std::uint32_t*>(&app->LetterKeyState(e.letterKey)));

    // 0x42D5D6..0x42D6B6 tail: VK_NUMPAD0..9 twin scans -> app+144..+180
    for (int i = 0; i < 10; ++i)
        KeyEdgeScan(app, VK_NUMPAD0 + i,
                    reinterpret_cast<std::uint32_t*>(&app->state.numpadKeyState[i]));
}

// Scroll a displayed bone into view (x86 0x41A280, x64 RVA 0x717E0).
// Count expanded morph rows and display bone entries, then send individual
// WM_VSCROLL line messages. Collapsed groups and the pinned root do not scroll.
void ScrollDisplayBoneIntoView(MMDApp* app, int boneIdx) {
    mdl::ModelRecord* model = app->SelectedModel();                                   // 0x41A28E
    if (model->displayRootBone == static_cast<std::uint32_t>(boneIdx) || boneIdx < 0) // 0x41A2BB
        return;

    int rows = -1;                                                             // v5
    const unsigned groupCount = static_cast<unsigned char>(model->groupCount); // 0x41A2D4
    if (groupCount <= 1u)
        return;
    const mdl::DisplayGroup* const groups = model->displayGroups;

    for (unsigned k = 1; k < groupCount; ++k) { // v17 / v4
        ++rows;                                 // 0x41A2FC
        if (groups[k].flags == 0)               // 0x41A2FF
            continue;
        const unsigned char sep = model->facialFrameCount; // v7
        if (sep != 0 && k == 2) {                          // 0x41A31A
            rows += sep;                                   // 0x41A32D
            continue;
        }
        const int n = static_cast<int>(model->displayBoneCount);
        if (n == 0)
            continue;
        const mdl::FrameGroup* const displayBones = model->displayBones;
        for (int j = 0; j < n; ++j) { // 0x41A34C
            if (displayBones[j].groupIndex != k)
                continue;
            ++rows;                                     // 0x41A351
            if (displayBones[j].targetIndex != boneIdx) // 0x41A358
                continue;
            // hit: scroll the bone list so `rows` is in view
            const int delta = rows - model->boneListPos; // 0x41A390
            HWND hwnd = app->MainWindow();
            if (delta >= 0) {
                RECT rc;
                GetClientRect(hwnd, &rc);                             // 0x41A3EA
                const int slack = (rc.bottom - 408) / 14 - delta - 2; // 0x41A40F
                if (slack < 0) {
                    int cnt2 = 2 - ((rc.bottom - 408) / 14 - delta);
                    for (; cnt2 != 0; --cnt2) // 0x41A452
                        SendMessageA(
                            hwnd, WM_VSCROLL, SB_LINEDOWN,
                            reinterpret_cast<LPARAM>(GetDlgItem(hwnd, panel::kTimelineVScroll)));
                }
            } else {
                int cnt2 = -delta;
                for (; cnt2 != 0; --cnt2) // 0x41A3D2
                    SendMessageA(
                        hwnd, WM_VSCROLL, SB_LINEUP,
                        reinterpret_cast<LPARAM>(GetDlgItem(hwnd, panel::kTimelineVScroll)));
            }
            return; // 0x41A381
        }
    }
}

// Select one display bone, auto-scroll, rebuild labels and invalidate readouts.
// This changes the selection bitmap, not pose edit flags or undo history.
void SelectDisplayBone(MMDApp* app, int boneIdx) {
    mdl::ModelRecord* model = app->SelectedModel();
    model->selectedBone = boneIdx; // 0x438CE5
    const int cap = static_cast<int>(model->boneCount);
    unsigned char* flags = model->boneSelection;
    for (int i = 0; i < cap; ++i) // 0x438D01
        flags[i] = 0;
    flags[boneIdx] = 1;                      // 0x438D3A
    ScrollDisplayBoneIntoView(app, boneIdx); // 0x438D41
    RebuildTimelineTrackPanel(app);          // 0x438D48
    InvalidateSelectionReadouts(app);        // 0x438D54
}

// Previous display bone (x86 0x438D60, x64 RVA 0x712E0).
// Within an expanded group, choose the preceding entry. Otherwise choose the
// last entry in the preceding expanded group; groups 0..2 lead to the root.
// An empty expanded group keeps the current selection, as in the original.
void SelectPreviousDisplayBone(MMDApp* app) {
    mdl::ModelRecord* model = app->SelectedModel();
    const int cur = model->selectedBone; // 0x438D71
    if (cur < 0) {
        SelectDisplayBone(app, model->displayRootBone); // 0x438D92
        return;
    }
    const int rootBone = model->displayRootBone;
    if (rootBone == cur)
        return; // 0x438D8F
    const int n = static_cast<int>(model->displayBoneCount);
    if (n == 0)
        return;
    const mdl::FrameGroup* const displayBones = model->displayBones;
    const mdl::DisplayGroup* const groups = model->displayGroups;

    for (int i = 0; i < n; ++i) { // 0x438DEB
        if (displayBones[i].targetIndex != cur)
            continue;                       // 0x438DF0
        int g = displayBones[i].groupIndex; // 0x438E12

        if (groups[g].flags != 0 && i >= 1) { // 0x438E1C
            // Previous entry in the same expanded group (scan downwards)
            for (int j = i - 1; j >= 0; --j) { // 0x438E84
                if (displayBones[j].groupIndex == g) {
                    SelectDisplayBone(app, displayBones[j].targetIndex); // 0x438F36
                    return;
                }
            }
        }

        // LABEL_24: highest expanded display group strictly below g
        if (g > 0) {
            for (--g; g > 0 && groups[g].flags == 0; --g) { // 0x438ECD
            }
        }
        if (g <= 2) {
            SelectDisplayBone(app, rootBone); // 0x438F28
            return;
        }
        // Last entry in that group (scan downwards from n-1)
        for (int j = n - 1; j >= 0; --j) { // 0x438EE4
            if (displayBones[j].groupIndex == g) {
                SelectDisplayBone(app, displayBones[j].targetIndex); // 0x438EED
                return;
            }
        }
        // not found -> next matching display entry (LABEL_34)
    }
}

// Next display bone (x86 0x438F50, x64 RVA 0x71540).
// From the root, choose the first entry of an expanded group. Otherwise choose
// the next entry of this group, then later expanded groups, skipping empty ones.
void SelectNextDisplayBone(MMDApp* app) {
    mdl::ModelRecord* model = app->SelectedModel();
    const int cur = model->selectedBone; // 0x438F64
    if (cur < 0) {
        SelectDisplayBone(app, model->displayRootBone); // 0x438F8C
        return;
    }
    const mdl::FrameGroup* const displayBones = model->displayBones;
    const mdl::DisplayGroup* const groups = model->displayGroups;
    const int n = static_cast<int>(model->displayBoneCount);
    const int lastGroup = static_cast<unsigned char>(model->groupCount) - 1; // v17

    if (cur == model->displayRootBone) {       // 0x438F96
        for (int g = 1; g <= lastGroup; ++g) { // 0x438FE8
            if (groups[g].flags == 0)
                continue;
            for (int j = 0; j < n; ++j) {
                if (displayBones[j].groupIndex == g) {
                    SelectDisplayBone(app, displayBones[j].targetIndex); // 0x43904E
                    return;
                }
            }
        }
    }
    if (n == 0) // 0x438FFE
        return;

    for (int i = 0; i < n; ++i) { // 0x439058
        if (displayBones[i].targetIndex != cur)
            continue;                             // 0x43905F
        const int g = displayBones[i].groupIndex; // 0x439081

        if (groups[g].flags != 0 && i + 1 < n) {
            // Next entry in the same expanded group (scan upwards)
            for (int j = i + 1; j < n; ++j) { // 0x439105
                if (displayBones[j].groupIndex == g) {
                    SelectDisplayBone(app, displayBones[j].targetIndex);
                    return;
                }
            }
        }

        // next expanded display group above g, then its first entry; exits to
        // the next matching display entry once g reaches the last entry (LABEL_41)
        if (g < lastGroup) {
            int g2 = g;
            for (;;) { // 0x4390B0
                ++g2;
                if (groups[g2].flags != 0) {
                    for (int j = 0; j < n; ++j) {
                        if (displayBones[j].groupIndex == g2) {
                            SelectDisplayBone(app, displayBones[j].targetIndex);
                            return;
                        }
                    }
                }
                if (g2 >= lastGroup) // LABEL_27/40
                    break;
            }
        }
    }
}

// ===========================================================================
// x86 0x0041A460 / x64 0x7FF7CB491E40 - ScrollAccessoryTimelineRowIntoView
// ===========================================================================
// Scroll the accessory's timeline row into view, using its display order,
// the accessory list position and the (clientHeight-452)/14 row budget.
// The original returned an unused LRESULT.
// =========================================================================//
namespace {

void ScrollAccessoryTimelineRowIntoView(MMDApp* app, unsigned char slotIdx) {
    const auto* accessory = app->AccessorySlot(slotIdx); // 0x41A46B
    if (accessory == nullptr)
        return;
    const int listPos = app->DisplayObjectListScrollPosition();
    const int delta = accessory->order - listPos; // 0x41A484
    HWND hwnd = app->MainWindow();
    if (delta >= 0) {
        RECT rc;
        GetClientRect(hwnd, &rc);                             // 0x41A4DE
        const int slack = (rc.bottom - 452) / 14 - delta - 2; // 0x41A503
        if (slack < 0) {
            int cnt = 2 - ((rc.bottom - 452) / 14 - delta);
            for (; cnt != 0; --cnt) // 0x41A542
                SendMessageA(hwnd, WM_VSCROLL, SB_LINEDOWN,
                             reinterpret_cast<LPARAM>(GetDlgItem(hwnd, panel::kTimelineVScroll)));
        }
    } else {
        int cnt = listPos - accessory->order;
        for (; cnt != 0; --cnt) // 0x41A4C6
            SendMessageA(hwnd, WM_VSCROLL, SB_LINEUP,
                         reinterpret_cast<LPARAM>(GetDlgItem(hwnd, panel::kTimelineVScroll)));
    }
}

inline void ClearAccessoryTimelineRows(MMDApp* app) {
    for (int slot = 0; slot < mdl::kAccessorySlotCount; ++slot) {
        auto* accessory = app->AccessorySlot(slot);
        if (accessory != nullptr)
            accessory->rowSelected = 0; // 0x4391EF
    }
}

// Select the accessory row and slot, update its combo/edit controls, scroll
// the row into view, then refresh the timeline and selection readouts.
inline void SelectAccessoryEditTarget(MMDApp* app, int idx) { // 0x43974F
    auto* accessory = app->AccessorySlot(idx);
    accessory->rowSelected = 1;
    HWND hwnd = app->MainWindow();
    app->SelectedObjectSlot() = static_cast<unsigned char>(idx); // 0x43975D
    const WPARAM wp = accessory->order;                          // 0x439771
    SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo), CB_SETCURSEL, wp, 0);
    SyncAccessoryEditPanel(app); // 0x43978C
    ScrollAccessoryTimelineRowIntoView(app, app->SelectedObjectSlot());
    RebuildTimelineTrackPanel(app); // 0x4397A2
    InvalidateSelectionReadouts(app);
}

} // namespace

// ===========================================================================
// x86 0x4391D0 / x64 0x7FF7CB4919D0 - SelectPrevEditTarget
// ===========================================================================
// Previous target: accessories in display order, gravity, self-shadow,
// light, camera (stays at camera). Each branch clears accessory row selection.
// Missing current accessory resets to camera/slot zero; a missing adjacent
// order leaves rows clear. AccessoryRecord pins order/rowSelected at
// x86 +1181/+1196 and x64 +1197/+1212; these are not visibility/shadow flags.
// =========================================================================//
void SelectPrevEditTarget(MMDApp* app) {
    if (app->GlobalTrackSelected(GlobalTimelineTrack::Camera) != 0) {
        ClearAccessoryTimelineRows(app);
        app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Camera);
        RebuildTimelineTrackPanel(app); // 0x439244
        InvalidateSelectionReadouts(app);
        return;
    }
    if (app->GlobalTrackSelected(GlobalTimelineTrack::Light) != 0) {
        ClearAccessoryTimelineRows(app);
        app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Camera);
        RebuildTimelineTrackPanel(app);
        InvalidateSelectionReadouts(app);
        return;
    }
    if (app->GlobalTrackSelected(GlobalTimelineTrack::SelfShadow) != 0) {
        ClearAccessoryTimelineRows(app);
        app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Light);
        RebuildTimelineTrackPanel(app);
        InvalidateSelectionReadouts(app);
        return;
    }
    if (app->GlobalTrackSelected(GlobalTimelineTrack::Gravity) != 0) {
        ClearAccessoryTimelineRows(app);
        app->SelectGlobalTimelineTrack(GlobalTimelineTrack::SelfShadow);
        RebuildTimelineTrackPanel(app);
        InvalidateSelectionReadouts(app);
        return;
    }

    // No global track selected: previous accessory by display order.
    ClearAccessoryTimelineRows(app);
    const unsigned char curSlot = app->SelectedObjectSlot(); // 0x4393FB
    app->ClearGlobalTimelineTrackSelection();
    const auto* current = app->AccessorySlot(curSlot);
    if (current == nullptr) { // 0x439425
        app->SelectedObjectSlot() = 0;
        app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Camera);
        RebuildTimelineTrackPanel(app); // 0x43950D
        InvalidateSelectionReadouts(app);
        return;
    }
    const unsigned char order = current->order; // 0x43942D
    if (order != 0) {
        const int target = order - 1;
        int i = 0;
        while (app->AccessorySlot(i) == nullptr ||
               app->AccessorySlot(i)->order != target) { // 0x43945F
            ++i;
            if (i >= mdl::kAccessorySlotCount) {
                RebuildTimelineTrackPanel(app); // 0x439486
                InvalidateSelectionReadouts(app);
                return;
            }
        }
        SelectAccessoryEditTarget(app, i); // 0x43949C..
        return;
    }
    app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Gravity);
    ScrollAccessoryTimelineRowIntoView(app, curSlot); // 0x439441
    RebuildTimelineTrackPanel(app);                   // 0x439448
    InvalidateSelectionReadouts(app);
}

// ===========================================================================
// x86 0x439520 / x64 0x7FF7CB491BF0 - SelectNextEditTarget
// ===========================================================================
// Next target: camera, light, self-shadow, gravity, then accessories in
// display order. From gravity, find order zero across all slots. With no
// adjacent order, restore the current row without updating the combo.
// =========================================================================//
void SelectNextEditTarget(MMDApp* app) {
    if (app->GlobalTrackSelected(GlobalTimelineTrack::Camera) != 0) {
        ClearAccessoryTimelineRows(app);
        app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Light);
        RebuildTimelineTrackPanel(app); // 0x4395A3
        InvalidateSelectionReadouts(app);
        return;
    }
    if (app->GlobalTrackSelected(GlobalTimelineTrack::Light) != 0) {
        ClearAccessoryTimelineRows(app);
        app->SelectGlobalTimelineTrack(GlobalTimelineTrack::SelfShadow);
        RebuildTimelineTrackPanel(app); // 0x439623
        InvalidateSelectionReadouts(app);
        return;
    }
    if (app->GlobalTrackSelected(GlobalTimelineTrack::SelfShadow) != 0) {
        ClearAccessoryTimelineRows(app);
        app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Gravity);
        RebuildTimelineTrackPanel(app); // 0x4396A3
        InvalidateSelectionReadouts(app);
        return;
    }

    if (app->GlobalTrackSelected(GlobalTimelineTrack::Gravity) != 0) {
        ClearAccessoryTimelineRows(app);
        app->ClearGlobalTimelineTrackSelection();
        // First accessory in display order; its physical slot may be nonzero.
        int i = 0;
        while (app->AccessorySlot(i) == nullptr || app->AccessorySlot(i)->order != 0) { // 0x439734
            ++i;
            if (i >= mdl::kAccessorySlotCount) {
                app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Gravity);
                RebuildTimelineTrackPanel(app); // 0x4396A3
                InvalidateSelectionReadouts(app);
                return;
            }
        }
        SelectAccessoryEditTarget(app, i); // 0x43974F..
        return;
    }

    // No global track selected: next accessory by display order.
    ClearAccessoryTimelineRows(app);
    const unsigned char curSlot = app->SelectedObjectSlot(); // 0x4397F8 tail
    auto* current = app->AccessorySlot(curSlot);             // v30 @0x439824
    app->ClearGlobalTimelineTrackSelection();
    if (current == nullptr) { // 0x439822
        app->SelectedObjectSlot() = 0;
        app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Camera);
        RebuildTimelineTrackPanel(app); // 0x439891
        InvalidateSelectionReadouts(app);
        return;
    }
    // The original promotes to int before adding: order 255 must not wrap.
    const int target = current->order + 1; // 0x43982D
    int i = 0;
    while (app->AccessorySlot(i) == nullptr || app->AccessorySlot(i)->order != target) { // 0x439832
        ++i;
        if (i >= mdl::kAccessorySlotCount) {
            current->rowSelected = 1; // 0x43985A
            ScrollAccessoryTimelineRowIntoView(app, curSlot);
            RebuildTimelineTrackPanel(app); // 0x439872
            InvalidateSelectionReadouts(app);
            return;
        }
    }
    SelectAccessoryEditTarget(app, i);
}

} // namespace mikudancestudio
