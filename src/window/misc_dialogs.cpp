// ===========================================================================
// Misc dialog bodies: frame-control apply, reorder fill, accessory-edit init, physics ON/OFF apply
// ===========================================================================
// Split out of src/window/command_view_menu.cpp (the menu-251..302 command
// family) so the dialog's helper bodies can be ported independently.
// Every function keeps its original x86 VA; behaviour notes live in the
// per-function comments.  The x64 confirmation addresses (MikuMikuDance
// v932x64) are recorded per function:
//   ApplyCameraFrameScaleAdd  ->  sub_7FF7CB4BC330   (called by camera dialog proc sub_7FF7CB478860, x86 0x43DAD0)
//   ApplyBoneFrameScaleAdd ->  sub_7FF7CB4BC9C0   (called by bone dialog proc sub_7FF7CB4789D0, x86 0x43E000)
//   RegisterBoneUndoSnapshot  ->  sub_7FF7CB4EEBD0   (bone apply's pre-loop undo registration)
//   ApplyMorphScaleAdd  ->  sub_7FF7CB4BD120   (called by dialog proc sub_7FF7CB478B40)
//   InitModelOrderDialog  ->  sub_7FF7CB4A98C0   (array builder; the listbox fill + the
//                                       allocation live in dialog proc
//                                       sub_7FF7CB477D70's WM_INITDIALOG -
//                                       the x86 0x41E810 rolls all three
//                                       together because its caller
//                                       ModelCalculateOrderDlgProc only passes (count, hDlg))
//   InitGravityDialog  ->  sub_7FF7CB4B5F00   (gravity cluster echo into 709..713)
//   ApplyPhysicsOnOff  ->  sub_7FF7CB4BED40   (called by dialog proc sub_7FF7CB4BEC00)
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// ---- interpolation-curve panel helpers ------------------------------------
// The two GDI helpers are twins of the ones ported in
// src/window/ui_selection_reeval.cpp (0x415E90 / 0x416090); they are
// duplicated here because that TU keeps them file-local.

