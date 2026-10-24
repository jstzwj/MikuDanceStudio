// ===========================================================================
// VA 0x00414610 - PanelPaint  (original: sub_414610, ~0x880 bytes)
// ===========================================================================
// Right-panel repaint (called from the scroll handlers and the editor click
// path).  Everything draws onto the offscreen panel DC (this+0x2D4) which
// WM_PAINT (0x47C0A0) later blits to the window at (6,145).
//
// Draw flow (original instruction ranges in parentheses):
//   1. 0x414638  compat DC over the panel bitmap (this+0x2DC); SRCCOPY from
//      the bitmap at (91,0) size (sidebar, listH) erases stale rows of the
//      list column, then DeleteDC.
//   2. 0x414696 / 0x414757  two band-background passes: PS_SOLID 1px pen +
//      solid brush of colour this+0xA063C resp. this+0xA0640; 100 bands,
//      28 px apart, starting y = 30 resp. y = 16 while y < 2816; a band is
//      painted only when its visibility byte (this+0xA04F8, one byte per
//      band, odd bands at +1) is set; Rectangle(91, y, listW, y+13) with
//      listW = (sub1d574)+0x1D4E4.  Pens/brushes restored via the handles
//      saved at entry (oldPen/oldBrush) and deleted in the original order.
//   3. 0x414812  vertical grid lines: rows = (sidebar-84)/13; each row
//      (x = 100+13i) gets a 1 px line from y=16 to listH ((sub1d574)+
//      0x1D4E8) and, every 5th row, a "%-4d" label (DrawGlyph 0x40E290,
//      size 12, at (x-3, 3)).  The selected row (scroll+idx == this+0x980)
//      uses colour this+0xA0650, multiples of 5 this+0xA0654, others
//      this+0xA0658 (unsigned % 5, as the original div).
//   4. 0x414986  zeroes the three column-major edit-mode hit maps
//      (this+0x984 / 0x27A84 / 0x4EB84, 200 ints per visible frame
//      column, `rows` columns) and fills the four fixed-band maps
//      (this+0x75C84..0x765E4) with -1 plus 200 accessory rows at
//      this+0x76904 with 0xFF.
//   5. 0x414A1F  icon DC over this+0x2F0 (11x11 icon sheet, two rows);
//      band rows are SRCAND mask blits from source (44, row) plus SRCPAINT
//      from (22/0, row); negative links use source column 33.
//   6. 0x414A29  display mode (optflag[0] @0x2F8 != 0): four fixed band lists
//      (this+0x374/0x378/0x37C/0x380, record strides 84/40/24/36, band
//      y = 17/31/45/59, flag byte +72/+36/+20/+33) plus the accessory bands
//      (this+0x9D9D0 index array -> pointers at this+0x384, stride 60,
//      y = 73+14k while y < 2873); else bone-edit mode: bone list
//      (model+0x26E8, stride 28, flag +20, link = model+0x2DBC int), morph
//      list (model+0x26E4, stride 20, flag +16, links model+0x2DB8[head],
//      count model+0x2D80) and IK list (model+0x26E0, stride 60, flags +56/
//      +57, links model+0x2DB4[head], count model+0x2D84).  The mask byte
//      used for the IK icon rows is *(*(model+0x26BC) + sizeof(mikudancestudio::mdl::BoneRecord) *head + 0x1EC).
//      The per-record "visibility" byte (+16/+56) is re-derived from the
//      drag-rect (this+0xA05C0..0xA05CC, byte this+0xA05D0) and mode
//      words this+0x24 / this+0xC0 whenever the shared mask
//      (byte this+0xA0189 && dword this+0xA03EB == 0) and a negative link
//      apply.  Row maps: bone this+0x4EB84, morph this+0x27A84, IK
//      this+0x984; sentinels 0 (cleared), -1, -2, -10 (index 0).
//   7. 0x415C6B  SCROLLINFO at this+0x960 for the list scrollbar (control
//      428, SB_CTL): cbSize 28 / fMask SIF_ALL / nMin 0; nMax =
//      max(pos+rows, <listlen>+20, selected+rows/2) - 1, where <listlen>
//      is this+0x9E16C in display mode and model+0x31B0 (+ the extra
//      this+0x9E16C clamp) in bone-edit mode; nPage = rows; nPos = pos.
//   8. 0x415D0E  when dword this+0xA03EB == 0 && byte this+0xA0189 != 0:
//      0x969696 selection box from (this+0xA018C, this+0xA0190) to the
//      clamped corner ((this+4)-6, (this+8)-145); then DeleteDC(icons),
//      PostViewRefresh (0x40D130), and InvalidateRect{left 6, top 145,
//      right sidebar-19, bottom bottom-248} on the main window.
//
// GDI object lifetimes, loop bounds and coordinate constants mirror the
// original instruction stream one-to-one; the repeated per-record drawing
// of the four fixed bands is factored into DrawBandList() (behaviour
// identical, address range annotated).
//
// Reference: ../translated/MikuMikuDance/fcn_00414610.cpp
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#include <cstdint>
#include <cstring>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

