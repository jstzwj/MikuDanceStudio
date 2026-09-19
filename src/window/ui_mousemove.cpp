// ===========================================================================
// VA 0x00444CC0 - HandleMouseMove  (original: sub_444CC0)
// ===========================================================================
// WM_MOUSEMOVE handler for the editor panel (~0x1DA6 bytes, 441 basic
// blocks; called from WndProc 0x4C3A10, case 512).  Original signature:
//   HCURSOR __thiscall sub_444CC0(int this, unsigned __int16 X, unsigned
//   __int16 Y)
// The caller pushes lParam (mouse X in the low word) then lParam >> 16
// (mouse Y), i.e. X = lParam & 0xFFFF and Y = mouseY here.  The HCURSOR
// return value is discarded by the wndproc, so the port is void, but the
// early returns are preserved because they gate the hide-rect flag clears.
//
// Flow (original instruction ranges):
//   1. 0x444CC6  three hide-rect gates (bytes this+0xA06B4/0xA06B5/0xA06B6):
//      when the mouse sits inside the stored rect this+0xA0D40..0xA0D4C the
//      gate flag is cleared so later moves can re-enter; otherwise the
//      handler returns with the flag still set.
//   2. 0x444D5B  on a real move: store X/Y into this+4/this+8 (with the
//      0xEA60 -> X-0x10000 wrap), maintain the >50px jump flag
//      (byte this+0xA05D3, prev this+0xC/0x10, gate byte this+0x9F12C),
//      GetClientRect, then cursor switching:
//        IDC_SIZEWE (0x7F84): while the pointer hugs the sidebar edge
//          (sidebar <= X <= sidebar+6 && Y <= bottom-158 && 0xA0D38 == 0)
//        IDC_HAND   (0x7F89): while interaction mode (dword this+0x344) == 1
//   3. 0x444E54  sidebar drag (byte this+0xC8): sidebar this+0xA06C8 =
//      clamp(X, 250, clientRight-5), ratio float this+0xA4428, then
//      RelayoutSidebarControls + PanelPaint and three local InvalidateRect passes
//      (right strip / timeline strip / top strip).
//   4. 0x444F46  if byte this+0xA03EB != 0: hover row = (X - 0xA018C - 6)
//      / 13, byte this+0xA0B0D = 1, then the drag-frame remap over the
//      hovered row's list(s): display mode (optflag[0] @0x2F8 != 0) walks
//      the four fixed band lists (84/40/24/36-byte records via this+0x374/
//      0x378/0x37C/0x380) and the accessory lists (this+0x384, 60-byte
//      records) driven by the 16-byte offset records at this+0xA03EC..
//      0xA0410, then PanelPaint, ReloadModels, RefreshLightPanel, RefreshSelfShadowPanel,
//      ApplyGravityTrack, ApplyAccessoryTrack x255 and SyncAccessoryEditPanel; bone-edit mode walks the
//      model bone/morph/IK lists (28/20/60-byte records, offset records at
//      this+0xA0414..0xA0428) then PanelPaint + SeekModelFrame.
//   5. 0x44606F  else, if byte this+0xA0189 != 0: hover-highlight pass over
//      the five fixed-band regions (window-y bands 160..174 / 174..188 /
//      188..202 / 202..216, i.e. the four fixed bands + the accessory
//      grid; row pitch 13 starting at x = 94, column pitch 14 starting at
//      y = 209).  The visibility flags of the band records are first
//      cleared for all 200 rows (0x4461CE display / 0x44653B bone-edit,
//      gated on mode word this+0x24 != 3) and then re-set for the hovered
//      rows to (this+0xC0 != 3); the hover rect lands in
//      this+0xA05C0..0xA05CC.  Ends with PanelPaint and the rect zeroed.
//   6. 0x446A4C  if byte this+0x9DA09 != 0: 0x416280.
//
// The magic-number divisions (/13 = 0x4EC4EC4F<<2, /14 = 0x92492493<<3 with
// add) were verified numerically against the original instruction stream -
// both are plain C truncating division, so ordinary `/` is used.
//
// Reference: ../translated/MikuMikuDance/fcn_00444cc0.cpp (machine
//            translation, incomplete; IDA IDB is authoritative)
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/timeline_selection_grid.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"