namespace {

constexpr int kCurvePanelSize = 128;   // 128x128 interpolation mini panel

// One interpolation curve: control-point pair as signed panel bytes.
struct Curve {
    int x1;
    int y1;
    int x2;
    int y2;
};

bool operator!=(const Curve& left, const Curve& right) {
    return left.x1 != right.x1 || left.y1 != right.y1 ||
           left.x2 != right.x2 || left.y2 != right.y2;
}

// x64 sub_7FF7CB483530 (x86 twin 0x415E90): draw one interpolation curve
// into hdcInterpCurve - red 1px pen, cubic Bezier between (0,0) and
// (128,128) with the control pair (x1,y1)/(x2,y2), sampled at
// t = 1/100 .. 99/100 (y plotted as 126 - y).
void DrawCurveSegment(MMDApp* app, const Curve& curve) {
    HDC dc = app->CurveDC();
    HPEN pen = CreatePen(PS_SOLID, 1, 0xFF0000u);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    int oldX = 0;
    int oldY = 126;
    for (int step = 1; step < 100; ++step) {
        const float t = static_cast<float>(step) / 100.0f;
        const float ax = static_cast<float>(curve.x1) * t;
        const float ay = static_cast<float>(curve.y1) * t;
        const float bx = static_cast<float>(curve.x1) +
                         static_cast<float>(curve.x2 - curve.x1) * t;
        const float by = static_cast<float>(curve.y1) +
                         static_cast<float>(curve.y2 - curve.y1) * t;
        const float cx = static_cast<float>(curve.x2) +
                         static_cast<float>(kCurvePanelSize - curve.x2) * t;
        const float cy = static_cast<float>(curve.y2) +
                         static_cast<float>(kCurvePanelSize - curve.y2) * t;
        const float dx = ax + (bx - ax) * t;
        const float dy = ay + (by - ay) * t;
        const float ex = bx + (cx - bx) * t;
        const float ey = by + (cy - by) * t;
        const int x = static_cast<int>(dx + (ex - dx) * t);
        const int y = 126 - static_cast<int>(dy + (ey - dy) * t);
        MoveToEx(dc, oldX, oldY, nullptr);
        LineTo(dc, x, y);
        oldX = x;
        oldY = y;
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

// x64 sub_7FF7CB483790 (x86 twin 0x416090): highlight the current curve -
// 2px blue X marks on both control points (y as 127-complement, cached
// into interpCurveControlCache for the drag editor) plus black guide
// lines from the panel corners.
void DrawCurveHighlight(MMDApp* app, const Curve& curve) {
    HDC dc = app->CurveDC();
    HPEN pen = CreatePen(PS_SOLID, 2, 0x000000FFu);
    HGDIOBJ oldPen = SelectObject(dc, pen);

    app->state.interpCurveControlCache[0] =
        static_cast<std::uint8_t>(curve.x1);
    app->state.interpCurveControlCache[1] =
        static_cast<std::uint8_t>(127 - curve.y1);
    app->state.interpCurveControlCache[2] =
        static_cast<std::uint8_t>(curve.x2);
    app->state.interpCurveControlCache[3] =
        static_cast<std::uint8_t>(127 - curve.y2);

    MoveToEx(dc, curve.x1 - 3, 124 - curve.y1, nullptr);
    LineTo(dc, curve.x1 + 3, 130 - curve.y1);
    MoveToEx(dc, curve.x1 + 3, 124 - curve.y1, nullptr);
    LineTo(dc, curve.x1 - 3, 130 - curve.y1);
    MoveToEx(dc, curve.x2 - 3, 124 - curve.y2, nullptr);
    LineTo(dc, curve.x2 + 3, 130 - curve.y2);
    MoveToEx(dc, curve.x2 + 3, 124 - curve.y2, nullptr);
    LineTo(dc, curve.x2 - 3, 130 - curve.y2);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    HPEN guide = CreatePen(PS_SOLID, 0, 0);
    SelectObject(dc, guide);
    MoveToEx(dc, 0, 127, nullptr);
    LineTo(dc, curve.x1, 127 - curve.y1);
    MoveToEx(dc, 127, 0, nullptr);
    LineTo(dc, curve.x2, 127 - curve.y2);
    SelectObject(dc, oldPen);
    DeleteObject(guide);
}

// Curve-gate byte: camera records mark timeline-selected keys at +72
// (CameraKey::selected), bone keys mark allocated records at +56.
bool CurveKeyActive(const mdl::CameraKey& key) { return key.selected != 0; }
bool CurveKeyActive(const mdl::BoneKey& key) { return key.allocated != 0; }

// Channel curve of one record.  Camera: interpolation[4][6], channel index
// inside each group; bone: interpolation[16], groups x1/y1/x2/y2 at 0/4/8/12.
Curve ReadCurve(const mdl::CameraKey& key, int channel) {
    const auto value = [&key, channel](int group) {
        return static_cast<int>(static_cast<std::int8_t>(
            key.interpolation[group][channel]));
    };
    return {value(0), value(1), value(2), value(3)};
}
Curve ReadCurve(const mdl::BoneKey& key, int channel) {
    const auto value = [&key, channel](int base) {
        return static_cast<int>(static_cast<std::int8_t>(
            key.interpolation[base + channel]));
    };
    return {value(0), value(4), value(8), value(12)};
}

// One track scan of the x64 repaint loop (camera and bone records share
// the shape).  First active record draws its curve and seeds the baseline
// plus (in all-channel mode) the per-channel snapshots; every later record
// redraws only differing curves, clearing interpCurveUniformFound on any
// cross-record difference and perChannelUniform on any per-channel one.
// In single-channel mode the original forces perChannelUniform false right
// after the first record (kept as-is: button 430 then keys off
// interpCurveUniformFound alone).
template <typename Key>
void ScanCurveTrack(MMDApp* app, const Key* keys, std::size_t capacity,
                    int channelCount, int channel, Curve& baseline,
                    Curve (&firstChannels)[6], bool& found,
                    bool& perChannelUniform) {
    if (keys == nullptr)
        return;
    for (std::size_t i = 0; i < capacity; ++i) {
        const Key& key = keys[i];
        if (!CurveKeyActive(key))
            continue;
        if (!found) {
            if (channel >= channelCount) {
                DrawCurveSegment(app, ReadCurve(key, 0));
                baseline = ReadCurve(key, 0);
                firstChannels[0] = baseline;
                app->state.interpCurveUniformFound = 1;
                for (int c = 1; c < channelCount; ++c) {
                    firstChannels[c] = ReadCurve(key, c);
                    if (ReadCurve(key, c) != baseline) {
                        DrawCurveSegment(app, ReadCurve(key, c));
                        app->state.interpCurveUniformFound = 0;
                    }
                }
            } else {
                DrawCurveSegment(app, ReadCurve(key, channel));
                baseline = ReadCurve(key, channel);
                perChannelUniform = false;
                app->state.interpCurveUniformFound = 1;
            }
            found = true;
        } else if (channel >= channelCount) {
            for (int c = 0; c < channelCount; ++c) {
                if (ReadCurve(key, c) != baseline) {
                    DrawCurveSegment(app, ReadCurve(key, c));
                    app->state.interpCurveUniformFound = 0;
                }
            }
            for (int c = 0; c < channelCount; ++c) {
                if (ReadCurve(key, c) != firstChannels[c])
                    perChannelUniform = false;
            }
        } else {
            if (ReadCurve(key, channel) != baseline) {
                DrawCurveSegment(app, ReadCurve(key, channel));
                app->state.interpCurveUniformFound = 0;
            }
        }
    }
}

}  // namespace

// ===========================================================================
// x64 0x7FF7CB482BB0 - CurvePanelRepaint(app)
// ===========================================================================
// Repaints the 128x128 interpolation-curve mini panel (white pen/brush
// wash into hdcInterpCurve).  Channel = CB_GETCURSEL of combo 433; the
// camera mode (optflag[0]) scans the global camera track (84-byte keys,
// gate = selected +72, six interpolation channels in interpolation[4][6]),
// model mode the selected model's bone-key table (60-byte records, gate =
// allocated +56, four channels in interpolation[16]).  When the combo
// selects the "all" entry (camera >= 6 / bone >= 4) the first active
// record's channel-0 curve seeds the baseline and each further differing
// curve (any channel, any record) is redrawn over it, else only the
// selected channel is compared.  All curves identical -> the current
// curve is highlighted (sub_7FF7CB483790) and 0x9DA04 stores that fact;
// button 430 follows that flag or, in all-channel mode only, the weaker
// per-channel consistency; 432 follows "any key found"; 431 additionally
// requires the signed preset-A lead byte (0x9DA0A) to be >= 0.  Tail:
// InvalidateRect{8, bottom-135, 137, bottom-6} on the main window.
// Port detail: the x64 walks 0x927C0 60-byte bone slots (the E build's
// larger table); the port scans kBoneKeyCapacity like the sibling ports
// (0x416280 / 0x430510 in ui_selection_reeval.cpp).
// =========================================================================//
void CurvePanelRepaint(MMDApp* app) {  // x64 0x7FF7CB482BB0
    constexpr int kInterpCurveCombo = 433;   // channel selector combo
    constexpr int kCurveUniformButton = 430; // enabled when curves agree
    constexpr int kCurvePresetButton = 431;  // also gated on preset-A byte
    constexpr int kCurveFoundButton = 432;   // enabled when a key exists
    constexpr int kCurveDirtyLeft = 8;
    constexpr int kCurveDirtyRight = 137;
    constexpr int kCurveDirtyTopMargin = 135;
    constexpr int kCurveDirtyBottomMargin = 6;

    app->state.interpCurveUniformFound = 0;
    HDC dc = app->CurveDC();
    HPEN whitePen = CreatePen(PS_SOLID, 1, 0x00FFFFFFu);
    HBRUSH whiteBrush = CreateSolidBrush(0x00FFFFFFu);
    HGDIOBJ oldPen = SelectObject(dc, whitePen);
    HGDIOBJ oldBrush = SelectObject(dc, whiteBrush);
    Rectangle(dc, 0, 0, kCurvePanelSize, kCurvePanelSize);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(whitePen);
    DeleteObject(whiteBrush);

    const HWND window = static_cast<HWND>(app->Hwnd());
    const int channel = static_cast<int>(SendMessageA(
        GetDlgItem(window, kInterpCurveCombo), CB_GETCURSEL, 0, 0));

    bool found = false;             // x64 v83: any active record seen
    bool perChannelUniform = true;  // x64 v84: per-channel consistency
    Curve baseline{};
    Curve firstChannels[6]{};

    if (app->state.optflag[0] != 0) {
        ScanCurveTrack(app, app->CameraKeys(), mdl::kTimelineKeyCapacity,
                       6, channel, baseline, firstChannels, found,
                       perChannelUniform);
    } else {
        unsigned char* model = app->SelectedModel();
        ScanCurveTrack(app, model != nullptr ? mdl::BoneKeys(model) : nullptr,
                       mdl::kBoneKeyCapacity, 4, channel, baseline,
                       firstChannels, found, perChannelUniform);
    }

    if (app->state.interpCurveUniformFound != 0) {
        DrawCurveHighlight(app, baseline);
        EnableWindow(GetDlgItem(window, kCurveUniformButton), TRUE);
    } else {
        EnableWindow(GetDlgItem(window, kCurveUniformButton),
                     perChannelUniform ? TRUE : FALSE);
    }
    EnableWindow(GetDlgItem(window, kCurveFoundButton),
                 found ? TRUE : FALSE);
    EnableWindow(GetDlgItem(window, kCurvePresetButton),
                 found &&
                         reinterpret_cast<const std::int8_t&>(
                             app->state.lightA[0]) >= 0
                     ? TRUE
                     : FALSE);

    RECT client{};
    GetClientRect(window, &client);
    RECT dirty{kCurveDirtyLeft,
               client.bottom - kCurveDirtyTopMargin,
               kCurveDirtyRight,
               client.bottom - kCurveDirtyBottomMargin};
    InvalidateRect(window, &dirty, FALSE);
}

// ===========================================================================
// x64 0x7FF7CB4ED750 - MorphPanelRefresh(model)
// ===========================================================================
// Facial/IK panel echo of one model record: checkbox 439 mirrors the
// visibility byte (loadComplete, x64 +12569), the radio pair 444/445
// mirrors the IK-chain state of combo 443's current selection
// (ikChains[sel].enabled -> 444 else 445), and each of the four morph
// selectors (selectedMorphs[0..3], x64 +12592..) with an index >= 0 parks
// morphs[idx].value * 100 on its trackbar (505/510/515/520) and echoes
// "%5.4f" into the paired edit (506/511/516/521).  Negative selectors
// leave their group untouched (unlike PostLoadInit's echo).
// =========================================================================//
void MorphPanelRefresh(unsigned char* model) {  // x64 0x7FF7CB4ED750
    constexpr int kModelVisibleCheck = 439;
    constexpr int kIkChainCombo = 443;
    constexpr int kIkOnRadio = 444;
    constexpr int kIkOffRadio = 445;
    static const int kMorphSlider[4] = {505, 510, 515, 520};
    static const int kMorphEdit[4] = {506, 511, 516, 521};

    mdl::ModelRecord* m = mdl::Mdl(model);
    HWND window = static_cast<HWND>(m->hwnd);

    SendMessageA(GetDlgItem(window, kModelVisibleCheck), BM_SETCHECK,
                 m->loadComplete != 0 ? 1 : 0, 0);
    if (m->ikChains != nullptr) {
        const int selection = static_cast<int>(SendMessageA(
            GetDlgItem(window, kIkChainCombo), CB_GETCURSEL, 0, 0));
        CheckRadioButton(window, kIkOnRadio, kIkOffRadio,
                         m->ikChains[selection].enabled != 0 ? kIkOnRadio
                                                             : kIkOffRadio);
    }
    if (m->morphs != nullptr) {
        char text[256];
        for (int group = 0; group < 4; ++group) {
            const std::int32_t selection = m->selectedMorphs[group];
            if (selection < 0)
                continue;
            const float value = m->morphs[selection].value;
            SendMessageA(GetDlgItem(window, kMorphSlider[group]),
                         TBM_SETPOS, 1,
                         static_cast<LPARAM>(
                             static_cast<int>(value * 100.0f)));
            sprintf_s(text, 0x100, "%5.4f", static_cast<double>(value));
            SetWindowTextA(GetDlgItem(window, kMorphEdit[group]), text);
        }
    }
}

// ===========================================================================
// 0x0043DAD0 - case-242 camera "frame control" apply (x64 sub_7FF7CB4BC330)
// ===========================================================================
// Reads the 16 edits 686..701 as eight (scale, add) pairs and applies each
// pair to every selected camera key (camera track app+0x374, 84-byte
// CameraKey records, 10000 of them, gated on the +0x48 selection byte):
//   pair  field                add conversion
//   0     eye.x    (+0x10)     -
//   1     eye.y    (+0x14)     -
//   2     eye.z    (+0x18)     -
//   3     target.x (+0x1C)     * -3.141592 / 180
//   4     target.y (+0x20)     *  3.141592 / 180
//   5     target.z (+0x24)     *  3.141592 / 180
//   6     distance (+0x0C)     -
//   7     fov      (+0x44 int) -
// A pair is skipped when scale == 1.0 and add == 0.0 (the dialog's default
// prefill).  Tail: camera seek (x64 sub_7FF7CB479C30 == the ported 0x42E640
// ReloadModels), PostViewRefresh, dirty 0xA0B0D = 1.
// =========================================================================//
void ApplyCameraFrameScaleAdd(MMDApp* app, HWND hDlg) {  // VA 0x0043DAD0
    float scale[8];
    float add[8];
    char buf[20];
    for (int pair = 0; pair < 8; ++pair) {
        GetWindowTextA(GetDlgItem(hDlg, panel::kCamMulPosXScaleEdit + 2 * pair), buf, 20);
        scale[pair] = static_cast<float>(atof(buf));
        GetWindowTextA(GetDlgItem(hDlg, panel::kCamMulPosXOffsetEdit + 2 * pair), buf, 20);
        add[pair] = static_cast<float>(atof(buf));
    }
    // degree -> radian on the three target-angle adds (target.x negated)
    add[3] = static_cast<float>(add[3] * -3.141592) / 180.0f;
    add[4] = static_cast<float>(add[4] * 3.141592) / 180.0f;
    add[5] = static_cast<float>(add[5] * 3.141592) / 180.0f;

    mdl::CameraKey* keys = app->CameraKeys();
    for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
        mdl::CameraKey& key = keys[i];
        if (key.selected == 0)
            continue;
        if (scale[0] != 1.0f || add[0] != 0.0f)
            key.eye[0] = static_cast<float>(scale[0] * key.eye[0]) + add[0];
        if (scale[1] != 1.0f || add[1] != 0.0f)
            key.eye[1] = static_cast<float>(scale[1] * key.eye[1]) + add[1];
        if (scale[2] != 1.0f || add[2] != 0.0f)
            key.eye[2] = static_cast<float>(scale[2] * key.eye[2]) + add[2];
        if (scale[3] != 1.0f || add[3] != 0.0f)
            key.target[0] =
                static_cast<float>(scale[3] * key.target[0]) + add[3];
        if (scale[4] != 1.0f || add[4] != 0.0f)
            key.target[1] =
                static_cast<float>(scale[4] * key.target[1]) + add[4];
        if (scale[5] != 1.0f || add[5] != 0.0f)
            key.target[2] =
                static_cast<float>(scale[5] * key.target[2]) + add[5];
        if (scale[6] != 1.0f || add[6] != 0.0f)
            key.distance =
                static_cast<float>(scale[6] * key.distance) + add[6];
        if (scale[7] != 1.0f || add[7] != 0.0f)
            key.fov = static_cast<int>(
                static_cast<float>(static_cast<float>(
                    static_cast<float>(key.fov) * scale[7]) + add[7]));
    }
    ReloadModels(app);      // 0x42E640 (x64 sub_7FF7CB479C30)
    PostViewRefresh(app);   // 0x40D130 (x64 sub_7FF7CB440DD0)
    app->SceneModified() = 1;
}

// ===========================================================================
// x64 0x7FF7CB4EEBD0 - bone undo registration (RegisterBoneUndoSnapshot)
// ===========================================================================
// Shared pre-loop helper of the bone frame-control apply (six code call
// sites in the original): counts the model's allocated bone keys (600000
// 60-byte records, gate = allocated byte +56) and, when at least one
// exists, rolls the model's undo ring - enable undo (400) / disable redo
// (401), cursor = (cursor+1) mod 30 mirrored into redoState, undoDirty = 1
// with redoDirty = 0 (one WORD store at model+0x3558 in the original), slot
// operation 2 at `frame`, a 36-byte BonePoseSnapshot per bone (bone-table
// trans/rotQuat plus the bonePhysicsState byte), then one 64-byte
// auxiliary record {int recordIndex, 60-byte BoneKey copy} per allocated
// key, deduped through keyVisitMap (model+0x3CAC) exactly like the physics
// apply's chain walk below.
// =========================================================================//
void RegisterBoneUndoSnapshot(unsigned char* modelBytes,
                              int frame) {  // x64 0x7FF7CB4EEBD0
    mdl::ModelRecord* model = mdl::Mdl(modelBytes);
    mdl::BoneKey* keys = model->boneKeys;
    if (keys == nullptr)
        return;

    std::size_t used = 0;
    for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i) {
        if (keys[i].allocated != 0)
            ++used;
    }
    if (used == 0)
        return;

