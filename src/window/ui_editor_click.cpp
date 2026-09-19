// ===========================================================================
// VA 0x00446A70 - HandleLButtonDown  (original: sub_446A70)
// ===========================================================================
// WM_LBUTTONDOWN handler for the model-editor panel (sole caller: WndProc
// 0x4C3A10 @ 0x4C412D).  The largest UI function (~0x3F27 bytes, 3591
// instructions).  this+4 / this+8 hold the click X / Y coordinates.
//
// Click regions (gates computed once up front):
//   var_190 = Y > 160 ; var_1A0 = Y < clientBottom - 0xF8 (248)
//   A. Margin column  X in (4, 20):    row-type display-flag toggle
//      (model[0x26D0] table, 0x65-stride, flag at +0x64) + PostLanguageSweep
//   B. Name column    X in (20, 95]:  bone/morph row selection + keyframe
//      insert (0x4A1510) - display-mode sub-branch when byte 0x2F8 set
//   C. Main area      X in (95, sidebar-18) && Y gates: selection bitmap
//      clear across all 100 model slots, scrollbar 0x1A1 sync, per-slot
//      frame apply (0x4B4260 / 0x4A02C0), mode branch on byte 0x2F8,
//      scroll clamp, PanelPaint (0x414610), timeline strip redraw
//      (0x4C2A00 + InvalidateRect), physics sync (0x4C2B80 / 0x4C3530)
//   D. else: bone/morph name-row rect hit test (rects derived from bytes
//      0x9DA05/0x9DA06 and 0x9DA07/0x9DA08 + client bottom) -> keyframe
//      insert + selection-row marker 0x9DA09 = 1 (bone) / 2 (morph)
//
// Every path funnels into the shared tail (dirty flag 0xA0189, click anchor
// 0xA018C/0xA0190) and the selection-count re-evaluation (0xA03EC region,
// 0x40 dwords, per-model flag counting + qsort + 0x49D410 feed).
//
// Layout: HandleLButtonDown keeps the S1 gates and dispatches to one
// file-local sub-function per click region; every original `goto L_tail`
// exit became a plain return, and the L_rowtype forward jump inside the
// name column is kept verbatim:
//   HandleLButtonDown_MarginColumnToggle   S2    0x446B01-0x446B9C
//   HandleLButtonDown_NameColumnHit        S3+S4 0x446BA1-0x44738F
//   HandleLButtonDown_TimelineAreaHit      S5-S6 0x447390-0x448EEB
//   HandleLButtonDown_FrameStripHit        S7    0x44A468-0x44A850
//   HandleLButtonDown_NameRowRectHit       S7b   0x44A855-0x44A97D
//
// Reference: ../translated/MikuMikuDance/fcn_00446a70.cpp
//   NOTE: the translated file is a register-tracking re-render that deviates
//   in places (coordinate mapping, message ids, helper arg lists); this port
//   follows the IDA decompilation of MikuMikuDance.exe 0x446A70.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// Forward declarations for functions ported in this wave whose bodies live
// in other translation units (not yet registered in ported_funcs.hpp;
// declared here with their original VAs).
void PanelPaint(MMDApp* app);                                   // VA 0x00414610
void PostLanguageSweep(MMDApp* app);                            // VA 0x0042F1E0
void TimelineDrawTicks(int frameOffset, int width);             // VA 0x004C2A00
void SetFrameNormalized(int frame);                             // VA 0x004C2B80

// Editor-panel refresh helpers still living in src/app/late_ports.cpp
// (signatures below are the placeholder ones from stubs.cpp; where the
// original passes a MODEL pointer or extra arguments the call site casts
// and marks TODO(port) - see the individual call sites).
void RefreshLightPanel(MMDApp* app);                                    // VA 0x00411070
void RefreshSelfShadowPanel(MMDApp* app);                                    // VA 0x00411B90
void ApplyGravityTrack(MMDApp* app);                                    // VA 0x00412330
void SyncAccessoryEditPanel(MMDApp* app);                                    // VA 0x004134E0
void ApplyAccessoryTrack(MMDApp* app, int idx);                           // VA 0x00413120
void SnapshotPoseBeforeFrameChange(unsigned char* model, int frame);  // VA 0x004A0080
void SyncModelEditControls(unsigned char* model);                     // VA 0x004A02C0
void SnapshotSelectedKeysForUndo(unsigned char* model, int frame);    // VA 0x004A1510
int SeekModelFrame(unsigned char* model, int frame, int physicsMode);  // VA 0x004B4260
void WaveRestartAt(void* sub, double v);                       // VA 0x004C3530
void AviBgOverlayRefresh(MMDApp* app);                                    // VA 0x004168D0
void AppendBoneKeyToUndo(unsigned char* model, int index);           // VA 0x0049D410
void PostLanguageSweep2(MMDApp* app);  // VA 0x0040D070 (ui_view_refresh.cpp);
                                       // was declared here as 0x40D070 - same
                                       // function, the local twin was removed
void SelectionReeval(MMDApp* app);                              // VA 0x00430510 (stubs.cpp)

// qsort comparator of the selection-stat records - original VA 0x0040EC70:
//   v2 = a1[3]; v3 = a2[3]; return (v2 >= v3) ? (v2 > v3) : -1;
// (compares the 4th dword of each 16-byte record; -1 when a1 < a2,
//  (v2>v3) when a1 >= a2 - so equal records return 0)
int CompareFunction(const void* a, const void* b) {
    const std::int32_t keyA = static_cast<const std::int32_t*>(a)[3];
    const std::int32_t keyB = static_cast<const std::int32_t*>(b)[3];
    if (keyA >= keyB)
        return keyA > keyB;
    return -1;
}

// ---- file-local helpers (extracted from sub_446A70, original VAs in the
//      comments) -----------------------------------------------------------

// Signed division by 13 (original magic 0x4EC4EC4F, sar 2) - used for the
// (coord - 100) / 13 row indexing.  (sub_446A70 @ 0x4473F8 etc.)
static int Div13(int v) {
    return v / 13;
}

// Signed division by 14 (original magic 0x92492493, sar 3) - used for the
// (coord - 160) / 14 and (coord - 216) / 14 row indexing.  (sub_446A70 @
// 0x446B5C, 0x446CA2, 0x4472E7)
static int Div14(int v) {
    return v / 14;
}

// Active model pointer = slot[byte this+0x910] of the 100-slot array at
// this+0x780.  (sub_446A70 pattern @ 0x446B48, re-read every use like the
// original reloads it in every loop iteration)
static unsigned char* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

static unsigned char* ModelAtSlot(MMDApp* app, std::size_t slot) {
    return app->ModelSlot(static_cast<int>(slot));
}

// Byte-wise string equality used for the combo-list matching (original
// inline 2-byte-step loop @ 0x446F01 / 0x446F50).
static bool MatchText(const char* a, const char* b) {
    while (*a != '\0') {
        if (*a != *b)
            return false;
        ++a;
        ++b;
    }
    return *b == '\0';
}