// --- layout / draw constants from the original instruction stream --------
constexpr int kListLeft = 91;    // 0x5B  list column x
constexpr int kIconLeft = 95;    // 0x5F  marker x; centred on grid x=100
constexpr int kRowH = 13;        // 0x0D  row pitch (grid lines, bands)
constexpr int kBandH = 14;       // 0x0E  band pitch (icon rows)
constexpr int kBandLastY = 2816; // 0xB00 band loops run while y < 2816
constexpr int kGridX0 = 100;     // 0x64  first grid-line x
constexpr int kGridTopY = 16;    // 0x10  grid lines start at y = 16
constexpr int kIcon = 11;        // 0x0B  icon square
constexpr int kAccY0 = 73;       // 0x49  first accessory band y
constexpr int kAccLastY = 2873;  //      accessory bands run while y < 2873
constexpr int kSheetMaskX = 44;  // 0x2C  SRCAND source column
constexpr int kSheetOnX = 22;    // 0x16  SRCPAINT "enabled" column
constexpr int kSheetNegX = 33;   // 0x21  SRCPAINT "reverse link" column
constexpr int kSheetAltX = 11;   // 0x0B  SRCPAINT alternate column
constexpr int kSheetOffX = 0;    //      SRCPAINT "disabled" column

// --- row-hit map base (200 ints per band/row) ----------------------------

// --- model object fields (bone-edit mode) ---------------------------------
// Reached through the ModelRecord accessors (morphCount / boneCount /
// morphKeyIndices / boneKeyIndices / boneListSelLine / maxFrame /
// displayRootBone).  The former x86-raw constants (0x2D80/0x2D84/0x2DB4/
// 0x2DB8/0x2DBC/0x31B0/0x3900) drifted from the x64 layout and read
// unrelated fields on the 64-bit build.

// (0x374/0x378/0x37C/0x380 are state.cameraKeyTrack / lightKeyTrack /
// selfShadowKeyTrack / gravityKeyTrack, reached through the
// CameraKeys()/LightKeys()/ShadowKeys()/GravityKeys() accessors; the clean
// erase template is state.bmpPanelSpare, the live drawing bitmap selected
// into PanelDC is state.bmpPanel, and the 11x11 icon sheet is
// state.bmpRes101.)

// Current model slot for the slot-order index (this+0x910) - 0x414F6F.
unsigned char* CurrentModel(MMDApp* app) {
    return app->SelectedModel();
}