    HWND window = static_cast<HWND>(model->hwnd);
    EnableWindow(GetDlgItem(window, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(window, panel::kRedoButton), FALSE);
    std::uint32_t& cursor = model->undoState[0];
    if (++cursor >= 30)
        cursor = 0;
    model->undoState[1] = cursor;
    model->undoDirty = 1;
    model->redoDirty = 0;
    mdl::UndoRecord& undo = model->undoRings[0].slots[cursor];
    undo.operation = 2;
    undo.dirty = 0;
    undo.frame = static_cast<std::uint32_t>(frame);

    if (undo.bonePose != nullptr) {
        ::operator delete(undo.bonePose);
        undo.bonePose = nullptr;
    }
    const std::int32_t boneCount = model->boneCount;
    auto* snapshot = static_cast<mdl::BonePoseSnapshot*>(::operator new(
        sizeof(mdl::BonePoseSnapshot) * boneCount));
    undo.bonePose = snapshot;
    std::memset(snapshot, 0,
                sizeof(mdl::BonePoseSnapshot) *
                    static_cast<std::size_t>(boneCount));
    mdl::BoneRecord* bones = model->boneTable;
    for (int i = 0; i < boneCount; ++i) {
        snapshot[i].boneIndex = i;
        snapshot[i].position[0] = bones[i].trans[0];
        snapshot[i].position[1] = bones[i].trans[1];
        snapshot[i].position[2] = bones[i].trans[2];
        snapshot[i].rotation[0] = bones[i].rotQuat[0];
        snapshot[i].rotation[1] = bones[i].rotQuat[1];
        snapshot[i].rotation[2] = bones[i].rotQuat[2];
        snapshot[i].rotation[3] = bones[i].rotQuat[3];
        snapshot[i].physicsDisabled =
            model->bonePhysicsState != nullptr
                ? model->bonePhysicsState[i]
                : 0;
    }

    if (undo.auxiliaryPose != nullptr) {
        ::operator delete(undo.auxiliaryPose);
        undo.auxiliaryPose = nullptr;
    }
    undo.auxiliaryPose = ::operator new(used * 0x40);
    std::memset(undo.auxiliaryPose, 0, used * 0x40);
    std::memset(model->keyVisitMap, 0, sizeof(model->keyVisitMap));

    // 64-byte auxiliary record = {int recordIndex, 60-byte BoneKey copy};
    // the append uses undo.dirty as its counter like the physics apply.
    for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i) {
        if (keys[i].allocated == 0 || model->keyVisitMap[i] != 0)
            continue;
        model->keyVisitMap[i] = 1;
        auto* records = static_cast<unsigned char*>(undo.auxiliaryPose);
        *reinterpret_cast<std::int32_t*>(
            records + 0x40 * static_cast<std::size_t>(undo.dirty)) =
            static_cast<std::int32_t>(i);
        std::memcpy(records + 0x40 * static_cast<std::size_t>(undo.dirty) + 4,
                    &keys[i], 0x3C);
        ++undo.dirty;
    }
}