// Clear the four selection-bitmap arrays pointed to by this+0x374 (rigid,
// 0x54-stride, flag +0x48), this+0x378 (joint, 0x28-stride, flag +0x24),
// this+0x37C (IK, 0x18-stride, flag +0x14), this+0x380 (morph, 0x24-stride,
// flag +0x21) - 10000 records each - plus the 255-slot record table at
// this+0x384 (0x3C-stride, flag +0x18, 10000 records per slot).
// (sub_446A70 @ 0x447460 / 0x447580 / 0x4476A0 / 0x4477C0 / 0x447900 /
//  0x4479E0 / 0x447A30)
static void ClearSelectionBitmaps(MMDApp* app) {
    for (int i = 0; i < 0x2710; ++i) {
        app->CameraKeys()[i].selected = 0;
        app->LightKeys()[i].selected = 0;
        app->ShadowKeys()[i].selected = 0;
        app->GravityKeys()[i].selected = 0;
    }
    for (int s = 0; s < 0xFF; ++s) {
        auto* rec = reinterpret_cast<unsigned char*>(app->AccessoryKeys(s));
        if (rec == nullptr)
            continue;
        // Original termination is byte offset 0x927C0 (0x44781E,
        // 0x44795E, 0x447A3E).  Dividing by the 0x3C record stride gives
        // exactly 0x2710 records.  The old 0x3D80 loop wrote 344,640 bytes
        // beyond every 600,000-byte accessory table and made a timeline
        // key click corrupt the neighbouring heap allocations.
        for (int i = 0; i < 0x2710; ++i)
            rec[i * 0x3C + 0x18] = 0;
    }
}

// Clear the active model's bone and morph key selections, plus the remaining
// display-key selections.  (sub_446A70 @ 0x447AF0 / 0x447BA0 / 0x447C30 etc.)
static void ClearActiveModelFlags(MMDApp* app) {
    // The original reaches this helper only while the model-edit context is
    // active.  During a partially completed mode switch MikuDanceStudio can briefly
    // expose the same row with no active slot, so preserve that entrance
    // invariant here instead of dereferencing slot 0/null.
    if (ActiveModel(app) == nullptr)
        return;
    unsigned char* model = ActiveModel(app);
    mikudancestudio::mdl::BoneKey* boneKeys = mikudancestudio::mdl::BoneKeys(model);
    for (std::size_t i = 0; i < mikudancestudio::mdl::kBoneKeyCapacity; ++i)
        boneKeys[i].allocated = 0;

    mikudancestudio::mdl::MorphKey* morphKeys = mikudancestudio::mdl::MorphKeys(model);
    for (std::size_t i = 0; i < mikudancestudio::mdl::kMorphKeyCapacity; ++i)
        morphKeys[i].allocated = 0;
    mikudancestudio::mdl::DisplayKey* displayKeys = mikudancestudio::mdl::DisplayKeys(model);
    for (std::size_t i = 0; i < mikudancestudio::mdl::kDisplayKeyCapacity; ++i)
        displayKeys[i].allocated = 0;
}

// The selection bounds use the original timeline address coordinate: key
// links are stored as signed offsets in the same coordinate space.  Keep the
// narrowing at this ABI boundary instead of scattering pointer casts through
// the editor logic.
static std::int32_t TimelineAddress(const void* address) {
    return static_cast<std::int32_t>(
        reinterpret_cast<std::uintptr_t>(address));
}

static void SetTimelineSelectionRange(MMDApp* app, std::int32_t firstOffset,
                                      const void* firstBase,
                                      std::int32_t lastOffset,
                                      const void* lastBase) {
    app->TimelineRangeFirstOffset() = firstOffset;
    app->TimelineRangeFirstBase() = TimelineAddress(firstBase);
    app->TimelineRangeLastOffset() = lastOffset;
    app->TimelineRangeLastBase() = TimelineAddress(lastBase);
}

// 16-byte selection-record fill for one record array (original pattern @
// 0x449070 rigid / 0x4491E0 joint / 0x449340 IK / 0x449490 morph):
//   recStride-byte records, 5 flag bytes each at flagOff + k*flagStride;
//   every set flag appends {index = recIdx-2+k, 0, 0, record dword at
//   flagOff+k*flagStride-flagOff}.  The records are qsorted with the app
//   comparator (0x40EC70).  Count/ptr land in app+countOff/app+ptrOff.
static void FillSelectionRecords(MMDApp* app, unsigned char* arr, int recStride,
                                 int flagOff, int flagStride,
                                 TimelineSelectionBand band) {
    const std::int32_t cnt = app->TimelineSelectionCount(band);
    if (cnt <= 0)
        return;
    std::int32_t* out = static_cast<std::int32_t*>(
        ::operator new(static_cast<std::size_t>(cnt) * 0x10));
    app->TimelineSelectionRecords(band) =
        reinterpret_cast<TimelineSelectionRecord*>(out);
    int recIdx = 2;
    // The original advances recIdx by five and stops when recIdx-2 reaches
    // 0x2710.  recStride already spans five timeline records, so this is
    // 0x2710 / 5 groups rather than 0x2710 groups.  Iterating 0x2710 groups
    // reads five times past every app track allocation.
    for (int i = 0; i < 0x2710 / 5; ++i) {
        for (int k = 0; k < 5; ++k) {
            if (arr[i * recStride + flagOff + k * flagStride] != 0) {
                out[0] = recIdx - 2 + k;
                out[3] = *reinterpret_cast<std::int32_t*>(
                    arr + i * recStride + k * flagStride);
                out += 4;
            }
        }
        recIdx += 5;
    }
    qsort(app->TimelineSelectionRecords(band), static_cast<std::size_t>(cnt), 0x10,
          CompareFunction);
}