// One fixed global timeline list.  Per-record body of
// 0x414A51 / 0x414B30 / 0x414C10 / 0x414CF0; the accessory band loop
// (0x414E00) keeps its own body because its map is laid out per row.
template <typename Key>
void DrawBandList(MMDApp* app, HDC panel, HDC icons, Key* keys, int bandY, std::int32_t (&map)[200],
                  int rowCount) {
    // The original never sees these four lists null because 0x466D20
    // (reached via the window proc on WM_CREATE) allocates them before
    // LocalizeUI; that path is not ported yet, so guard until it lands.
    if (keys == nullptr)
        return;
    const std::int32_t scroll = app->state.timelineStartFrame;
    const std::int32_t limit = scroll + rowCount;
    if (static_cast<std::int32_t>(keys[0].frame) >= limit)
        return;
    int record = 0;
    for (;;) {
        const Key& key = keys[record];
        const int row = static_cast<std::int32_t>(key.frame) - scroll;
        if (row >= 0) {
            const int x = kRowH * row + kIconLeft;
            BitBlt(panel, x, bandY, kIcon, kIcon, icons, kSheetMaskX, 0, SRCAND);
            BitBlt(panel, x, bandY, kIcon, kIcon, icons, key.selected ? kSheetOnX : kSheetOffX, 0,
                   SRCPAINT);
            map[row] = record;
        }
        const int next = static_cast<int>(key.next);
        record = next;
        if (next == 0 || static_cast<std::int32_t>(keys[next].frame) >= limit)
            return;
    }
}

} // namespace