// ===========================================================================
// 0x0043E000 - case-251 bone "frame control" apply (x64 sub_7FF7CB4BC9C0)
// ===========================================================================
// Reads the 12 edits 686..697 as six (scale, add) pairs and applies them to
// every allocated bone key of the current model (model slot app+0x13E0,
// bone-key table model+0x2790, 600000 60-byte records, gate = allocated
// byte +56):
//   pair  edits   field
//   0     686/687 position.x (+0x1C)
//   1     688/689 position.y (+0x20)
//   2     690/691 position.z (+0x24)
//   3     692/693 euler X angle (add enters as +deg * pi / 180)
//   4     694/695 euler Y angle (add enters as -deg * pi / 180)
//   5     696/697 euler Z angle (add enters as -deg * pi / 180)
// A position pair is skipped when scale == 1.0 and add == 0.0.  Before the
// loop the original registers the bone undo snapshot
// (RegisterBoneUndoSnapshot above) unconditionally - even when every pair
// is the identity.
// The rotation block runs when any rotation scale != 1.0 or converted
// add != 0.0: it builds the key quaternion's matrix (quat at +0x28) and
// decomposes it as Rz*Rx*Ry, the same extraction as the select-dialog
// display twin sub_7FF7CB4BB3E0 (RefreshSelectNavDisplay in
// dialog_select_ops.cpp):
//   z = atan2f(_12, _22); x = asinf(-_32); y = atan2f(_31, _33)
//   gimbal patch when |cosf(x)| < 1e-6 (x64 0x7FF7CB4BCF3B..F84):
//     z += _12 > 0 ? +3.141592 : -3.141592
//     y += _31 > 0 ? +3.141592 : -3.141592
// then scales + adds each angle (z takes pair 5, x pair 3, y pair 4),
// rebuilds Rz * Rx * Ry (rotZ, mul rotX, mul rotY) and writes the
// quaternion back via D3DXQuaternionRotationMatrix.
// Tail: PanelPaint (x64 sub_7FF7CB480EA0), CurvePanelRepaint (x64
// sub_7FF7CB482BB0), SeekModelFrame(model, currentFrame,
// playbackPhysicsMode) (x64 sub_7FF7CB4EBD90), dirty 0xA0B0D = 1,
// PostViewRefresh (x64 sub_7FF7CB440DD0).
// =========================================================================//
void ApplyBoneFrameScaleAdd(MMDApp* app, HWND hDlg) {  // VA 0x0043E000
    unsigned char* modelBytes = app->ModelSlot(app->state.slotIdx);
    if (modelBytes == nullptr)
        return;

    d3dx::Api& api = d3dx::Get();
    // The original imports d3dx statically for the rotation rebuild; the
    // runtime-resolved port resolves it once before the loop.
    api.Load();

    float scale[6];
    float add[6];
    char buf[20];
    for (int pair = 0; pair < 6; ++pair) {
        GetWindowTextA(
            GetDlgItem(hDlg, panel::kBoneMulPosXScaleEdit + 2 * pair),
            buf, 20);
        scale[pair] = static_cast<float>(atof(buf));
        GetWindowTextA(
            GetDlgItem(hDlg, panel::kBoneMulPosXOffsetEdit + 2 * pair),
            buf, 20);
        add[pair] = static_cast<float>(atof(buf));
    }
    // degree -> radian on the three rotation adds (X positive, Y/Z negated)
    add[3] = static_cast<float>(add[3] * 3.141592) / 180.0f;
    add[4] = static_cast<float>(add[4] * -3.141592) / 180.0f;
    add[5] = static_cast<float>(add[5] * -3.141592) / 180.0f;

    RegisterBoneUndoSnapshot(modelBytes, app->state.currentFrame);

    mdl::BoneKey* keys = mdl::BoneKeys(modelBytes);
    d3dx::D3DXMATRIXF quatMatrix{};
    d3dx::D3DXMATRIXF rotation{};
    d3dx::D3DXMATRIXF axis{};
    for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i) {
        mdl::BoneKey& key = keys[i];
        if (key.allocated == 0)
            continue;
        if (scale[0] != 1.0f || add[0] != 0.0f)
            key.position[0] =
                static_cast<float>(scale[0] * key.position[0]) + add[0];
        if (scale[1] != 1.0f || add[1] != 0.0f)
            key.position[1] =
                static_cast<float>(scale[1] * key.position[1]) + add[1];
        if (scale[2] != 1.0f || add[2] != 0.0f)
            key.position[2] =
                static_cast<float>(scale[2] * key.position[2]) + add[2];
        if (scale[3] != 1.0f || scale[4] != 1.0f || scale[5] != 1.0f ||
            add[3] != 0.0f || add[4] != 0.0f || add[5] != 0.0f) {
            api.matrixRotationQuaternion(&quatMatrix, key.rotation);
            float zAngle = atan2f(quatMatrix.m[0][1], quatMatrix.m[1][1]);
            float xAngle = asinf(-quatMatrix.m[2][1]);
            float yAngle = atan2f(quatMatrix.m[2][0], quatMatrix.m[2][2]);
            if (1e-6f > fabsf(cosf(xAngle))) {  // gimbal lock
                zAngle +=
                    quatMatrix.m[0][1] > 0.0f ? 3.141592f : -3.141592f;
                yAngle +=
                    quatMatrix.m[2][0] > 0.0f ? 3.141592f : -3.141592f;
            }
            api.rotZ(&rotation, zAngle * scale[5] + add[5]);
            api.rotX(&axis, xAngle * scale[3] + add[3]);
            api.multiply(&rotation, &rotation, &axis);
            api.rotY(&axis, yAngle * scale[4] + add[4]);
            api.multiply(&rotation, &rotation, &axis);
            api.quatFromMatrix(key.rotation, &rotation);
        }
    }
    PanelPaint(app);         // 0x414610 (x64 sub_7FF7CB480EA0)
    CurvePanelRepaint(app);  // x64 sub_7FF7CB482BB0
    SeekModelFrame(modelBytes, app->state.currentFrame,
              app->state.playbackPhysicsMode);  // 0x4B4260 (x64 0x4EBD90)
    app->SceneModified() = 1;  // x64 0xA1B31 dirty byte
    PostViewRefresh(app);   // 0x40D130 (x64 sub_7FF7CB440DD0)
}