// Shared selection-change point (loc_448EEB-0x44A437): dirty flag + click
// anchor + selection-count re-evaluation (0xA03EC region, per-array flag
// counting + record fills + qsort), panel repaint (0x414610) and the
// selection-range reset (0xA05C0-0xA05CC / 0xA05D0).
// (sub_446A70 @ 0x448EEB-0x44A437)
static void SelectionStats(MMDApp* app, int x, int y, HWND hwnd) {
    app->SelectionBoxDragging() = 1;
    app->SelectionBoxAnchorX() = x - 6;
    app->SelectionBoxAnchorY() = y - 0x91;
    if (app->TimelineSelectionChanged() != 0) {
        memset(app->TimelineSelectionRegion(), 0, 0x40);        // 656364 (0xA03EC)
        if (app->state.optflag[0] != 0) {   // 760 (0x2F8)
            // ---- display-mode counts (0x448F2A-0x44902F) ---------------------
            // count set flags in the four record arrays (app+0x374 rigid
            // 0x54-stride flag+0x48, app+0x378 joint 0x28-stride flag+0x24,
            // app+0x37C IK 0x18-stride flag+0x14, app+0x380 morph 0x24-stride
            // flag+0x21; 0x2710 samples each) and in the 255-slot record table
            // app+0x384 (0x3C-stride flag+0x18, 0x2710 samples per slot).
            unsigned char* rarr = reinterpret_cast<unsigned char*>(app->CameraKeys());
            unsigned char* jarr = reinterpret_cast<unsigned char*>(app->LightKeys());
            unsigned char* iarr = reinterpret_cast<unsigned char*>(app->ShadowKeys());
            unsigned char* marr = reinterpret_cast<unsigned char*>(app->GravityKeys());
            for (int off = 0; off < 0x2710; ++off) {
                if (rarr[off * 0x54 + 0x48] != 0)
                    ++app->TimelineSelectionCount(TimelineSelectionBand::Camera);
                if (jarr[off * 0x28 + 0x24] != 0)
                    ++app->TimelineSelectionCount(TimelineSelectionBand::Light);
                if (iarr[off * 0x18 + 0x14] != 0)
                    ++app->TimelineSelectionCount(TimelineSelectionBand::SelfShadow);
                if (marr[off * 0x24 + 0x21] != 0)
                    ++app->TimelineSelectionCount(TimelineSelectionBand::Gravity);
                for (int g = 0; g < 0x33; ++g) {
                    for (int j = 0; j < 5; ++j) {
                        unsigned char* slot = reinterpret_cast<unsigned char*>(
                            app->AccessoryKeys(g * 5 + j));
                        if (slot[off * 0x3C + 0x18] != 0)
                            ++app->TimelineSelectionCount(TimelineSelectionBand::Accessory);
                    }
                }
            }
            // count/ptr pairs: rigid 0xA03EC/0xA03F0, joint 0xA03F4/0xA03F8,
            // IK 0xA03FC/0xA0400, morph 0xA0404/0xA0408
            FillSelectionRecords(app, rarr, 0x1A4, 0x48, 0x54,
                                 TimelineSelectionBand::Camera);
            FillSelectionRecords(app, jarr, 0xC8, 0x24, 0x28,
                                 TimelineSelectionBand::Light);
            FillSelectionRecords(app, iarr, 0x78, 0x14, 0x18,
                                 TimelineSelectionBand::SelfShadow);
            FillSelectionRecords(app, marr, 0xB4, 0x21, 0x24,
                                 TimelineSelectionBand::Gravity);
            // accessory fill (0x4495B5-0x449790): records
            // {0, frameIndex, slotIndex, slot[frameIndex].frame}.  The original
            // keeps the frame index in a separate outer-loop counter and advances
            // the slot index monotonically across all 51 groups.
            if (app->TimelineSelectionCount(TimelineSelectionBand::Accessory) > 0) {
                std::int32_t* out = static_cast<std::int32_t*>(::operator new(
                    static_cast<std::size_t>(app->TimelineSelectionCount(
                        TimelineSelectionBand::Accessory)) * 0x10));
                app->TimelineSelectionRecords(TimelineSelectionBand::Accessory) =
                    reinterpret_cast<TimelineSelectionRecord*>(out);
                for (int off = 0; off < 0x2710; ++off) {
                    for (int g = 0; g < 0x33; ++g) {
                        for (int j = 0; j < 5; ++j) {
                            unsigned char* slot = reinterpret_cast<unsigned char*>(
                                app->AccessoryKeys(g * 5 + j));
                            if (slot[off * 0x3C + 0x18] != 0) {
                                out[1] = off;
                                out[2] = g * 5 + j;
                                out[3] = *reinterpret_cast<std::int32_t*>(slot + off * 0x3C);
                                out += 4;
                            }
                        }
                    }
                }
                qsort(app->TimelineSelectionRecords(TimelineSelectionBand::Accessory),
                      static_cast<std::size_t>(app->TimelineSelectionCount(
                          TimelineSelectionBand::Accessory)),
                      0x10, CompareFunction);
            }
            // 0x4495BD jle
        } else {
            // ---- edit-mode stats (loc_4497A1-0x44A426) ------------------------
            // count the 6 bone display flags (model+0x26E0, 0x168-stride,
            // flags at +0x38+i*0x3C) into 0xA0414
            std::int32_t& boneCnt =
                app->TimelineSelectionCount(TimelineSelectionBand::ModelIk);
            boneCnt = 0;
            unsigned char* m = ActiveModel(app);
            if (m != nullptr) {
                mikudancestudio::mdl::BoneKey* bones = mikudancestudio::mdl::BoneKeys(m);
                for (std::size_t i = 0; i < mikudancestudio::mdl::kBoneKeyCapacity; ++i)
                    if (bones[i].allocated != 0) ++boneCnt;
                if (boneCnt > 0) {
                    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);      // 400
                    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);     // 401
                    // keyframe-cluster append on the active model (0x449886-0x449A5A)
                    auto* record = mikudancestudio::mdl::Mdl(m);
                    record->undoDirty = 1;
                    record->redoDirty = 0;
                    std::uint32_t& kf = record->undoState[0];
                    kf = kf + 1;
                    if (kf >= 0x1E)
                        kf = 0;
                    record->undoState[1] = kf;
                    auto& undo = record->undoRings[0].slots[kf];
                    undo.operation = 2;
                    undo.frame = app->state.currentFrame;
                    auto*& bufSlot = undo.bonePose;
                    if (bufSlot != nullptr) {
                        ::operator delete(bufSlot);
                        bufSlot = nullptr;
                    }
                    const std::int32_t boneN = mikudancestudio::mdl::Mdl(m)->boneCount;
                    bufSlot = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
                        ::operator new(static_cast<std::size_t>(boneN) *
                                       sizeof(mikudancestudio::mdl::BonePoseSnapshot)));
                    memset(bufSlot, 0, static_cast<std::size_t>(boneN) *
                                           sizeof(mikudancestudio::mdl::BonePoseSnapshot));
                    mikudancestudio::mdl::BoneRecord* srcBase = mikudancestudio::mdl::Bones(m);
                    unsigned char* flagMap = mikudancestudio::mdl::Mdl(m)->bonePhysicsState;
                    for (int i = 0; i < boneN; ++i) {
                        auto& out = bufSlot[i];
                        out.boneIndex = 0;
                        memcpy(out.position, srcBase[i].trans, sizeof out.position);
                        memcpy(out.rotation, srcBase[i].rotQuat, sizeof out.rotation);
                        out.physicsDisabled = flagMap[i];
                    }
                    // second keyframe cluster + data buffer (0x449BB5-0x449CC2)
                    undo.dirty = 0;
                    void*& dataSlot = undo.auxiliaryPose;
                    if (dataSlot != nullptr) {
                        ::operator delete(dataSlot);
                        dataSlot = nullptr;
                    }
                    dataSlot = ::operator new(static_cast<std::size_t>(boneCnt) * 0x40);
                    memset(dataSlot, 0, static_cast<std::size_t>(boneCnt) * 0x40);
                    memset(mikudancestudio::mdl::Mdl(m)->keyVisitMap, 0,
                            sizeof(mikudancestudio::mdl::Mdl(m)->keyVisitMap));
                    // feed every set bone flag to 0x49D410 (0x449CF0-0x449D1C)
                    for (std::size_t i = 0; i < mikudancestudio::mdl::kBoneKeyCapacity; ++i)
                        if (bones[i].allocated != 0)
                            AppendBoneKeyToUndo(ActiveModel(app), static_cast<int>(i));
                    // bone-temp records (0x449D1E-0x449F53): 6 flags per 0x168-stride
                    // record -> 0xA0414/0xA0418, qsorted
                    std::int32_t* bout = static_cast<std::int32_t*>(
                        ::operator new(static_cast<std::size_t>(boneCnt) * 0x10));
                    app->TimelineSelectionRecords(TimelineSelectionBand::ModelIk) =
                        reinterpret_cast<TimelineSelectionRecord*>(bout);
                    int idx = 2;
                    for (std::size_t i = 0; i < mikudancestudio::mdl::kBoneKeyCapacity / 6; ++i) {
                        for (int k = 0; k < 6; ++k) {
                            mikudancestudio::mdl::BoneKey& key = bones[i * 6 + k];
                            if (key.allocated != 0) {
                                bout[0] = idx - 2 + k;
                                bout[3] = static_cast<std::int32_t>(key.frame);
                                bout += 4;
                            }
                        }
                        idx += 6;
                    }
                    qsort(app->TimelineSelectionRecords(TimelineSelectionBand::ModelIk),
                          static_cast<std::size_t>(boneCnt),
                          0x10, CompareFunction);
                }
                // morph-temp records (0x449F56-0x44A1C2): 5 flags per 0x64-stride
                // record -> 0xA041C/0xA0420, qsorted
                std::int32_t& morphCnt =
                    app->TimelineSelectionCount(TimelineSelectionBand::ModelMorph);
                morphCnt = 0;
                {
                    unsigned char* mm = ActiveModel(app);
                    mikudancestudio::mdl::MorphKey* morphs = mikudancestudio::mdl::MorphKeys(mm);
                    for (std::size_t i = 0; i < mikudancestudio::mdl::kMorphKeyCapacity; ++i)
                        if (morphs[i].allocated != 0) ++morphCnt;
                    if (morphCnt > 0) {
                        std::int32_t* mout = static_cast<std::int32_t*>(
                            ::operator new(static_cast<std::size_t>(morphCnt) * 0x10));
                        app->TimelineSelectionRecords(TimelineSelectionBand::ModelMorph) =
                            reinterpret_cast<TimelineSelectionRecord*>(mout);
                        int idx = 2;
                        for (int i = 0; i < 0xFA0; ++i) {
                            for (int k = 0; k < 5; ++k) {
                                mikudancestudio::mdl::MorphKey& key = morphs[i * 5 + k];
                                if (key.allocated != 0) {
                                    mout[0] = idx - 2 + k;
                                    mout[3] = static_cast<std::int32_t>(key.frame);
                                    mout += 4;
                                }
                            }
                            idx += 5;
                        }
                        qsort(app->TimelineSelectionRecords(TimelineSelectionBand::ModelMorph),
                              static_cast<std::size_t>(morphCnt),
                              0x10, CompareFunction);
                    }
                }
                // IK-temp records (0x44A1C2-0x44A426): 5 flags per 0x8C-stride
                // record -> 0xA0424/0xA0428, qsorted at loc_44A426
                std::int32_t& ikCnt =
                    app->TimelineSelectionCount(TimelineSelectionBand::ModelBone);
                ikCnt = 0;
                {
                    unsigned char* mm = ActiveModel(app);
                    mikudancestudio::mdl::DisplayKey* displayKeys = mikudancestudio::mdl::DisplayKeys(mm);
                    for (std::size_t i = 0; i < mikudancestudio::mdl::kDisplayKeyCapacity; ++i)
                        if (displayKeys[i].allocated != 0) ++ikCnt;
                    if (ikCnt > 0) {
                        std::int32_t* iout = static_cast<std::int32_t*>(
                            ::operator new(static_cast<std::size_t>(ikCnt) * 0x10));
                        app->TimelineSelectionRecords(TimelineSelectionBand::ModelBone) =
                            reinterpret_cast<TimelineSelectionRecord*>(iout);
                        int idx = 2;
                        for (int i = 0; i < 0xC8; ++i) {       // 0x6D60/0x8C
                            for (int k = 0; k < 5; ++k) {
                                mikudancestudio::mdl::DisplayKey& key = displayKeys[i * 5 + k];
                                if (key.allocated != 0) {
                                    iout[0] = idx - 2 + k;
                                    iout[3] = static_cast<std::int32_t>(key.frame);
                                    iout += 4;
                                }
                            }
                            idx += 5;
                        }
                    }
                }
                // fall into the shared qsort site at loc_44A426 with the IK pair.
                qsort(app->TimelineSelectionRecords(TimelineSelectionBand::ModelBone),
                      static_cast<std::size_t>(
                          app->TimelineSelectionCount(TimelineSelectionBand::ModelBone)),
                      0x10, CompareFunction);
            }
        }
    }
    // loc_44A437
    PanelPaint(app);                               // 0x414610
    app->ClearTimelineRange();
    SelectionReeval(app);                          // 0x430510
}