namespace mikudancestudio {

// VA 0x00414610 - ported (ui_panel_paint.cpp).
void PanelPaint(MMDApp* app);

// Not-yet-ported refresh helpers (src/app/late_ports.cpp).
void SyncAccessoryEditPanel(MMDApp* a);              // 0x4134E0
void RefreshLightPanel(MMDApp* a);              // 0x411070
void ApplyGravityTrack(MMDApp* a);              // 0x412330
void RefreshSelfShadowPanel(MMDApp* a);              // 0x411B90
void DragInterpolationControlPoint(MMDApp* a);  // 0x416280
void ReloadModels(MMDApp* a);              // 0x42E640
void RelayoutSidebarControls(MMDApp* a);              // 0x442EB0
int SeekModelFrame(unsigned char* model, int frame, int physicsMode);  // 0x4B4260
void ApplyAccessoryTrack(MMDApp* a, int idx);     // 0x413120

namespace {

// --- deferred: the accessory hit grid stays offset-addressed -------
// (x64 xlate only carries its first 800 element entries)

// --- layout constants -------------------------------------------------------
constexpr int kRowPitch = 13;     // 0x0D  hover row pitch
constexpr int kColPitch = 14;     // 0x0E  hover column pitch (bands)
constexpr int kGridX0 = 94;       // 0x5E  first hover row x
constexpr int kColY0 = 209;       // 0xD1  first hover column y (display)
constexpr int kColY0Bone = 153;   // 0x99  first hover column y (bone-edit)
constexpr int kSidebarMin = 250;  // 0xFA  sidebar drag minimum


// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

unsigned char* CurrentModel(MMDApp* app) {
    return app->SelectedModel();
}

// Frame-remap walk over 16-byte offset records {idx, parent, slot, frame}
// referenced by the band lists (display mode 0x444F8C..0x445833), or by the
// model bone/morph/IK lists (bone-edit mode 0x445889..0x446040).  Every
// record that references list entry `idx` rewrites the entry's frame so the
// dragged frames land on the hovered row:
//   row == 0: frame = rec.frame                       (direct copy)
//   row <  0: parent chain via record field +4        (order-preserving)
//   row >  0: reverse walk, parent chain via field +8 (clamped, and the
//             entry frame feeds the this+0x9E16C / model+0x31B0 maxima)
// `limit` >= 0 selects the `idx >= limit` validity test (morph/IK), -1 the
// `idx > 0` test (bands/bone).  `maxModel` is model+0x31B0 for the model
// lists, nullptr for the band lists.
template <typename Key>
void RemapList(MMDApp* app, const int* recs, int count, Key* keys,
               int row, int limit, std::uint32_t* maxModel) {
    auto valid = [limit](int idx) {
        return limit >= 0 ? idx >= limit : idx > 0;
    };
    if (row == 0) {
        for (int i = 0; i < count; ++i) {
            const int* rec = recs + 4 * i;
            if (valid(rec[0]))
                keys[rec[0]].frame = rec[3];
        }
    } else if (row < 0) {
        for (int i = 0; i < count; ++i) {
            const int* rec = recs + 4 * i;
            const int idx = rec[0];
            if (valid(idx)) {
                Key& key = keys[idx];
                const std::int32_t pf = static_cast<std::int32_t>(
                    keys[key.previous].frame);
                if (pf + 1 < row + rec[3]) {
                    const std::int32_t v = row + rec[3];
                    if (v > 0)
                        key.frame = v;
                } else {
                    key.frame = pf + 1;
                }
            }
        }
    } else {
        for (int i = count - 1; i >= 0; --i) {
            const int* rec = recs + 4 * i;
            const int idx = rec[0];
            if (valid(idx)) {
                Key& key = keys[idx];
                if (key.next == 0) {
                    key.frame = row + rec[3];
                } else {
                    const std::int32_t nextFrame =
                        static_cast<std::int32_t>(keys[key.next].frame);
                    if (nextFrame - 1 > row + rec[3])
                        key.frame = row + rec[3];
                    else
                        key.frame = nextFrame - 1;
                }
                const std::uint32_t frame = key.frame;
                if (maxModel != nullptr) {
                    if (*maxModel < frame)
                        *maxModel = frame;
                    if (app->LastRegisteredFrame() < *maxModel)
                        app->LastRegisteredFrame() = *maxModel;
                } else if (app->LastRegisteredFrame() < frame) {
                    app->LastRegisteredFrame() = frame;
                }
            }
        }
    }
}

// Accessory-list variant of RemapList (0x445621..0x445833): the offset
// records carry the index at +4 and the accessory slot at +8; the list base
// is this+0x384[slot] and the records are 60 bytes.
void RemapAccList(MMDApp* app, const int* recs, int count, int row) {
    auto listOf = [app](int slot) {
        return app->AccessoryKeys(slot);
    };
    if (row == 0) {
        for (int i = 0; i < count; ++i) {
            const int* rec = recs + 4 * i;
            const int idx = rec[1];
            if (idx > 0)
                listOf(rec[2])[idx].frame = rec[3];
        }
    } else if (row < 0) {
        for (int i = 0; i < count; ++i) {
            const int* rec = recs + 4 * i;
            const int idx = rec[1];
            if (idx > 0) {
                mdl::AccessoryKey* list = listOf(rec[2]);
                mdl::AccessoryKey& key = list[idx];
                const std::int32_t pf = static_cast<std::int32_t>(
                    list[key.previous].frame);
                if (pf + 1 < row + rec[3]) {
                    const std::int32_t v = row + rec[3];
                    if (v > 0)
                        key.frame = v;
                } else {
                    key.frame = pf + 1;
                }
            }
        }
    } else {
        for (int i = count - 1; i >= 0; --i) {
            const int* rec = recs + 4 * i;
            const int idx = rec[1];
            if (idx > 0) {
                mdl::AccessoryKey* list = listOf(rec[2]);
                mdl::AccessoryKey& key = list[idx];
                if (key.next == 0) {
                    key.frame = row + rec[3];
                } else {
                    const std::int32_t nextFrame =
                        static_cast<std::int32_t>(list[key.next].frame);
                    if (nextFrame - 1 > row + rec[3])
                        key.frame = row + rec[3];
                    else
                        key.frame = nextFrame - 1;
                }
                const std::uint32_t frame = key.frame;
                if (app->LastRegisteredFrame() < frame)
                    app->LastRegisteredFrame() = frame;
            }
        }
    }
}

// Write the hover flag value (Ctrl not active) into a band record flag byte.
inline unsigned char HoverFlag(MMDApp* app) {
    return static_cast<unsigned char>(!app->CtrlModifierActive());
}

void SetAccessorySelected(MMDApp* app, int slot, int keyIndex,
                          std::uint8_t selected) {
    auto* keys = app->AccessoryKeys(slot);
    keys[keyIndex].selected = selected;
}

}  // namespace

// ===========================================================================
// WM_MOUSEMOVE handler (see header comment for the flow map).
// ===========================================================================
void HandleMouseMove(std::uint32_t lParam, int mouseY) {
    MMDApp* app = g_Block;
    const int X = static_cast<int>(lParam & 0xFFFFu);  // a2 (low word)
    const int Y = mouseY;                              // a3 (high word)

    // --- 1. hide-rect gates (0x444CC6) -------------------------------------
    if (app->state.a06B5 != 0) {
        D3DRenderer* sub = app->Renderer();
        const int ratio = static_cast<int>(
            sub->viewScale * 200.0);  // viewScale = sub+0x1D4F0 float ratio
        if (X > static_cast<int>(app->state.hideLeft) - ratio)
            return;
        app->state.a06B5 = 0;
    }
    if (app->state.a06B4 != 0) {
        if (Y > app->state.hideBottom)
            return;
        app->state.a06B4 = 0;
    }
    if (app->state.a06B6 != 0) {
        if (X > app->state.hideLeft ||
            X < app->state.hideRight)
            return;
        // Original stores (X < 0xA0D40), which is false on this path (0x444D55).
        app->state.a06B6 = 0;
    }

    // --- 2. position store + cursor switching (0x444D5B) -------------------
    if (app->MouseY() != Y || app->MouseX() != X) {
        app->MouseX() = X;
        app->MouseY() = Y;
        if (X > 0xEA60)
            app->MouseX() = X - 0x10000;
        if (Y > 0xEA60)
            app->MouseY() = Y - 0x10000;
        if (app->state.separateWindowMouseSeen != 0) {
            const int x0 = app->MouseX();
            const int y0 = app->MouseY();
            if (std::abs(app->PreviousMouseX() - x0) > 50 ||
                std::abs(app->PreviousMouseY() - y0) > 50)
                app->state.mouseJumped = 1;
            app->PreviousMouseX() = x0;
            app->PreviousMouseY() = y0;
            app->state.separateWindowMouseSeen = 0;
        }
        RECT rc;
        GetClientRect(static_cast<HWND>(app->Hwnd()), &rc);
        if ((app->SidebarWidth() <=
                 app->MouseX() && app->MouseY() <= rc.bottom - 158 &&
             app->MouseX() <=
                 app->SidebarWidth() + 6) &
            (app->FloatingWindow() == nullptr)) {
            SetCursor(LoadCursorA(nullptr,
                                  reinterpret_cast<LPCSTR>(
                                      static_cast<INT_PTR>(0x7F84))));  // IDC_SIZEWE
        }

        // --- 3. sidebar drag (0x444E54) ------------------------------------
        if (app->SidebarResizeDragging() != 0) {
            const int x = app->MouseX();
            app->SidebarWidth() = x;
            if (x < kSidebarMin)
                app->SidebarWidth() = kSidebarMin;
            if (rc.right - 5 < app->SidebarWidth())
                app->SidebarWidth() = rc.right - 5;
            app->SidebarRatio() = static_cast<float>(
                static_cast<double>(app->SidebarWidth()) /
                static_cast<double>(rc.right));
            RelayoutSidebarControls(app);
            PanelPaint(app);
            const int sidebar = app->SidebarWidth();
            HWND hwnd = static_cast<HWND>(app->Hwnd());
            rc.bottom -= 158;
            rc.left = sidebar - 19;
            InvalidateRect(hwnd, &rc, FALSE);
            rc.right = sidebar;
            rc.left = 0;
            rc.top = rc.bottom - 90;
            InvalidateRect(hwnd, &rc, FALSE);
            rc.left = 0;
            rc.top = 0;
            rc.right = sidebar;
            rc.bottom = 145;
            InvalidateRect(hwnd, &rc, FALSE);
        }
        if (app->ViewportToolHovered() == 1) {
            SetCursor(LoadCursorA(nullptr,
                                  reinterpret_cast<LPCSTR>(
                                      static_cast<INT_PTR>(0x7F89))));  // IDC_HAND
        }

        // --- 4. drag-frame remap (0x444F46) --------------------------------
        if (app->TimelineSelectionChanged() != 0) {
            const int row =
                (app->MouseX() -
                 app->SelectionBoxAnchorX() - 6) /
                kRowPitch;  // v15 == v257 (magic /13 = truncating)
            app->SceneModified() = 1;
            if (app->state.optflag[0] == 0) {
                // ---- bone-edit mode (0x445887..0x446065) ------------------
                unsigned char* model = CurrentModel(app);
                mdl::ModelRecord& record = *mdl::Mdl(model);
                // bone list: 28-byte records, idx > 0 (0x44588F..0x445B30)
                RemapList(app,
                          reinterpret_cast<const int*>(app->TimelineSelectionRecords(
                              TimelineSelectionBand::ModelBone)),
                          app->TimelineSelectionCount(TimelineSelectionBand::ModelBone),
                          mdl::DisplayKeys(model), row, -1,
                          &record.maxFrame);
                // morph list: 20-byte records, idx >= model count
                // (0x445B38..0x445D90)
                RemapList(app,
                          reinterpret_cast<const int*>(app->TimelineSelectionRecords(
                              TimelineSelectionBand::ModelMorph)),
                          app->TimelineSelectionCount(
                              TimelineSelectionBand::ModelMorph),
                          mdl::MorphKeys(model), row,
                          static_cast<std::int32_t>(record.morphCount),
                          &record.maxFrame);
                // IK list: 60-byte records, idx >= model count
                // (0x445D9E..0x446040)
                RemapList(app,
                          reinterpret_cast<const int*>(app->TimelineSelectionRecords(
                              TimelineSelectionBand::ModelIk)),
                          app->TimelineSelectionCount(TimelineSelectionBand::ModelIk),
                          mdl::BoneKeys(model), row,
                          static_cast<std::int32_t>(record.boneCount),
                          &record.maxFrame);
                PanelPaint(app);  // 0x446044
                // 0x446065: original __thiscall(this=model, dword0x980,
                // dword0xA0CC4).
                SeekModelFrame(model,
                          app->state.currentFrame,
                          app->state.playbackPhysicsMode);
            } else {
                // ---- display mode (0x444F8A..0x44587D) --------------------
                // four fixed band lists (84/40/24/36-byte records)
                RemapList(app,
                          reinterpret_cast<const int*>(app->TimelineSelectionRecords(
                              TimelineSelectionBand::Camera)),
                          app->TimelineSelectionCount(TimelineSelectionBand::Camera),
                          app->CameraKeys(),
                          row, -1, nullptr);
                RemapList(app,
                          reinterpret_cast<const int*>(app->TimelineSelectionRecords(
                              TimelineSelectionBand::Light)),
                          app->TimelineSelectionCount(TimelineSelectionBand::Light),
                          app->LightKeys(),
                          row, -1, nullptr);
                RemapList(app,
                          reinterpret_cast<const int*>(app->TimelineSelectionRecords(
                              TimelineSelectionBand::SelfShadow)),
                          app->TimelineSelectionCount(TimelineSelectionBand::SelfShadow),
                          app->ShadowKeys(),
                          row, -1, nullptr);
                RemapList(app,
                          reinterpret_cast<const int*>(app->TimelineSelectionRecords(
                              TimelineSelectionBand::Gravity)),
                          app->TimelineSelectionCount(TimelineSelectionBand::Gravity),
                          app->GravityKeys(),
                          row, -1, nullptr);
                RemapAccList(app,
                             reinterpret_cast<const int*>(app->TimelineSelectionRecords(
                                 TimelineSelectionBand::Accessory)),
                             app->TimelineSelectionCount(
                                 TimelineSelectionBand::Accessory), row);
                PanelPaint(app);   // 0x445837
                ReloadModels(app);    // 0x44583E
                RefreshLightPanel(app);    // 0x445845
                RefreshSelfShadowPanel(app);    // 0x44584C
                ApplyGravityTrack(app);    // 0x445853
                // per-slot refresh for every set flag (0x445858..0x445879)
                for (int i = 0; i < 255; ++i) {
                    if (app->ObjectSlot(i) != nullptr)
                        ApplyAccessoryTrack(app, i);
                }
                SyncAccessoryEditPanel(app);    // 0x44587D
            }
        } else if (app->SelectionBoxDragging() != 0) {
            // ---- 5. hover highlight (0x44606F..0x446A2C) ------------------
            // hover row/column range from the anchor (this+0xA018C/0xA0190)
            int xEnd = app->MouseX();
            int xStart;
            if (xEnd <= app->SelectionBoxAnchorX() + 6) {
                xStart = xEnd;
                xEnd = app->SelectionBoxAnchorX() + 6;
            } else {
                xStart = app->SelectionBoxAnchorX() + 6;
            }
            int yTop, yBottom;
            if (app->MouseY() <=
                app->SelectionBoxAnchorY() + 145) {
                yTop = app->MouseY();
                yBottom = app->SelectionBoxAnchorY() + 145;
            } else {
                yTop = app->SelectionBoxAnchorY() + 145;
                yBottom = app->MouseY();
            }
            int rowStart = (xStart - kGridX0) / kRowPitch;  // 0x4460BC
            if (xStart - kRowPitch * rowStart - kGridX0 <= 6)
                rowStart -= 1;
            if (rowStart < 0)
                rowStart = 0;
            int rowEnd = (xEnd - kGridX0) / kRowPitch;      // 0x4460F8
            if (xEnd - kRowPitch * rowEnd - kGridX0 > 6)
                rowEnd += 1;
            if (rowEnd > app->SidebarWidth() - 22)
                rowEnd = app->SidebarWidth() - 22;
            int colStart = (yTop - kColY0) / kColPitch;     // 0x446135
            if (yTop - kColPitch * colStart - kColY0 <= 7)
                colStart -= 1;
            if (colStart < 0)
                colStart = 0;
            int colEnd = (yBottom - kColY0) / kColPitch;    // 0x44617B
            if (yBottom - kColPitch * colEnd - kColY0 > 7)
                colEnd += 1;

            const unsigned char flag = HoverFlag(app);
            if (app->state.optflag[0] != 0) {
                // ---- display-mode hover (0x4461B3) ------------------------
                if (!app->ShiftModifierActive()) {
                    // clear band visibility flags for all 200 rows
                    // (0x4461CE..0x446312)
                    auto* cameraKeys = app->CameraKeys();
                    auto* lightKeys = app->LightKeys();
                    auto* shadowKeys = app->ShadowKeys();
                    auto* gravityKeys = app->GravityKeys();
                    int* bandMaps = app->state.rowHitBand1;
                    int* accWalk =
                        app->state.rowHitAcc + 1;
                    for (int row_i = 0; row_i < 200; ++row_i) {
                        const int b0 = bandMaps[-200];  // band0 map
                        if (b0 >= 0)
                            cameraKeys[b0].selected = 0;
                        const int b1 = bandMaps[0];     // band1 map
                        if (b1 >= 0)
                            lightKeys[b1].selected = 0;
                        const int b2 = bandMaps[200];   // band2 map
                        if (b2 >= 0)
                            shadowKeys[b2].selected = 0;
                        const int b3 = bandMaps[400];   // band3 map
                        if (b3 >= 0)
                            gravityKeys[b3].selected = 0;
                        // accessory map + slot array, 40 groups of 5
                        // (0x446242..0x4462FC); accWalk keeps walking the
                        // whole map across rows, slotWalk restarts per row.
                        int* slotWalk = app->state.jointLineMap + 4;
                        for (int g = 0; g < 40; ++g, slotWalk += 5, accWalk += 5) {
                            const int a0 = accWalk[-1];
                            if (a0 >= 0) {
                                const int s0 = slotWalk[-1];
                                if (s0 >= 0)
                                    SetAccessorySelected(app, s0, a0, 0);
                            }
                            const int accSel0 = accWalk[0];
                            if (accSel0 >= 0 && slotWalk[0] >= 0)
                                SetAccessorySelected(app, slotWalk[0], accSel0, 0);
                            const int accSel1 = accWalk[1];
                            if (accSel1 >= 0) {
                                const int s2 = slotWalk[1];
                                if (s2 >= 0)
                                    SetAccessorySelected(app, s2, accSel1, 0);
                            }
                            const int accSel2 = accWalk[2];
                            if (accSel2 >= 0) {
                                const int s3 = slotWalk[2];
                                if (s3 >= 0)
                                    SetAccessorySelected(app, s3, accSel2, 0);
                            }
                            const int accSel3 = accWalk[3];
                            if (accSel3 >= 0) {
                                const int s4 = slotWalk[3];
                                if (s4 >= 0)
                                    SetAccessorySelected(app, s4, accSel3, 0);
                            }
                        }
                        ++bandMaps;
                    }
                }
                // hovered-row highlight for the four fixed bands
                // (0x446342..0x44649F)
                if (yBottom >= 160 && yTop <= 174 && rowStart < rowEnd) {
                    auto* keys = app->CameraKeys();
                    int* m = app->state.rowHitBand0 + rowStart;
                    int n = rowEnd - rowStart;
                    do {
                        if (*m >= 0)
                            keys[*m].selected = flag;
                        ++m;
                    } while (--n);
                }
                if (yBottom >= 174 && yTop <= 188 && rowStart < rowEnd) {
                    auto* keys = app->LightKeys();
                    int* m = app->state.rowHitBand1 + rowStart;
                    int n = rowEnd - rowStart;
                    do {
                        if (*m >= 0)
                            keys[*m].selected = flag;
                        ++m;
                    } while (--n);
                }
                if (yBottom >= 188 && yTop <= 202 && rowStart < rowEnd) {
                    auto* keys = app->ShadowKeys();
                    int* m = app->state.rowHitBand2 + rowStart;
                    int n = rowEnd - rowStart;
                    do {
                        if (*m >= 0)
                            keys[*m].selected = flag;
                        ++m;
                    } while (--n);
                }
                if (yBottom >= 202 && yTop <= 216 && rowStart < rowEnd) {
                    auto* keys = app->GravityKeys();
                    int* m = app->state.rowHitBand3 + rowStart;
                    int n = rowEnd - rowStart;
                    do {
                        if (*m >= 0)
                            keys[*m].selected = flag;
                        ++m;
                    } while (--n);
                }
                // accessory grid rows x columns (0x4464A1..0x446534)
                // x64 0x7FF7CB459875 resets the column cursor for each row;
                // only the row base advances by 0x320 at 0x7FF7CB4598BC.
                VisitTimelineSelectionCells(rowStart, rowEnd, colStart, colEnd,
                    [&](std::size_t cell, int column) {
                        const int key = app->state.rowHitAcc[cell];
                        const int slot = app->state.jointLineMap[column];
                        if (key >= 0 && slot >= 0)
                            SetAccessorySelected(app, slot, key, flag);
                    });
            } else {
                // ---- bone-edit mode hover (0x44653B) ----------------------
                unsigned char* model = CurrentModel(app);
                mdl::DisplayKey* displayKeys = mdl::DisplayKeys(model);
                mdl::MorphKey* morphKeys = mdl::MorphKeys(model);
                mdl::BoneKey* boneKeys = mdl::BoneKeys(model);
                if (!app->ShiftModifierActive()) {
                    // clear bone/morph/IK visibility flags for all 200 rows
                    // (0x44653F..0x4467F0)
                    displayKeys[0].allocated = 0;
                    boneKeys[0].allocated = 0;
                    for (std::size_t cell = 0; cell < 200 * 200; ++cell) {
                        const int display = app->state.rowHitIk[cell];
                        if (display > 0)
                            displayKeys[display].allocated = 0;
                        const int morph = app->state.rowHitMorph[cell];
                        if (morph > 0)
                            morphKeys[morph].allocated = 0;
                        const int bone = app->state.rowHitBone[cell];
                        if (bone > 0)
                            boneKeys[bone].allocated = 0;
                    }
                }
                // hovered-row highlight over the morph map grid
                // (0x446815..0x446A0F)
                int colStartB = (yTop - kColY0Bone) / kColPitch;
                if (yTop - kColPitch * colStartB - kColY0Bone <= 7)
                    colStartB -= 1;
                if (colStartB < 0)
                    colStartB = 0;
                int colEndB = (yBottom - kColY0Bone) / kColPitch;
                if (yBottom - kColPitch * colEndB - kColY0Bone > 7)
                    colEndB += 1;
                // x64 0x7FF7CB459A6C reloads the row cursor; 0x459BB9
                // advances the row base independently of the inner loop.
                VisitTimelineSelectionCells(rowStart, rowEnd, colStartB, colEndB,
                    [&](std::size_t cell, int) {
                        const int display = app->state.rowHitIk[cell];
                        if (display > 0)
                            displayKeys[display].allocated = flag;
                        else if (display == -10)
                            displayKeys[0].allocated = flag;
                        const int morph = app->state.rowHitMorph[cell];
                        if (morph > 0)
                            morphKeys[morph].allocated = flag;
                        else if (morph == -10)
                            morphKeys[0].allocated = flag;
                        const int bone = app->state.rowHitBone[cell];
                        if (bone > 0)
                            boneKeys[bone].allocated = flag;
                        else if (bone == -10)
                            boneKeys[0].allocated = flag;
                    });
                // hover rect (0x446A13..0x446A25)
                app->TimelineRangeFirstOffset() = rowStart;
                app->TimelineRangeLastOffset() = rowEnd;
                app->TimelineRangeFirstBase() = colStartB;
                app->TimelineRangeLastBase() = colEndB;
            }
            PanelPaint(app);  // 0x446A2D
            // rect reset (0x446A32..0x446A46)
            app->TimelineRangeFirstOffset() = 0;
            app->TimelineRangeLastOffset() = 0;
            app->TimelineRangeFirstBase() = 0;
            app->TimelineRangeLastBase() = 0;
        }
    }

    // --- 6. physics-enabled hover helper (0x446A4C) -------------------------
    if (app->PendingTimelineSelectionRow() != TimelineSelectionRow::None)
        DragInterpolationControlPoint(app);
}

}  // namespace mikudancestudio