// ===========================================================================
// 0x0043E680 - case-252 "frame control" apply (x64 sub_7FF7CB4BD120)
// ===========================================================================
// Reads the (scale, add) pair from edits 686/687 and, when it is not the
// identity (1.0 / 0.0), scales+shifts every allocated morph key of the
// current model (morphKeys table, 20000 20-byte records, value +0x0C,
// allocated byte +0x10).  Tail: PanelPaint, curve-panel repaint
// (x64 sub_7FF7CB482BB0), model frame seek SeekModelFrame(model, currentFrame,
// playbackPhysicsMode) (x64 sub_7FF7CB4EBD90), facial/IK panel refresh
// (x64 sub_7FF7CB4ED750), dirty 0xA0B0D = 1.
// =========================================================================//
void ApplyMorphScaleAdd(MMDApp* app, HWND hDlg) {  // VA 0x0043E680
    unsigned char* modelBytes = app->ModelSlot(app->state.slotIdx);
    if (modelBytes == nullptr)
        return;

    char buf[20];
    GetWindowTextA(GetDlgItem(hDlg, panel::kMorphMulScaleEdit), buf, 20);
    const float scale = static_cast<float>(atof(buf));
    GetWindowTextA(GetDlgItem(hDlg, panel::kMorphMulOffsetEdit), buf, 20);
    const float add = static_cast<float>(atof(buf));

    if (scale != 1.0f || add != 0.0f) {
        mdl::MorphKey* keys = mdl::MorphKeys(modelBytes);
        for (std::size_t i = 0; i < mdl::kMorphKeyCapacity; ++i) {
            if (keys[i].allocated != 0)
                keys[i].value =
                    static_cast<float>(scale * keys[i].value) + add;
        }
    }
    PanelPaint(app);         // 0x414610 (x64 sub_7FF7CB480EA0)
    CurvePanelRepaint(app);  // x64 sub_7FF7CB482BB0
    SeekModelFrame(modelBytes, app->state.currentFrame,
              app->state.playbackPhysicsMode);  // 0x4B4260 (x64 0x4EBD90)
    MorphPanelRefresh(modelBytes);               // x64 sub_7FF7CB4ED750
    app->SceneModified() = 1;
}