// x64-safe home for the eight timeline-selection record pointers - see the
// g_timelineSelectionRecords comment in mmd_app.hpp.
TimelineSelectionRecord* g_timelineSelectionRecords[8] = {};

// ---- HandleLButtonDown region helpers ------------------------------------

// One S5a display-mode cell toggle - the 15-line branch body that the four
// per-band blocks (0x4473DA-0x4479BB: rigid/joint/IK/morph) repeat
// verbatim.  selected != 0: shift clears it, a plain click only marks the
// stats dirty; selected == 0: a plain click first clears all selection
// bitmaps, a shift-click sets it without the clear.
static void ToggleTimelineCell(MMDApp* app, bool shiftActive,
                               std::uint8_t& selected) {
    if (selected != 0) {
        if (shiftActive)
            selected = 0;
        else
            app->TimelineSelectionChanged() = 1;
    } else if (!shiftActive) {
        ClearSelectionBitmaps(app);
        selected = 1;
    } else {
        selected = 1;
    }
}

// S5a band table: the four structurally identical band toggles differ only
// in {y-range, row-hit table, track array}, so they collapse into the
// bands[] loop in HandleLButtonDown_TimelineAreaHit.  The y-ranges are
// disjoint and contiguous ([0xA0, 0xD8) in 14px steps), so at most one
// entry matches - the same one-block-executes invariant as the original
// if-chain.  The fifth band (accessory 2D grid, y >= 0xD8) has its own
// index math and stays separate.
struct TimelineBandHit {
    int yMin;
    int yMax;
    const std::int32_t* rowHits;              // state.rowHitBand0..3
    std::uint8_t& (*keySelected)(MMDApp*, int);
};

static std::uint8_t& CameraKeySelected(MMDApp* app, int i) {
    return app->CameraKeys()[i].selected;
}
static std::uint8_t& LightKeySelected(MMDApp* app, int i) {
    return app->LightKeys()[i].selected;
}
static std::uint8_t& ShadowKeySelected(MMDApp* app, int i) {
    return app->ShadowKeys()[i].selected;
}
static std::uint8_t& GravityKeySelected(MMDApp* app, int i) {
    return app->GravityKeys()[i].selected;
}

// S6 stage-toggle helpers.  The three edit-mode stages (display/morph/bone
// keys, loc_447A4D-0x448EEB) repeat two branch shapes over three key
// families:
//   type > 0 / type == -10 : the allocated-toggle below (6 copies in the
//                            original, differing only in key family/index)
//   type == -1 / type == -2: the timeline-range rows below (4 copies,
//                            morph/bone stages only)
// Both range rows address [type*sizeof(Key), +1] in the key array; -1
// marks the range apply-enabled under shift (else just stats-dirty), -2
// always sets the range and clears the model flags unless shift is held.
// NOTE: unlike the original, the non-shift -1 row evaluates the key-array
// pointer (a pure read of the active-slot table) before branching -
// behaviour is identical.
template <typename KeyT>
static void ToggleModelKeyAllocated(MMDApp* app, KeyT& key,
                                    bool shiftActive) {
    if (key.allocated != 0) {
        if (shiftActive)
            key.allocated = 0;
        else
            app->TimelineSelectionChanged() = 1;
    } else if (shiftActive) {
        key.allocated = 1;
    } else {
        ClearActiveModelFlags(app);
        key.allocated = 1;
    }
}

template <typename KeyT>
static void StageRangeRow(MMDApp* app, std::int32_t type, KeyT* keys,
                          bool shiftActive) {
    if (type == -1) {                          // loc_4483B8 / loc_448AB8
        if (shiftActive) {
            SetTimelineSelectionRange(app, type * sizeof(*keys), keys,
                                      type * sizeof(*keys) + 1,
                                      reinterpret_cast<unsigned char*>(keys) + 1);
            app->TimelineRangeApplyEnabled() = 1;
        } else {
            app->TimelineSelectionChanged() = 1;
        }
    } else {                                   // type == -2, loc_4483F1/0x448AF1
        SetTimelineSelectionRange(app, type * sizeof(*keys), keys,
                                  type * sizeof(*keys) + 1,
                                  reinterpret_cast<unsigned char*>(keys) + 1);
        if (!shiftActive)
            ClearActiveModelFlags(app);
    }
}