void PanelPaint(MMDApp* app) {
    const std::int32_t sidebar = app->SidebarWidth();
    D3DRenderer* sub = app->Renderer();
    // (The 0x1D574 render wrapper's "list width/height" reads below are its
    // screenWidth/screenHeight fields, sub+0x1D4E4/0x1D4E8.)
    const std::int32_t listW = sub->screenWidth;  // sub+0x1D4E4 "list width"
    const std::int32_t listH = sub->screenHeight; // sub+0x1D4E8 "list height"
    HDC panel = app->PanelDC();
    const std::int32_t scroll = app->state.timelineStartFrame;

    // --- 1. restore list region from the clean template (0x414638) --------
    // The erase source is the SPARE bitmap (x86 this+0x2DC / x64 app+752
    // = bmpPanelSpare): the pristine template drawn at init.  bmpPanel is
    // the live bitmap selected into PanelDC, so copying from it would be a
    // self-copy no-op and every non-band pixel (ruler strip, flagless
    // rows) would keep its previous frame - stacked ruler digits and icon
    // trails.
    HDC dc = CreateCompatibleDC(nullptr);
    SelectObject(dc, reinterpret_cast<HGDIOBJ>(app->state.bmpPanelSpare));
    BitBlt(panel, kListLeft, 0, sidebar, listH, dc, kListLeft, 0, SRCCOPY);
    DeleteDC(dc);

    // --- 2. band backgrounds (0x414696 / 0x414757) ------------------------
    HPEN pen = CreatePen(PS_SOLID, 1, app->ThemeColor(UiThemeColor::PanelHeader));
    HBRUSH brush = CreateSolidBrush(app->ThemeColor(UiThemeColor::PanelHeader));
    HGDIOBJ oldPen = SelectObject(panel, pen);
    HGDIOBJ oldBrush = SelectObject(panel, brush);
    unsigned char* flags = app->PanelRowFlags() + 1;
    for (int y = 30; y < kBandLastY; y += kBandH * 2, flags += 2) {
        if (*flags)
            Rectangle(panel, kListLeft, y, listW, y + kRowH);
    }
    SelectObject(panel, oldPen);
    DeleteObject(pen);
    SelectObject(panel, oldBrush);
    DeleteObject(brush);

    HPEN pen2 = CreatePen(PS_SOLID, 1, app->ThemeColor(UiThemeColor::PanelBody));
    HBRUSH brush2 = CreateSolidBrush(app->ThemeColor(UiThemeColor::PanelBody));
    SelectObject(panel, pen2);
    SelectObject(panel, brush2);
    flags = app->PanelRowFlags();
    for (int y = 16; y < kBandLastY; y += kBandH * 2, flags += 2) {
        if (*flags)
            Rectangle(panel, kListLeft, y, listW, y + kRowH);
    }
    SelectObject(panel, oldPen);
    DeleteObject(pen2);
    SelectObject(panel, oldBrush);
    DeleteObject(brush2);

    // --- 3. row labels + grid lines (0x414812) -----------------------------
    const int rows = (sidebar - 84) / kRowH; // signed, as the original
    if (rows < 0)
        return;
    if (rows > 0) {
        char label[0x100];
        char sel[0x100];
        int x = kGridX0;
        for (int idx = 0; idx < rows; ++idx) {
            const std::int32_t n = scroll + idx;
            COLORREF color;
            if (n == app->state.currentFrame) {
                sprintf_s(sel, sizeof(sel), "%-4d", n);
                // 0xA0650..+2 are the R/G/B bytes of themeColors[30] (the
                // selected-row colour); the G/B reads take the byte view of
                // the COLORREF instead of raw offsets, whose +1/+2 bytes do
                // not translate on x64.
                DrawGlyph(app, sel, panel, 12, x - 3, 3, app->state.themeColors[30],
                          reinterpret_cast<const unsigned char*>(&app->state.themeColors[30])[1],
                          reinterpret_cast<const unsigned char*>(&app->state.themeColors[30])[2],
                          1);
                color = static_cast<COLORREF>(app->state.themeColors[30]);
            } else if (static_cast<std::uint32_t>(n) % 5 != 0) {
                color = static_cast<COLORREF>(app->state.themeColors[32]);
            } else {
                sprintf_s(label, sizeof(label), "%-4d", n);
                DrawGlyph(app, label, panel, 12, x - 3, 3, app->state.themeColors[33],
                          reinterpret_cast<const unsigned char*>(&app->state.themeColors[33])[1],
                          reinterpret_cast<const unsigned char*>(&app->state.themeColors[33])[2],
                          1);
                color = static_cast<COLORREF>(app->state.themeColors[31]);
            }
            HPEN line = CreatePen(PS_SOLID, 1, color);
            SelectObject(panel, line);
            MoveToEx(panel, x, kGridTopY, nullptr);
            LineTo(panel, x, listH);
            SelectObject(panel, oldPen);
            DeleteObject(line);
            x += kRowH;
        }
    }

    // --- 4. clear the hit maps (0x414986 / 0x4149CB) -----------------------
    // The three edit-mode maps are COLUMN-MAJOR: 200 ints per visible frame
    // column, indexed [trackRow + 200*frameColumn] by the click handler and
    // the paint chains.  Both originals clear rows x 200 entries per map
    // (x86 0x414986: outer `rows` iterations of a 200-int sweep over
    // 0x984/0x27A84/0x4EB84; x64 0x7FF7CB4814C0 identical), NOT just the
    // first column - anything less leaves stale record indices in columns
    // 1..N after a scroll, so clicks and box-sweeps toggle keys that are
    // no longer under the cursor.
    // Historic naming inversion: the old local name said "kMapIk", but
    // 0x984 is pinned as rowHitBone (the bone row map).  Likewise the
    // old "kMapBone" 0x4EB84 is pinned as rowHitIk exactly - NOT the
    // BoneEditRowMap() rowHitIk+64 view, which matches x86 0x4EC84.
    // Columns beyond the first land in the reserved tail behind each
    // 200-int head (same situation as rowHitAcc, see app_layout.hpp).
    if (rows > 0) {
        std::int32_t* const ikMap = app->state.rowHitBone;
        std::int32_t* const morphMap = app->state.rowHitMorph;
        std::int32_t* const boneMap = app->state.rowHitIk;
        for (int column = 0; column < rows; ++column) {
            const std::size_t base = static_cast<std::size_t>(column) * 200;
            for (int index = 0; index < 200; ++index) {
                const std::size_t slot = base + static_cast<std::size_t>(index);
                ikMap[slot] = 0;
                morphMap[slot] = 0;
                boneMap[slot] = 0;
            }
        }
    }
    {
        std::int32_t* const band0Map = app->state.rowHitBand0;
        std::int32_t* const band1Map = app->state.rowHitBand1;
        std::int32_t* const band2Map = app->state.rowHitBand2;
        std::int32_t* const band3Map = app->state.rowHitBand3;
        unsigned char* acc = reinterpret_cast<unsigned char*>(app->state.rowHitAcc);
        for (int row = 0; row < 200; ++row) {
            band0Map[row] = -1;
            band1Map[row] = -1;
            band2Map[row] = -1;
            band3Map[row] = -1;
            memset(acc, 0xFF, 0x320);
            acc += 800;
        }
    }

    // --- 5. icon DC over the icon sheet (0x414A1F) -------------------------
    HDC icons = CreateCompatibleDC(nullptr);
    SelectObject(icons, reinterpret_cast<HGDIOBJ>(app->state.bmpRes101));

    // Shared scrollbar + selection-box + refresh tail (0x415C6B..0x415E64).
    // a/b are the two "list length + 20" nMax candidates (second one is the
    // this+0x9E16C clamp applied only in bone-edit mode; both branches pass
    // it, which is a no-op difference for display mode since a == b there).
    auto FinishPanel = [&](std::uint32_t a, std::uint32_t b) {
        const std::uint32_t pos = static_cast<std::uint32_t>(scroll);
        // x64 @0x7FF7CB482963：SCROLLINFO 内嵌在 app+0x1430（cbSize=1Ch、
        // fMask=SIF_ALL），nPage/nPos 落在 app+0x1440/0x1444；HandleHScroll
        // （0x7FF7CB45E060）case 2/3/5 回读的就是这两格。x86 原版把这块临时
        // 结构放在 app+0x960 暂存区、靠地址重叠留住 nPage/nPos——该字节地址
        // 在 x64 布局上不是状态字段（与附属物轨道指针重叠），所以这里改成
        // 局部对象 + 关键字段别名回写状态域（427 竖条的惯用法，见 wm_paint.cpp）。
        SCROLLINFO scrollInfo{};
        scrollInfo.cbSize = sizeof(scrollInfo);
        scrollInfo.fMask = SIF_ALL;
        scrollInfo.nMin = 0;
        std::uint32_t nMax = pos + static_cast<std::uint32_t>(rows);
        if (nMax < a)
            nMax = a;
        if (nMax < b)
            nMax = b;
        const std::uint32_t mid = static_cast<std::uint32_t>(app->state.currentFrame) +
                                  static_cast<std::uint32_t>(rows / 2);
        if (nMax < mid)
            nMax = mid;
        scrollInfo.nMax = static_cast<int>(nMax) - 1;
        scrollInfo.nPage = static_cast<DWORD>(rows);
        scrollInfo.nPos = static_cast<int>(pos);
        // 别名回写：不回写则 page/thumb 滚动读到 0，SB_PAGEUP/DOWN 不动、
        // SB_THUMBPOSITION 每次都跳到绝对滑块位置（x64 里这两格永驻 app 结构）。
        app->state.timelineScrollNPage = static_cast<std::int32_t>(scrollInfo.nPage);
        app->state.timelineScrollNPos = scrollInfo.nPos;
        SetScrollInfo(GetDlgItem(static_cast<HWND>(app->Hwnd()), panel::kTimelineHScroll), SB_CTL,
                      &scrollInfo, TRUE);

        RECT rc;
        GetClientRect(static_cast<HWND>(app->Hwnd()), &rc);
        if (app->SelectionBoxDragging() != 0 && app->TimelineSelectionChanged() == 0) {
            HPEN box = CreatePen(PS_SOLID, 1, 0x969696);
            SelectObject(panel, box);
            int x2 = app->MouseX() - 6;
            int y2 = app->MouseY() - 145;
            if (x2 < kListLeft)
                x2 = kListLeft;
            if (y2 < 17)
                y2 = 17;
            if (x2 > sidebar - 28)
                x2 = sidebar - 28;
            if (y2 > rc.bottom - 396)
                y2 = rc.bottom - 396;
            const int ax = app->SelectionBoxAnchorX();
            const int ay = app->SelectionBoxAnchorY();
            MoveToEx(panel, ax, ay, nullptr);
            LineTo(panel, ax, y2);
            LineTo(panel, x2, y2);
            LineTo(panel, x2, ay);
            LineTo(panel, ax, ay);
            SelectObject(panel, oldPen);
            DeleteObject(box);
        }
        DeleteDC(icons);
        PostViewRefresh(app);
        rc.bottom -= 248;
        rc.left = 6;
        rc.top = 145;
        rc.right = sidebar - 19;
        InvalidateRect(static_cast<HWND>(app->Hwnd()), &rc, FALSE);
    };

    if (app->CameraMode() != 0) {
        // --- 6a. display mode: four fixed bands + accessory bands ----------
        const std::int32_t limit = scroll + rows;
        DrawBandList(app, panel, icons, app->CameraKeys(), 17, app->state.rowHitBand0, rows);
        DrawBandList(app, panel, icons, app->LightKeys(), 31, app->state.rowHitBand1, rows);
        DrawBandList(app, panel, icons, app->ShadowKeys(), 45, app->state.rowHitBand2, rows);
        DrawBandList(app, panel, icons, app->GravityKeys(), 59, app->state.rowHitBand3, rows);
        // accessory bands (0x414DC7..0x414F17)
        // state.jointLineMap is the 0x9DA50 array: 0x414DB1
        // `lea eax,[esi+9DA50h]` reads the SAME 200-int array
        // RebuildTimelineTrackPanel memsets to -1 before the joint walk - dual use as
        // the accessory band index source.  (An earlier revision read
        // 0x9D9D0, which stays zero, so the idx<0 break never fired and the
        // band loop dereferenced a null accessory slot at startup.)
        const std::int32_t* idxArr = app->state.jointLineMap;
        int band = 0;
        int bandY = kAccY0;
        while (bandY < kAccLastY) {
            const int idx = idxArr[band];
            if (idx < 0)
                break;
            mdl::AccessoryKey* list = app->AccessoryKeys(idx);
            if (list == nullptr) // see the DrawBandList note
                break;
            int record = 0;
            if (static_cast<std::int32_t>(list[0].frame) < limit) {
                for (;;) {
                    const mdl::AccessoryKey& key = list[record];
                    const int row = static_cast<std::int32_t>(key.frame) - scroll;
                    if (row >= 0) {
                        const int x = kRowH * row + kIconLeft;
                        BitBlt(panel, x, bandY, kIcon, kIcon, icons, kSheetMaskX, 0, SRCAND);
                        BitBlt(panel, x, bandY, kIcon, kIcon, icons,
                               key.selected ? kSheetOnX : kSheetOffX, 0, SRCPAINT);
                        // NOTE: kept as raw - the index band+200*row reaches
                        // beyond state.rowHitAcc[800] into the unnamed pad95
                        // region (the x86 blob reserves 200 rows of 200 ints
                        // here), and the x64 xlate carries element entries
                        // only for the first 800 ints.
                        app->state.rowHitAcc[static_cast<unsigned>(band) + 200u * row] = record;
                    }
                    const int next = static_cast<int>(key.next);
                    record = next;
                    if (next == 0 || static_cast<std::int32_t>(list[next].frame) >= limit)
                        break;
                }
            }
            ++band;
            bandY += kBandH;
        }
        const std::uint32_t len = static_cast<std::uint32_t>(app->LastRegisteredFrame()) + 20;
        FinishPanel(len, len);
    } else {
        // --- 6b. bone-edit mode: bone / morph / IK lists -------------------
        unsigned char* model = CurrentModel(app);
        const std::int32_t limit = scroll + rows;

        // model display/IK list (model+0x26E8) - 0x414F6F..0x415122
        mdl::DisplayKey* displayKeys = mdl::DisplayKeys(model);
        if (static_cast<std::int32_t>(displayKeys[0].frame) < limit) {
            int record = 0;
            for (;;) {
                const mdl::DisplayKey& key = displayKeys[record];
                const int row = static_cast<std::int32_t>(key.frame) - scroll;
                if (row >= 0) {
                    const int link = mdl::Mdl(model)->boneListSelLine;
                    if (link != 0) {
                        const int x = kRowH * row + kIconLeft;
                        const int y = kBandH * link + 17;
                        BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetMaskX, 0, SRCAND);
                        BitBlt(panel, x, y, kIcon, kIcon, icons,
                               key.allocated ? kSheetOnX : kSheetOffX, 0, SRCPAINT);
                        app->state.rowHitIk[link + 200 * row] = record != 0 ? record : -10;
                    }
                }
                const int next = static_cast<int>(key.next);
                record = next;
                if (next == 0 || static_cast<std::int32_t>(displayKeys[next].frame) >= limit)
                    break;
            }
        }

        // morph list (model+0x26E4, 20-byte records) - 0x415129..0x41557A
        mdl::ModelRecord* modelRecord = mdl::Mdl(model);
        if (static_cast<std::int32_t>(modelRecord->morphCount) > 0) {
            mdl::MorphKey* morphList = mdl::MorphKeys(model);
            std::uint32_t* morphLinks = modelRecord->morphKeyIndices;
            std::int32_t* mapMorph = app->state.rowHitMorph;
            const std::int32_t morphCnt = static_cast<std::int32_t>(modelRecord->morphCount);
            for (int head = 0; head < morphCnt; ++head) {
                if (static_cast<std::int32_t>(morphList[head].frame) >= limit)
                    continue;
                int rec = head;
                for (;;) {
                    mdl::MorphKey& key = morphList[rec];
                    const int row = static_cast<std::int32_t>(key.frame) - scroll;
                    if (row >= 0) {
                        const int link = morphLinks[head];
                        if (link != 0) {
                            // visibility flag (+16) maintenance (0x4151B7)
                            if (app->SelectionBoxDragging() != 0 &&
                                app->TimelineSelectionChanged() == 0 && link < 0) {
                                const int neg = -link;
                                if (app->TimelineRangeFirstColumn() <= row &&
                                    row < app->TimelineRangeLastColumn() &&
                                    neg < app->TimelineRangeLastRow() &&
                                    app->TimelineRangeFirstRow() <= neg) {
                                    key.allocated =
                                        static_cast<std::uint8_t>(!app->CtrlModifierActive());
                                } else if (!app->ShiftModifierActive()) {
                                    key.allocated = 0;
                                }
                            }
                            // row drawing (0x415231..0x4154B1)
                            const int x = kRowH * row + kIconLeft;
                            if (key.allocated != 0) {
                                if (link >= 0) {
                                    const int y = kBandH * link + 17;
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetMaskX, 0,
                                           SRCAND);
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetOnX, 0,
                                           SRCPAINT);
                                } else {
                                    const int y = 17 - kBandH * link;
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetMaskX, 0,
                                           SRCAND);
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetNegX, 0,
                                           SRCPAINT);
                                    mapMorph[200 * row - link] = -1;
                                }
                            } else if (link >= 0) {
                                if (mapMorph[200 * row + link] >= 0) {
                                    const int y = kBandH * link + 17;
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetMaskX, 0,
                                           SRCAND);
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetOffX, 0,
                                           SRCPAINT);
                                }
                            } else if (mapMorph[200 * row - link] >= 0) {
                                const int y = 17 - kBandH * link;
                                BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetMaskX, 0, SRCAND);
                                BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetAltX, 0, SRCPAINT);
                                mapMorph[200 * row - link] = -2;
                            }
                            // final map write (0x4154CB)
                            if (link >= 0)
                                mapMorph[200 * row + link] = rec != 0 ? rec : -10;
                        }
                    }
                    const int next = static_cast<int>(key.next);
                    rec = next;
                    if (next == 0 || static_cast<std::int32_t>(morphList[next].frame) >= limit)
                        break;
                }
            }
        }

        // IK list (model+0x26E0, 60-byte records) - 0x41557A..0x415C6B
        const std::int32_t ikCnt = static_cast<std::int32_t>(mdl::Mdl(model)->boneCount);
        if (ikCnt > 0) {
            mdl::BoneKey* ikList = mdl::BoneKeys(model);
            mdl::BoneRecord* bones = mdl::Bones(model);
            std::uint32_t* ikLinks = mdl::Mdl(model)->boneKeyIndices;
            std::int32_t* mapIk = app->state.rowHitBone; // old name kMapIk
            for (int head = 0; head < ikCnt; ++head) {   // chain-head index (x)
                if (static_cast<std::int32_t>(ikList[head].frame) >= limit)
                    continue;
                int rec = head;
                for (;;) {
                    mdl::BoneKey& key = ikList[rec];
                    const int row = static_cast<std::int32_t>(key.frame) - scroll;
                    if (row >= 0) {
                        const int link = ikLinks[head];
                        if (link != 0 || head == mdl::Mdl(model)->displayRootBone) {
                            // visibility flag (+56) maintenance (0x415631..0x4156CB)
                            if (app->SelectionBoxDragging() != 0 &&
                                app->TimelineSelectionChanged() == 0 && link < 0) {
                                const int neg = -link;
                                if (neg < app->TimelineRangeLastRow() &&
                                    row < app->TimelineRangeLastColumn() &&
                                    app->TimelineRangeFirstRow() <= neg &&
                                    app->TimelineRangeFirstColumn() <= row) {
                                    key.allocated =
                                        app->TimelineRangeApplyEnabled() != 0
                                            ? 0
                                            : static_cast<std::uint8_t>(!app->CtrlModifierActive());
                                } else if (!app->ShiftModifierActive()) {
                                    key.allocated = 0;
                                }
                            }
                            // row drawing (0x4156D1..0x415B98)
                            const bool mask =
                                key.physicsDisabled == 0 && bones[head].hasRigidBody != 0;
                            const int x = kRowH * row + kIconLeft;
                            if (key.allocated != 0) {
                                if (link >= 0) {
                                    const int y = kBandH * link + 17;
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetMaskX,
                                           mask ? kIcon : 0, SRCAND);
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetOnX,
                                           mask ? kIcon : 0, SRCPAINT);
                                } else {
                                    const int y = 17 - kBandH * link;
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetMaskX,
                                           mask ? kIcon : 0, SRCAND);
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetNegX, 0,
                                           SRCPAINT);
                                    mapIk[200 * row - link] = -1;
                                }
                            } else if (link >= 0) {
                                if (mapIk[200 * row + link] >= 0) {
                                    const int y = kBandH * link + 17;
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetMaskX,
                                           mask ? kIcon : 0, SRCAND);
                                    BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetOffX,
                                           mask ? kIcon : 0, SRCPAINT);
                                }
                            } else if (mapIk[200 * row - link] == -1) {
                                const int y = 17 - kBandH * link;
                                BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetMaskX,
                                       mask ? kIcon : 0, SRCAND);
                                BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetNegX,
                                       mask ? kIcon : 0, SRCPAINT);
                            } else {
                                const int y = 17 - kBandH * link;
                                BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetMaskX,
                                       mask ? kIcon : 0, SRCAND);
                                // 0x415A35 and 0x415A5C feed the common BitBlt call
                                // with source (11,11) and (11,0), respectively.
                                BitBlt(panel, x, y, kIcon, kIcon, icons, kSheetAltX,
                                       mask ? kIcon : 0, SRCPAINT);
                                mapIk[200 * row - link] = -2;
                            }
                            // final map write (LABEL_156, 0x415B98)
                            if (link >= 0)
                                mapIk[200 * row + link] = rec != 0 ? rec : -10;
                        }
                    }
                    const int next = static_cast<int>(key.next);
                    rec = next;
                    if (next == 0 || static_cast<std::int32_t>(ikList[next].frame) >= limit)
                        break;
                }
            }
        }
        const std::uint32_t a = static_cast<std::uint32_t>(mdl::Mdl(model)->maxFrame) + 20;
        const std::uint32_t b = static_cast<std::uint32_t>(app->LastRegisteredFrame()) + 20;
        FinishPanel(a, b);
    }
}

} // namespace mikudancestudio