// ===========================================================================
// 0x0041E810 - case-289 reorder-dialog init fill (x64 sub_7FF7CB4A98C0)
// ===========================================================================
// `count` is CB_GETCOUNT(main combo 0x1B4) - 1 (cached in
// g_calculateOrderDialogCount by
// the caller ModelCalculateOrderDlgProc).  The helper owns three steps the x64 dialog proc
// (sub_7FF7CB477D70) performs inline:
//   1. allocate the int scratch array app+0xA0B1C (count+1 dwords - the
//      original allocates CB_GETCOUNT entries),
//   2. copy combo entries 1..count into listbox 628 (entry 0 is the
//      "camera/light/accessory" header row),
//   3. build the order array: for order = 1..count scan the model slots for
//      the model whose combo order byte (+0x2D7C) equals `order` and store
//      slotIndex+1 (the +1 quirk ApplyModelCalculateOrderDialog relies on).
// =========================================================================//
void InitModelOrderDialog(int count, HWND hDlg) {  // VA 0x0041E810
    MMDApp* app = g_Block;
    constexpr std::size_t kModelOrder2D7C = 0x2D7C;  // combo order byte
    // value-init: element 0 stays untouched by the fill below, and the OK
    // apply (ApplyModelCalculateOrderDialog) walks the array from index 0
    app->AccessoryOrderArray() =
        new std::int32_t[static_cast<std::size_t>(count) + 1]();

    char buf[0x100];
    for (int i = 1; i <= count; ++i) {
        SendMessageA(GetDlgItem(app->state.hwnd, panel::kMainComboModel),
                     CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(buf));
        SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(buf));
    }

    std::int32_t* order =
        static_cast<std::int32_t*>(app->AccessoryOrderArray());
    // x64 sub_7FF7CB4A98C0+0x4A98F0: order runs 1..count (the combo count
    // cached at dword_7FF7CB564604), the slot scan to 0xFF (cmp ecx,0FFh
    // @0x4A991A) - kModelSlotCount wide.
    for (int ord = 1; ord <= count; ++ord) {
        for (int slot = 0; slot < kModelSlotCount; ++slot) {
            unsigned char* model = app->ModelSlot(slot);
            if (model != nullptr &&
                mdl::Mdl(model)->comboSelIndex ==
                    static_cast<unsigned char>(ord)) {
                order[ord] = slot + 1;
                break;
            }
        }
    }
}