// ---- S2 (VA 0x446B01-0x446B9C): margin column (X in (4,20)) ---------------
// Row-type display toggle, edit mode only; entered with the row gates
// already checked.  Row index = (y-0xA0)/14 (magic 0x92492493, sar 3); row
// type byte at model+0x2DC0 indexes a 0x65-stride table (model+0x26D0)
// whose flag at +0x64 is toggled.
static void HandleLButtonDown_MarginColumnToggle(MMDApp* app,
                                                 std::int32_t y) {
    if (app->state.optflag[0] == 0) {  // 760 (0x2F8)
        unsigned char* model = app->SelectedModel();
        if (model == nullptr)
            return;
        const std::int32_t type =
            static_cast<std::int8_t>(
                mdl::Mdl(model)->boneListRowType[Div14(y - 0xA0)]);
        if (type > 0) {
            mdl::DisplayGroup& frame = mdl::DisplayGroups(model)[type];
            frame.flags = (frame.flags == 0) ? 1 : 0;   // cmp/setz toggle
            PostLanguageSweep(app);             // 0x42F1E0
        }
    }
    return;
}

// ---- S3+S4 (VA 0x446BA1-0x44738F): name column (X in (20,95]) --------------
// Bone/morph row selection; entered with the row gates already checked.
// Edit mode selects the clicked row (row table model+0x2E88, special morph
// rows encoded as -1-idx, -999 = "bottom" morph row) and dispatches the
// row-type handlers (model+0x2DC0 byte); display mode goes to the S4
// sub-branch below.
static void HandleLButtonDown_NameColumnHit(MMDApp* app, HWND hwnd,
                                            std::int32_t y) {
    if (app->state.optflag[0] != 0) {  // 760 (0x2F8)
        // ---- S4: display-mode name column sub-branch (loc_44719F) ---
        if (!app->ShiftModifierActive()) {
            // clear the accessory-row selection flag of all 255 accessory
            // slots (the original walks it unrolled, five 4-byte pointers
            // per 0x14 step; on x64 the pointer array is indexed directly
            // through ObjectSlot)
            for (int i = 0; i < 255; ++i) {
                mdl::AccessoryRecord* accessory =
                    static_cast<mdl::AccessoryRecord*>(app->ObjectSlot(i));
                if (accessory != nullptr)
                    accessory->rowSelected = 0;
            }
            app->ClearGlobalTimelineTrackSelection();  // 0xA03E4..0xA03E7
        }
        // loc_447210: column-band toggles (band selected by this+8) - four
        // isomorphic {y-range, global-track} blocks merged into the table
        // below; the y-ranges are disjoint/contiguous ([0xA0, 0xD8) in 14px
        // steps), so at most one entry matches, exactly like the original
        // if-chain.
        static const struct {
            int yMin, yMax;
            GlobalTimelineTrack track;
        } kColumnBands[4] = {
            {0xA0, 0xAE, GlobalTimelineTrack::Camera},      // bone band
            {0xAE, 0xBC, GlobalTimelineTrack::Light},       // morph band
            {0xBC, 0xCA, GlobalTimelineTrack::SelfShadow},  // IK band
            {0xCA, 0xD8, GlobalTimelineTrack::Gravity},     // rigid band
        };
        for (const auto& band : kColumnBands) {
            if (y >= band.yMin && y < band.yMax) {
                app->GlobalTrackSelected(band.track) =
                    app->GlobalTrackSelected(band.track) == 0 ? 1 : 0;
                PostLanguageSweep(app);
                return;
            }
        }
        {                                                        // accessory rows (y >= 0xD8)
            const std::int32_t jrow = Div14(y - 0xD8);
            const std::int32_t jidx = app->state.jointLineMap[jrow];
            if (jidx >= 0) {
                mdl::AccessoryRecord* accessory = static_cast<
                    mdl::AccessoryRecord*>(app->ObjectSlot(jidx));
                if (accessory->rowSelected != 0) {
                    accessory->rowSelected = 0;
                    PostLanguageSweep(app);
                    PostLanguageSweep2(app);
                } else {
                    accessory->rowSelected = 1;
                    app->state.selectedObjectSlot =
                        static_cast<std::uint8_t>(jidx);
                    PostLanguageSweep(app);
                    SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo), CB_SETCURSEL,
                                 accessory->order, 0);
                    SyncAccessoryEditPanel(app);
                    PostLanguageSweep2(app);
                }
            } else {
                PostLanguageSweep2(app);
            }
            return;
        }
    }

    // ---- edit-mode name column: clear stale selection state ---------
    // (0x446BC7-0x446C8A)
    if (ActiveModel(app) == nullptr)
        return;
    if (!app->ShiftModifierActive()) {
        unsigned char* m0 = ActiveModel(app);
        const std::int32_t boneCount =
            mikudancestudio::mdl::Mdl(m0)->boneCount;
        for (int i = 0; i < boneCount; ++i) {
            unsigned char* mm = ActiveModel(app);
            mikudancestudio::mdl::Mdl(mm)->boneSelection[i] = 0;
        }
        for (int i = 0;
             i < static_cast<int>(mdl::Mdl(m0)->facialFrameCount); ++i) {
            unsigned char* mm = ActiveModel(app);
            mdl::DisplayFrames(mm)[i].selected = 0;
        }
        mikudancestudio::mdl::Mdl(ActiveModel(app))->displayKeyframesPresent = 0;
    }

    // loc_446C8A: row lookup + selection toggle
    {
        unsigned char* m = ActiveModel(app);
        const std::int32_t row = Div14(y - 0xA0);
        const std::int32_t idx =
            mikudancestudio::mdl::Mdl(m)->boneListRowRecord[row];
        if (idx >= 0) {
            unsigned char* p = mikudancestudio::mdl::Mdl(m)->boneSelection + idx;
            if (*p != 0) {
                *p = 0;
            } else {
                *p = 1;
                mikudancestudio::mdl::Mdl(m)->selectedBone = idx;
            }
            // via 0x44702D
        } else if (idx == -999) {                       // 0xFFFFFC19
            const std::int32_t want = -1 - idx;
            const std::int32_t morphCount =
                static_cast<std::int32_t>(mdl::Mdl(m)->facialFrameCount);
            int i = 0;
            for (; i < morphCount; ++i) {
                if (mdl::DisplayFrames(m)[i].targetIndex == want)
                    break;
            }
            if (i < morphCount) {
                // morph row found: toggle its flag, then sync the bone
                // name combo (controls 0x1F8..0x209 by bone row type)
                mdl::FrameGroup& faceRec = mdl::DisplayFrames(m)[i];
                if (faceRec.selected != 0) {
                    faceRec.selected = 0;
                    // via 0x44702D
                } else {
                    faceRec.selected = 1;
                    const std::int32_t morphIndex = faceRec.targetIndex;
                    auto& morph = mikudancestudio::mdl::Morphs(m)[morphIndex];
                    const auto panel = morph.panel;
                    HWND hCombo = nullptr, hSpin = nullptr, hText = nullptr;
                    switch (panel) {                         // 0x446D99-0x446E77
                        case mikudancestudio::mdl::MorphPanel::eyebrow:
                            hCombo = GetDlgItem(hwnd, panel::kMorphCombo0);
                            hSpin = GetDlgItem(hwnd, panel::kMorphSlider0);
                            hText = GetDlgItem(hwnd, panel::kMorphEdit0);
                            break;
                        case mikudancestudio::mdl::MorphPanel::eye:
                            hCombo = GetDlgItem(hwnd, panel::kMorphCombo1);
                            hSpin = GetDlgItem(hwnd, panel::kMorphSlider1);
                            hText = GetDlgItem(hwnd, panel::kMorphEdit1);
                            break;
                        case mikudancestudio::mdl::MorphPanel::mouth:
                            hCombo = GetDlgItem(hwnd, panel::kMorphCombo2);
                            hSpin = GetDlgItem(hwnd, panel::kMorphSlider2);
                            hText = GetDlgItem(hwnd, panel::kMorphEdit2);
                            break;
                        case mikudancestudio::mdl::MorphPanel::other:
                            hCombo = GetDlgItem(hwnd, panel::kMorphCombo3);
                            hSpin = GetDlgItem(hwnd, panel::kMorphSlider3);
                            hText = GetDlgItem(hwnd, panel::kMorphEdit3);
                            break;
                        // default: original falls through with stale stack
                        // handles; nullptr here is the safe equivalent.
                    }
                    // Preserve the active morph separately for each panel.
                    const auto panelIndex = static_cast<unsigned>(panel) - 1;
                    mikudancestudio::mdl::Mdl(m)->selectedMorphs[panelIndex] = morphIndex;
                    // sync the combo cursor with the morph name
                    // (CB_GETCOUNT 0x146 / CB_GETLBTEXT 0x148 / CB_SETCURSEL
                    // 0x14E, 0x446EC0-0x446FAE)
                    const LRESULT count = SendMessageA(hCombo, CB_GETCOUNT, 0, 0);
                    char nameBuf[0x64];
                    for (LONG i2 = 0; i2 < count; ++i2) {
                        SendMessageA(hCombo, CB_GETLBTEXT, i2,
                                     reinterpret_cast<LPARAM>(nameBuf));
                        if (MatchText(nameBuf, morph.name) ||
                            MatchText(nameBuf, morph.nameEn)) {
                            SendMessageA(hCombo, CB_SETCURSEL, i2, 0);
                        }
                    }
                    // Morph value spin/text sync (0x446FB4-0x447027):
                    // spin gets (int)(value*100.0), text gets "%5.4f"
                    const float v = mikudancestudio::mdl::Morphs(m)[
                        mikudancestudio::mdl::Mdl(m)->selectedMorphs[panelIndex]].value;
                    SendMessageA(hSpin, TBM_SETPOS, 1,
                                 static_cast<LPARAM>(static_cast<std::int32_t>(v * 100.0)));
                    char valBuf[0x64];
                    sprintf_s(valBuf, 0x64u, "%5.4f", static_cast<double>(v));
                    SetWindowTextA(hText, valBuf);
                }
            }
        }
    }

    // loc_447034: row-type dispatch after selection (entry point for
    // the selection paths above)
    {
        unsigned char* m = ActiveModel(app);
        const std::int32_t type =
            static_cast<std::int8_t>(
                mikudancestudio::mdl::Mdl(m)->boneListRowType[Div14(y - 0xA0)]);
        if (type == 1) {
            mdl::Mdl(m)->displayKeyframesPresent =
                (mdl::Mdl(m)->displayKeyframesPresent == 0) ? 1 : 0;  // setz
            PostLanguageSweep(app);
            PostLanguageSweep2(app);
            return;
        }
        if (type == 2) {
            for (int i = 0;
                 i < static_cast<int>(mdl::Mdl(m)->facialFrameCount); ++i) {
                mdl::FrameGroup& face = mdl::DisplayFrames(m)[i];
                face.selected = (face.selected == 0) ? 1 : 0;
            }
            PostLanguageSweep(app);
            PostLanguageSweep2(app);
            return;
        }
        if (type <= 0) {                                 // jle loc_447384
            PostLanguageSweep2(app);
            return;
        }
        // Rigid-group rows (x86 0x4470FA..0x44718A / x64 0x7FF7CB45A379..):
        // walk the 46-byte (0x2E) FrameGroup records at model+0x26D8, and
        // for each record whose type byte (record+0x28, read signed like
        // the original movsx) matches the clicked row type, toggle the
        // selection flag of the bone named by the uint16 at record+0x2A.
        for (int i = 0;
             i < static_cast<int>(mdl::Mdl(m)->rigidBodyCount); ++i) {
            mdl::FrameGroup& rigid = mdl::RigidGroups(m)[i];
            if (static_cast<std::int8_t>(rigid.groupIndex) == type) {
                const std::int32_t boneIdx = rigid.targetIndex;
                unsigned char* p =
                    mikudancestudio::mdl::Mdl(m)->boneSelection + boneIdx;
                if (*p != 0)
                    *p = 0;
                else
                    *p = 1;
            }
        }
        PostLanguageSweep(app);
        PostLanguageSweep2(app);
        return;
    }
}

// ---- S5-S6 (VA 0x447390-0x448EEB): main area (X in (95, sidebar-18)) --------
// Timeline cell hits, entered with the row gates already checked.  Display
// mode (S5a) toggles keys in the five timeline bands; edit mode (S6) runs
// the three-stage row-type chain.  Both converge on SelectionStats
// (loc_448EEB).
static void HandleLButtonDown_TimelineAreaHit(MMDApp* app, HWND hwnd,
                                              std::int32_t x,
                                              std::int32_t y) {
    if (app->PlaybackActive() != 0)
        return;
    app->TimelineSelectionChanged() = 0;
    if (app->state.optflag[0] != 0) {  // 760 (0x2F8)
        // ---- S5a: display-mode selection columns --------------------
        // (0x4473DA-0x4479BB) band selected by this+8, row by
        // (x-100)/13 (magic 0x4EC4EC4F, sar 2).  Selection indices are
        // latched into locals (rigid/joint/ik/morph); when none of the
        // five bands hit and mode != 3 the four selection bitmaps are
        // cleared wholesale (0x447990-0x447A48).
        const std::int32_t rowD = Div13(x - 0x64);
        std::int32_t bandIdx = -1;  // var_194 / hWnd var / var_198 / wParam var
        std::int32_t joint2dIdx = -1;                        // var_18C
        const bool shiftActive = app->ShiftModifierActive();
        // four isomorphic band toggles (0x4473DA-0x4479BB), table-driven:
        // rigid/joint/IK/morph differ only in {y-range, row-hit table,
        // track array}.  At most one band matches; the shared bandIdx latch
        // therefore equals the original four latches all staying -1.
        const TimelineBandHit bands[4] = {
            {0xA0, 0xAE, app->state.rowHitBand0, CameraKeySelected},  // rigid
            {0xAE, 0xBC, app->state.rowHitBand1, LightKeySelected},   // joint
            {0xBC, 0xCA, app->state.rowHitBand2, ShadowKeySelected},  // IK
            {0xCA, 0xD8, app->state.rowHitBand3, GravityKeySelected}, // morph
        };
        for (const TimelineBandHit& band : bands) {
            if (y < band.yMin || y >= band.yMax)
                continue;
            bandIdx = band.rowHits[rowD];
            if (bandIdx >= 0)
                ToggleTimelineCell(app, shiftActive,
                                   band.keySelected(app, bandIdx));
        }
        if (y >= 0xD8) {                                     // joint 2D band
            const std::int32_t jrow = Div14(y - 0xD8);
            const std::int32_t slotIdx =
                app->state.jointLineMap[jrow];
            const std::int32_t col = Div13(x - 0x64);
            joint2dIdx =
                app->state.rowHitAcc[col * 0xC8 + jrow];
            if (joint2dIdx >= 0) {
                unsigned char* slot = reinterpret_cast<unsigned char*>(
                    app->AccessoryKeys(slotIdx));
                unsigned char* rec = slot + joint2dIdx * 0x3C + 0x18;
                if (*rec != 0) {
                    if (shiftActive)
                        *rec = 0;
                    else
                        app->TimelineSelectionChanged() = 1;
                } else if (!shiftActive) {
                    ClearSelectionBitmaps(app);
                    *rec = 1;
                } else {
                    *rec = 1;
                }
            }
        }
        // loc_447990: no band hit at all -> wholesale bitmap clear
        if (bandIdx == -1 && joint2dIdx == -1 && !shiftActive) {
            ClearSelectionBitmaps(app);
        }
        SelectionStats(app, x, y, hwnd);       // loc_448EEB
        return;
    }
    // ---- S6: edit-mode generic selection chain (loc_447A4D-0x448EEB)
    // Three 2D-table row-type stages (app+0x4EB84 -> IK-type flags,
    // app+0x27A84 -> morph-type flags, app+0x984 -> bone-type flags),
    // each toggling one flag of the active model; the shared
    // selection-change point (loc_448EEB) runs the selection stats.
    else {
        if (ActiveModel(app) == nullptr)
            return;
        const bool shiftActive = app->ShiftModifierActive();
        const std::int32_t row2d = Div13(x - 0x64) * 0xC8 + Div14(y - 0xA0);
        // stage 1 (loc_447A4D): IK-type flags, model+0x26E8 0x1C-stride
        // records with flag at +0x14
        const std::int32_t ikType = app->state.rowHitIk[row2d];
        if (ikType > 0) {
            ToggleModelKeyAllocated(app,
                mikudancestudio::mdl::DisplayKeys(ActiveModel(app))[ikType],
                shiftActive);
        } else if (ikType == -10) {                 // loc_447CD9 (row 0)
            ToggleModelKeyAllocated(app,
                mikudancestudio::mdl::DisplayKeys(ActiveModel(app))[0],
                shiftActive);
        }
        // stage 2 (loc_447F03): morph-type flags, model+0x26E4
        // 0x14-stride records with flag at +0x10
        const std::int32_t morphType = app->state.rowHitMorph[row2d];
        if (morphType > 0) {
            ToggleModelKeyAllocated(app,
                mikudancestudio::mdl::MorphKeys(ActiveModel(app))[morphType],
                shiftActive);
        } else if (morphType == -10) {              // loc_448189 (row 0)
            ToggleModelKeyAllocated(app,
                mikudancestudio::mdl::MorphKeys(ActiveModel(app))[0],
                shiftActive);
        } else if (morphType == -1) {               // loc_4483B8
            StageRangeRow(app, morphType,
                          mikudancestudio::mdl::MorphKeys(ActiveModel(app)),
                          shiftActive);
        } else if (morphType == -2) {               // loc_4483F1
            StageRangeRow(app, morphType,
                          mikudancestudio::mdl::MorphKeys(ActiveModel(app)),
                          shiftActive);
        }
        // stage 3 (loc_4485FB): bone-type flags, model+0x26E0 0x3C flag
        // stride with flag at +0x38
        const std::int32_t boneType = app->state.rowHitBone[row2d];
        if (boneType > 0) {
            ToggleModelKeyAllocated(app,
                mikudancestudio::mdl::BoneKeys(ActiveModel(app))[boneType],
                shiftActive);
        } else if (boneType == -10) {               // loc_448889 (row 0)
            ToggleModelKeyAllocated(app,
                mikudancestudio::mdl::BoneKeys(ActiveModel(app))[0],
                shiftActive);
        } else if (boneType == -1) {                // loc_448AB8
            StageRangeRow(app, boneType,
                          mikudancestudio::mdl::BoneKeys(ActiveModel(app)),
                          shiftActive);
        } else if (boneType == -2) {                // loc_448AF1
            StageRangeRow(app, boneType,
                          mikudancestudio::mdl::BoneKeys(ActiveModel(app)),
                          shiftActive);
        }
        // loc_448CFB: none of the three stages hit -> wholesale
        // active-model flag clear (unless mode == 3)
        if (ikType == 0 && morphType == 0 && boneType == 0 && !shiftActive)
            ClearActiveModelFlags(app);
        SelectionStats(app, x, y, hwnd);           // loc_448EEB
        return;
    }
}