// ===========================================================================
// 0x00423160 - case-266 accessory-frame dialog init (x64 sub_7FF7CB4B5F00)
// ===========================================================================
// Echoes the gravity cluster into the dialog controls: "%3.2f" of the
// magnitude (0x9EDC8) into edit 709 and of the direction X/Y/Z (0x9EDBC /
// 0x9EDC0 / 0x9EDC4) into edits 710..712; trackbars 637..639 get range
// -100..100, tick frequency 0x3E8 and pos = value * 100; "%d" of the noise
// dword (0x9EDCC) into edit 713; checkbox 731 mirrors the noise-mode byte
// 0xA0CD4 and gates edit 713 through EnableWindow.
// =========================================================================//
void InitGravityDialog(HWND hDlg) {  // VA 0x00423160
    MMDApp* app = g_Block;
    char buf[0x100];
    sprintf_s(buf, 0x100, "%3.2f",
              static_cast<double>(app->state.gravityMagnitude));
    SetWindowTextA(GetDlgItem(hDlg, panel::kGravityAccelEdit), buf);
    const float direction[3] = {app->state.gravityX, app->state.gravityY,
                                app->state.gravityZ};
    static const int kDirectionEdits[3] = {710, 711, 712};
    static const int kDirectionTracks[3] = {637, 638, 639};
    for (int axis = 0; axis < 3; ++axis) {
        sprintf_s(buf, 0x100, "%3.2f",
                  static_cast<double>(direction[axis]));
        SetWindowTextA(GetDlgItem(hDlg, kDirectionEdits[axis]), buf);
        SendMessageA(GetDlgItem(hDlg, kDirectionTracks[axis]),
                     TBM_SETRANGEMIN, 0, -100);
        SendMessageA(GetDlgItem(hDlg, kDirectionTracks[axis]),
                     TBM_SETRANGEMAX, 0, 100);
        SendMessageA(GetDlgItem(hDlg, kDirectionTracks[axis]),
                     TBM_SETTICFREQ, 0x3E8, 0);
        SendMessageA(GetDlgItem(hDlg, kDirectionTracks[axis]),
                     TBM_SETPOS, 1,
                     static_cast<int>(
                         static_cast<double>(direction[axis]) * 100.0));
    }
    sprintf_s(buf, 0x100, "%d", app->state.gravityNoise);
    SetWindowTextA(GetDlgItem(hDlg, panel::kGravityNoiseEdit), buf);
    if (app->state.gravityNoiseEnabled != 0) {
        SendMessageA(GetDlgItem(hDlg, panel::kGravityNoiseCheckbox), BM_SETCHECK, 1, 0);
        EnableWindow(GetDlgItem(hDlg, panel::kGravityNoiseEdit), TRUE);
    } else {
        SendMessageA(GetDlgItem(hDlg, panel::kGravityNoiseCheckbox), BM_SETCHECK, 0, 0);
        EnableWindow(GetDlgItem(hDlg, panel::kGravityNoiseEdit), FALSE);
    }
}

// 0x00412330 ApplyGravityTrack (gravity-track apply + physics dialog refresh) is
// ported in src/model/track_apply.cpp.