// ---- S7 (VA 0x44A468-0x44A850): frame header strip (y in (144,160)) ---------
// Reached when the main-area gate fails; the strip gate in the original is
// (x < sidebar-0x12) && y in (0x90, 0xA0) && x > 0x64.  Snapshots edited
// bones, seeks every model to the clicked frame, then repaints and resyncs
// physics/audio.
static void HandleLButtonDown_FrameStripHit(MMDApp* app, HWND hwnd,
                                            std::int32_t x, std::int32_t y,
                                            std::int32_t sidebar) {
    if (app->PlaybackActive() != 0)
        return;
    app->CameraAttachmentTransformSuppressed() = 0;
    // Snapshot every model with edited bones, then clear its edit flags.
    for (int i = 0; i < kModelSlotCount; ++i) {
        unsigned char* model = ModelAtSlot(app, i);
        if (model == nullptr)
            continue;
        mdl::ModelRecord* modelRecord = mdl::Mdl(model);
        const std::int32_t n =
            static_cast<std::int32_t>(modelRecord->boneCount);
        if (n <= 0)
            continue;
        unsigned char* flags = modelRecord->bonePhysicsState;
        int j = 0;
        for (; j < n; ++j)
            if (flags[j] != 0)
                break;
        if (j < n) {
            // original: sub_4A0080(ecx = model, frame = app+0x980)
            SnapshotPoseBeforeFrameChange(model, app->state.currentFrame);
            modelRecord = mdl::Mdl(model);
            flags = modelRecord->bonePhysicsState;
            for (j = 0; j < n; ++j)
                flags[j] = 0;
        }
    }
    // scrollbar 0x1A1 sync (0x44A520-0x44A5BA): frame =
    // (x-100)/13 + scroll offset; EM_SETSEL (0xB1) to the end of the old
    // text, then WM_SETTEXT (0xC2) with the new frame
    app->state.currentFrame =
        Div13(x - 0x64) + app->state.timelineStartFrame;
    const LRESULT textLen = GetWindowTextLengthA(GetDlgItem(hwnd, panel::kCurrentFrameEdit));
    SendMessageA(GetDlgItem(hwnd, panel::kCurrentFrameEdit), EM_SETSEL, 0, textLen);
    char frameText[0x100];
    sprintf_s(frameText, 0x100u, "%d",
              app->state.currentFrame);
    SendMessageA(GetDlgItem(hwnd, panel::kCurrentFrameEdit), EM_REPLACESEL, 0,
                 reinterpret_cast<LPARAM>(frameText));
    // per-slot frame apply + active-slot panel sync (0x44A5C8-0x44A606)
    for (int i = 0; i < kModelSlotCount; ++i) {
        unsigned char* model = ModelAtSlot(app, i);
        if (model == nullptr)
            continue;
        // original: sub_4B4260(ecx = model, frame = app+0x980,
        //            app+0xA0CC4)
        SeekModelFrame(model,
                  app->state.currentFrame,
                  app->PlaybackPhysicsMode());
        if (i == static_cast<int>(app->SelectedModelSlot())) {
            SyncModelEditControls(model);
        }
    }
    // mode branch (0x44A608-0x44A72B)
    if (app->state.optflag[0] != 0) {  // 760 (0x2F8)
        ReloadModels(app);                            // 0x42E640
        RefreshLightPanel(app);                               // 0x411070
        RefreshSelfShadowPanel(app);                               // 0x411B90
        ApplyGravityTrack(app);                               // 0x412330
        for (int i = 0; i < 0xFF; ++i) {
            if (app->ObjectSlot(i) != nullptr)
                ApplyAccessoryTrack(app, i);                    // 0x413120
        }
        SyncAccessoryEditPanel(app);                               // 0x4134E0
    } else if (app->state.followCameraEnabled != 0) {  // 650648
        app->ViewOffsetX() = 0.0f;
        app->ViewOffsetY() = 0.0f;
        ReloadModels(app);
        RefreshLightPanel(app);
        RefreshSelfShadowPanel(app);
        ApplyGravityTrack(app);
        for (int i = 0; i < 0xFF; ++i) {
            if (app->ObjectSlot(i) != nullptr)
                ApplyAccessoryTrack(app, i);
        }
        SyncAccessoryEditPanel(app);
        const std::int32_t sel =
            app->CameraParentModel();
        if (sel >= 0) {
            unsigned char* selModel = ModelAtSlot(app, sel);
            // original: sub_4970B0(ecx = model at slot sel)
            ModelApplyMorphs(selModel);                        // 0x4970B0
            SetPhysicsMode(selModel, 0, app->ModelSlots(),
                           app->PlaybackPhysicsMode());                    // 0x4A9220
        }
        PostModelReload(app);                         // 0x41A650
    } else {
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);  // 400
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE); // 401
    }
    // scroll clamp + panel repaint (0x44A72D-0x44A766):
    // 0x97C = max(0, 0x980 - (sidebar-0x54)/26) with an unsigned compare
    const std::int32_t pages = (sidebar - 0x54) / 26;
    const std::uint32_t frameU =
        static_cast<std::uint32_t>(app->state.currentFrame);
    if (frameU <= static_cast<std::uint32_t>(pages))  // unsigned compare (jbe)
        app->state.timelineStartFrame = 0;
    else
        app->state.timelineStartFrame =
            static_cast<std::int32_t>(frameU - static_cast<std::uint32_t>(pages));
    PanelPaint(app);                                  // 0x414610
    // timeline strip redraw + physics sync (0x44A768-0x44A850)
    if (app->state.waveEnabled != 0) {  // 657100 (0xA06CC)
        TimelineDrawTicks(app->state.timelineStartFrame,
                          app->SidebarWidth());
        RECT rc2;
        rc2.left = 6;
        rc2.top = 95;
        rc2.right = sidebar - 3;
        rc2.bottom = 146;
        InvalidateRect(hwnd, &rc2, 0);
        if (app->state.wavPlaysOnFrameMove != 0) {  // 655766 (0xA0196)
            // gate flag read BEFORE the 0xA02B6 store (asm zf capture)
            const bool gate =
                app->state.automaticFrameAdvanceEnabled == 0;  // 656361 (0xA03E9)
            app->AudioSeekReady() = 1;  // 656054 (0xA02B6)
            if (gate) {
                SetFrameNormalized(app->FrameNormalization()); // 0x4C2B80
                const std::int32_t frameMinus1 =
                    app->state.currentFrame - 1;
                // fild + (negative ? fadd 2^32f) + fdiv 30.0 ==
                // (double)(unsigned)frameMinus1 / 30.0
                double t = static_cast<double>(static_cast<std::uint32_t>(frameMinus1)) / 30.0;
                if (t < 0.0)                          // fldz/fcom clamp
                    t = 0.0;
                // original: sub_4C3530(ecx = app+0xCC subsystem, t)
                WaveRestartAt(app->Audio(), t);
            }
        }
    }
    if (app->state.aviBackgroundEnabled == 1)  // 2332 (0x91C)
        AviBgOverlayRefresh(app);                               // 0x4168D0
    app->PhysicsResetPending() = 1;
    return;
}

// ---- S7b (VA 0x44A855-0x44A97D): bone/morph name-row rects ------------------
// Bone rect: X in (9DA05+3, 9DA05+13), Y in (9DA06+bottom-0x8C,
// 9DA06+bottom-0x82), gated on byte 9DA04; keyframe insert (0x4A1510)
// + row marker 0x9DA09 = 1.  Morph rect: same with 9DA07/9DA08, marker 2.
// A miss in both rects falls through to the caller's tail (loc_44A97E).
static void HandleLButtonDown_NameRowRectHit(MMDApp* app, std::int32_t x,
                                             std::int32_t y,
                                             std::int32_t bottom) {
    const std::int32_t boneOff =
        static_cast<std::int8_t>(app->state.interpCurveControlCache[1]) + bottom;
    const std::int32_t boneTop =
        static_cast<std::int8_t>(app->state.interpCurveControlCache[0]);
    if (x < boneTop + 0xD && x > boneTop + 3 &&
        y > boneOff - 0x8C && y < boneOff - 0x82 &&
        app->state.interpCurveUniformFound != 0) {
        if (app->PlaybackActive() != 0)
            return;
        unsigned char* m = ActiveModel(app);
        if (m != nullptr) {
            // original: sub_4A1510(ecx = model, frame = app+0x980)
            SnapshotSelectedKeysForUndo(m, app->state.currentFrame);
        }
        app->PendingTimelineSelectionRow() = TimelineSelectionRow::Bone;
        return;
    }
    const std::int32_t morphOff =
        static_cast<std::int8_t>(app->state.interpCurveControlCache[3]) + bottom;
    const std::int32_t morphTop =
        static_cast<std::int8_t>(app->state.interpCurveControlCache[2]);
    if (x < morphTop + 0xD && x > morphTop + 3 &&
        y > morphOff - 0x8C && y < morphOff - 0x82 &&
        app->state.interpCurveUniformFound != 0) {
        if (app->PlaybackActive() != 0)
            return;
        unsigned char* m = ActiveModel(app);
        if (m != nullptr) {
            SnapshotSelectedKeysForUndo(m, app->state.currentFrame);
        }
        app->PendingTimelineSelectionRow() = TimelineSelectionRow::Morph;
        return;
    }
}

void HandleLButtonDown(MMDApp* app) {
    // ---- S1: guard 0xA0274 + client rect + early-out gate ------------
    // (0x446A70-0x446B00) region dispatch follows; each branch returns
    // straight to the shared tail (loc_44A980).
    if (app->FullscreenMode() != 0)
        return;                                              // loc_44A980

    const HWND hwnd = static_cast<HWND>(app->state.hwnd);  // 657080
    RECT rc;
    GetClientRect(hwnd, &rc);
    const std::int32_t y = app->MouseY();
    const std::int32_t x = app->MouseX();
    const std::int32_t sidebar = app->SidebarWidth();

    // early-out: click in the left panel header strip - this+0xC8 = 1,
    // this+0xA442C = 0, then shared tail.  Gates (eval order as in the
    // original): y <= bottom-0x9E(158) && x <= sidebar+6 &&
    // [0xA0D38]==0 && sidebar <= x.
    if (y <= rc.bottom - 0x9E && x <= sidebar + 6 &&
        app->FloatingWindow() == nullptr && sidebar <= x) {
        app->SidebarResizeDragging() = 1;
        app->WindowLayoutReady() = 0;
        return;
    }
    // row gates used by every region below (0x446B01-0x446B3A):
    //   var_1A0 = y < bottom - 0xF8 (248) ; var_190 = y > 0xA0 (160)
    const bool rowGateY = y > 0xA0 && y < rc.bottom - 0xF8;

    // ---- S2: margin column (X in (4,20)): row-type display toggle
    if (rowGateY && x > 4 && x < 0x14) {
        HandleLButtonDown_MarginColumnToggle(app, y);
        return;
    }

    // ---- S3: name column (X in (20,95]): bone/morph row selection
    if (rowGateY && x > 0x14 && x <= 0x5F) {
        HandleLButtonDown_NameColumnHit(app, hwnd, y);
        return;
    }

    // ---- S5: main area (X in (95, sidebar-18) && row gates) --------------
    // (0x447390-0x447A4C) gate fail falls into S7 (name-row rects).
    if (rowGateY && x > 0x5F && x < sidebar - 0x12) {
        HandleLButtonDown_TimelineAreaHit(app, hwnd, x, y);
        return;
    }

    // ---- S7: header strip (y in (144,160))
    if (x < sidebar - 0x12 && y > 0x90 && y < 0xA0 && x > 0x64) {
        HandleLButtonDown_FrameStripHit(app, hwnd, x, y, sidebar);
        return;
    }

    // ---- S7b: bone/morph name-row rects
    HandleLButtonDown_NameRowRectHit(app, x, y, rc.bottom);
}

}  // namespace mikudancestudio