// ===========================================================================
// 0x004403C0 - case-275 physics ON/OFF apply (x64 sub_7FF7CB4BED40)
// ===========================================================================
// `on` is (CB_GETCURSEL(combo 669) != 0): 0 = "ON (X mark)", 1 = "OFF" - it
// lands in BoneKey::physicsDisabled of every registered key.  Body:
//   * walk the current model's bone keys (bones gated on the physics X-mark
//     list flag BoneRecord::hasRigidBody) and count the allocated keys along each
//     bone's key chain - the count sizes the undo auxiliary buffer,
//   * enable undo (button 400) / disable redo (401), advance the undo ring
//     (undoDirty=1, redoDirty=0, cursor wrap at 30, redo cursor mirrors),
//     register undo slot operation 2 at the current frame, snapshot every
//     bone (36-byte BonePoseSnapshot: index, trans, rotQuat,
//     physicsDisabled from bonePhysicsState[i]) and allocate the 64-byte
//     auxiliary records,
//   * clear keyVisitMap and re-register a physics key for every allocated
//     key of the hasRigidBody bones, writing `on` into physicsDisabled,
//   * tail: PanelPaint + model frame seek SeekModelFrame(model, currentFrame,
//     playbackPhysicsMode).
// =========================================================================//
void ApplyPhysicsOnOff(int on) {  // VA 0x004403C0
    MMDApp* app = g_Block;
    unsigned char* modelBytes = app->ModelSlot(app->state.slotIdx);
    if (modelBytes == nullptr)
        return;
    mdl::ModelRecord* model = mdl::Mdl(modelBytes);
    const std::int32_t boneCount = model->boneCount;
    if (boneCount <= 0)
        return;
    mdl::BoneRecord* bones = model->boneTable;
    mdl::BoneKey* keys = model->boneKeys;
    if (bones == nullptr || keys == nullptr)
        return;
    const unsigned char physicsOff = static_cast<unsigned char>(on != 0);

    // ---- count the physics-marked keys (chain walk; the root is visited
    // twice by the original - once directly, once by the tail/walk re-check
    // - kept as-is since it only sizes the undo buffer) -------------------
    int marked = 0;
    for (int i = 0; i < boneCount; ++i) {
        if (bones[i].hasRigidBody == 0)
            continue;
        if (keys[i].allocated != 0)
            ++marked;
        int cur = i;
        while (keys[cur].next > 0) {
            if (keys[cur].allocated != 0)
                ++marked;
            cur = static_cast<int>(keys[cur].next);
        }
        if (keys[cur].allocated != 0)
            ++marked;
    }
    if (marked == 0)
        return;

    // ---- undo ring slot --------------------------------------------------
    HWND hwnd = app->state.hwnd;
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    model->undoDirty = 1;
    model->redoDirty = 0;
    std::uint32_t& cursor = model->undoState[0];
    if (++cursor >= 30)
        cursor = 0;
    model->undoState[1] = cursor;
    mdl::UndoRecord& undo = model->undoRings[0].slots[cursor];
    undo.operation = 2;
    undo.frame = static_cast<std::uint32_t>(app->state.currentFrame);

    if (undo.bonePose != nullptr) {
        ::operator delete(undo.bonePose);
        undo.bonePose = nullptr;
    }
    auto* snapshot = static_cast<mdl::BonePoseSnapshot*>(::operator new(
        sizeof(mdl::BonePoseSnapshot) * boneCount));
    undo.bonePose = snapshot;
    std::memset(snapshot, 0,
                sizeof(mdl::BonePoseSnapshot) *
                    static_cast<std::size_t>(boneCount));
    for (int i = 0; i < boneCount; ++i) {
        snapshot[i].boneIndex = i;
        snapshot[i].position[0] = bones[i].trans[0];
        snapshot[i].position[1] = bones[i].trans[1];
        snapshot[i].position[2] = bones[i].trans[2];
        snapshot[i].rotation[0] = bones[i].rotQuat[0];
        snapshot[i].rotation[1] = bones[i].rotQuat[1];
        snapshot[i].rotation[2] = bones[i].rotQuat[2];
        snapshot[i].rotation[3] = bones[i].rotQuat[3];
        snapshot[i].physicsDisabled =
            model->bonePhysicsState != nullptr
                ? model->bonePhysicsState[i]
                : 0;
    }
    undo.dirty = 0;
    if (undo.auxiliaryPose != nullptr) {
        ::operator delete(undo.auxiliaryPose);
        undo.auxiliaryPose = nullptr;
    }
    undo.auxiliaryPose = ::operator new(
        static_cast<std::size_t>(marked) * 0x40);
    std::memset(undo.auxiliaryPose, 0,
                static_cast<std::size_t>(marked) * 0x40);
    std::memset(model->keyVisitMap, 0, sizeof(model->keyVisitMap));

    // ---- register the physics keys ---------------------------------------
    // 64-byte auxiliary record = {int boneIndex, 60-byte BoneKey copy};
    // the append dedups through keyVisitMap (x64 sub_7FF7CB4E8FD0).
    const auto appendAux = [model, &undo](int boneIndex) {
        if (model->keyVisitMap[boneIndex] != 0)
            return;
        model->keyVisitMap[boneIndex] = 1;
        auto* records =
            static_cast<unsigned char*>(undo.auxiliaryPose);
        *reinterpret_cast<std::int32_t*>(
            records + 0x40 * static_cast<std::size_t>(undo.dirty)) =
            boneIndex;
        std::memcpy(records + 0x40 * static_cast<std::size_t>(undo.dirty) +
                        4,
                    &model->boneKeys[boneIndex], 0x3C);
        ++undo.dirty;
    };
    for (int i = 0; i < boneCount; ++i) {
        if (bones[i].hasRigidBody == 0)
            continue;
        if (keys[i].allocated != 0) {
            appendAux(i);
            keys[i].physicsDisabled = physicsOff;
        }
        int cur = i;
        while (keys[cur].next > 0) {
            if (keys[cur].allocated != 0) {
                appendAux(cur);
                keys[cur].physicsDisabled = physicsOff;
            }
            cur = static_cast<int>(keys[cur].next);
        }
        if (keys[cur].allocated != 0) {
            appendAux(cur);
            keys[cur].physicsDisabled = physicsOff;
        }
    }
    PanelPaint(app);  // 0x414610 (x64 sub_7FF7CB480EA0)
    SeekModelFrame(modelBytes, app->state.currentFrame,
              app->state.playbackPhysicsMode);  // 0x4B4260 (x64 0x4EBD90)
}

}  // namespace mikudancestudio
