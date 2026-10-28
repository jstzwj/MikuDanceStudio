// Keyframe editor commands: selection, undo, curves and model controls.
// Clipboard operations and global registration live in their own modules.
// Module boundaries and compatibility rules: docs/ARCHITECTURE.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/dialog_resources.h"
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>

#include <commdlg.h>
#include <cstdint>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "frame_clipboard.hpp"
#include "frame_key_selection.hpp"

namespace mikudancestudio {

using editor::ClearModelKeyMarks;

// Editor dependencies not declared by the shared interface.
void ApplyCameraReferenceModeChange(MMDApp* app, int oldMode); // VA 0x0041ACD0

// ---------------------------------------------------------------------------
// Helpers shared by the family.
// ---------------------------------------------------------------------------

// Active model = slot array at this+0x780 indexed by byte this+0x910
// (sub_47E8A0 pattern; the original re-derives it on every use).
static mdl::ModelRecord* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

static void UnlinkAccessoryKey(mdl::AccessoryKey* keys, std::uint32_t index) {
    mdl::AccessoryKey& key = keys[index];
    keys[key.previous].next = key.next;
    keys[key.next].previous = key.previous;
    key.frame = 0;
    key.previous = 0;
    key.next = 0;
}

template <typename Key> static void UnlinkGlobalKey(Key* keys, std::uint32_t index) {
    Key& key = keys[index];
    keys[key.previous].next = key.next;
    keys[key.next].previous = key.previous;
    key.frame = 0;
    key.previous = 0;
    key.next = 0;
}

static void ResetCameraRecord(mdl::CameraKey& key, bool head) {
    key.selected = 0;
    key.distance = -45.0f;
    key.eye[0] = 0.0f;
    key.eye[1] = head ? 10.0f : 0.0f;
    key.eye[2] = 0.0f;
    std::memset(key.target, 0, sizeof(key.target));
    key.parentModel = -1;
    key.parentBone = 0;
    std::memset(key.interpolation, 20, 12);
    std::memset(reinterpret_cast<unsigned char*>(key.interpolation) + 12, 107, 12);
}

void DeleteSelectedCameraKeys(mdl::CameraKey* camera) {
    if (camera[0].selected != 0)
        ResetCameraRecord(camera[0], true);
    for (std::uint32_t i = 1; i < mdl::kTimelineKeyCapacity; ++i) {
        if (camera[i].selected == 0)
            continue;
        // A frame-line merge can leave selection and links in a freed slot.
        // Unlinking that slot would overwrite its former predecessor's link.
        if (camera[i].frame != 0)
            UnlinkGlobalKey(camera, i);
        ResetCameraRecord(camera[i], false);
    }
}

static void ResetLightRecord(mdl::LightKey& key, bool head) {
    key.selected = 0;
    if (head) {
        key.direction[0] = -0.5f;
        key.direction[1] = -1.0f;
        key.direction[2] = 0.5f;
        key.color[0] = 0.602f; // bit-exact: 0x3F1A1CAC, see case 467
        key.color[1] = 0.602f;
        key.color[2] = 0.602f;
    } else {
        std::memset(key.direction, 0, sizeof(key.direction) + sizeof(key.color));
    }
}

static unsigned char DefaultShadowMode(MMDApp* app) {
    D3DRenderer* locale = app->Renderer();
    if (locale != nullptr && locale->postProcessEnabled != 0) { // 0x1D544
        return 1;
    }
    // Fixed-function fallback: keep the PMM self-shadow mode when the
    // optional post-effect object was unavailable on this device.
    return app->state.selfShadowMode != 0 ? 1 : 0;
}

static void ResetShadowRecord(MMDApp* app, mdl::SelfShadowKey& key) {
    key.selected = 0;
    key.mode = DefaultShadowMode(app) != 0 ? 1 : 0;
    key.distance = 0.01125f;
}

static void ResetGravityRecord(mdl::GravityKey& key) {
    key.selected = 0;
    key.noiseEnabled = 0;
    key.noise = 10;
    key.acceleration = 9.8000002f;
    key.direction[0] = 0.0f;
    key.direction[1] = -1.0f;
    key.direction[2] = 0.0f;
}

static void ResetAccessoryRecord(mdl::AccessoryKey& key) {
    key.selected = 0;
    key.parentBone = 0;
    key.visible = 1;
    key.shadowEnabled = 0;
    key.parentModel = -1;
    std::memset(key.position, 0, sizeof(key.position));
    std::memset(key.rotation, 0, sizeof(key.rotation));
    key.opacity = 1.0f;
    key.scale = 1.0f;
}

// VA 0x004316B0. Camera mode purges the five global track families; model
// mode delegates to the exact 0x4A09E0 model-key deletion backend.
void DeleteMarkedKeyframes(MMDApp* app) {
    if (app == nullptr) {
        return;
    }
    if (app->CameraMode() != 0) {
        auto* camera = app->CameraKeys();
        DeleteSelectedCameraKeys(camera);

        auto* light = app->LightKeys();
        if (light[0].selected != 0) {
            ResetLightRecord(light[0], true);
            light[0].next = 0;
        }
        for (std::uint32_t i = 1; i < 10000; ++i) {
            if (light[i].selected != 0) {
                UnlinkGlobalKey(light, i);
                ResetLightRecord(light[i], false);
            }
        }

        auto* shadow = app->ShadowKeys();
        if (shadow[0].selected != 0) {
            ResetShadowRecord(app, shadow[0]);
            shadow[0].next = 0;
        }
        for (std::uint32_t i = 1; i < 10000; ++i) {
            if (shadow[i].selected != 0) {
                UnlinkGlobalKey(shadow, i);
                ResetShadowRecord(app, shadow[i]);
            }
        }

        auto* gravity = app->GravityKeys();
        if (gravity[0].selected != 0) {
            ResetGravityRecord(gravity[0]);
            gravity[0].next = 0;
        }
        for (std::uint32_t i = 1; i < 10000; ++i) {
            if (gravity[i].selected != 0) {
                UnlinkGlobalKey(gravity, i);
                ResetGravityRecord(gravity[i]);
            }
        }

        for (std::uint32_t slot = 0; slot < 255; ++slot) {
            mdl::AccessoryKey* accessory = app->AccessoryKeys(slot);
            if (accessory == nullptr) {
                continue;
            }
            if (accessory[0].selected != 0) {
                ResetAccessoryRecord(accessory[0]);
            }
            for (std::uint32_t i = 1; i < 10000; ++i) {
                if (accessory[i].selected != 0) {
                    UnlinkAccessoryKey(accessory, i);
                    ResetAccessoryRecord(accessory[i]);
                }
            }
        }
    } else {
        mdl::ModelRecord* model = ActiveModel(app);
        if (model != nullptr)
            DeleteMarkedModelKeys(model, app->state.currentFrame);
    }

    PanelPaint(app);
    SelectionReeval(app);
    if (app->CameraMode() != 0) {
        ReloadModels(app);
        RefreshLightPanel(app);
        RefreshSelfShadowPanel(app);
        ApplyGravityTrack(app);
        for (int slot = 0; slot < 255; ++slot) {
            if (app->AccessoryKeys(slot) != nullptr) {
                ApplyAccessoryTrack(app, slot);
            }
        }
        SyncAccessoryEditPanel(app);
    } else {
        mdl::ModelRecord* model = ActiveModel(app);
        if (model != nullptr) {
            SeekModelFrame(*model, app->state.currentFrame, app->PlaybackPhysicsMode());
        }
    }
}

// The view-reset shared tail loc_47EFF6: PostViewRefresh + SelectTimelineTrack(GlobalTimelineTrack::Camera)
// (the 0x2F8!=0 fast paths and the gate-skip path all land here).
void ViewResetRefresh(MMDApp* app) {
    PostViewRefresh(app);
    SelectTimelineTrack(GlobalTimelineTrack::Camera);
}

void ResetCameraAttachmentBasis(MMDApp* app) {
    D3DMATRIX& basis = app->CameraAttachmentBasis();
    basis = {};
    basis.m[0][0] = 1.0f;
    basis.m[1][1] = 1.0f;
    basis.m[2][2] = 1.0f;
    basis.m[3][3] = 1.0f;
}

namespace {

enum class ViewPreset {
    Front,
    Back,
    Top,
    Left,
    Right
};

struct ViewPresetSettings {
    float rotation;
    bool useReferenceHeight;
    bool rotatePitch;
};

// Shared behavior of original cases 402..406 (0x47EEDF / 0x47F163 /
// 0x47F211 / 0x47F00B / 0x47F0B5). Keep the original float constants:
// flt_52B738 = PI, flt_52E8FC = -PI/2, flt_52E900 = +PI/2.
// Top view rotates pitch and always clears the height; the other presets
// rotate yaw and select height from the camera's bone-reference mode.
void ResetViewPreset(MMDApp* app, ViewPreset preset) {
    static constexpr ViewPresetSettings kPresets[] = {
        {0.0f, true, false},        // Front
        {3.1415927f, true, false},  // Back
        {-1.5707964f, false, true}, // Top
        {1.5707964f, true, false},  // Left
        {-1.5707964f, true, false}, // Right
    };
    const ViewPresetSettings& settings = kPresets[static_cast<std::size_t>(preset)];
    if (app->CameraMode() != 0) {
        app->CameraRotation()[0] = settings.rotatePitch ? settings.rotation : 0.0f;
        app->CameraRotation()[1] = settings.rotatePitch ? 0.0f : settings.rotation;
        app->CameraRotation()[2] = 0.0f;
        app->CameraDistance() = -45.0f;
        ViewResetRefresh(app);
        return;
    }
    const std::int32_t parentModelSlot = app->CameraParentModel();
    const std::uint8_t selectedModelSlot = app->SelectedModelSlot();
    const std::uint8_t followCameraEnabled = app->state.followCameraEnabled;
    const bool hasParentModel = parentModelSlot >= 0; // setnl cl
    const bool followingParentModelWhileStopped =
        (selectedModelSlot == parentModelSlot) && app->PlaybackActive() == 0 &&
        followCameraEnabled != 0; // setz bl / setz al / and / and
    if (hasParentModel && followingParentModelWhileStopped) {
        ViewResetRefresh(app); // test ecx, ebx; jnz loc_47EFF6
        return;
    }
    if (hasParentModel && followCameraEnabled != 0) {
        app->CameraAttachmentTransformSuppressed() = 1;
    }
    app->ViewOffsetX() = 0.0f;
    app->CameraDistance() = -45.0f;
    if (settings.useReferenceHeight) {
        if (app->CameraReferenceMode() == CameraAttachmentReference::SelectedBone) {
            app->ViewOffsetY() = 0.0f;
        } else {
            app->ViewOffsetY() = 10.0f;
        }
    } else {
        app->ViewOffsetY() = 0.0f;
    }
    app->CameraRotation()[0] = settings.rotatePitch ? settings.rotation : 0.0f;
    app->CameraRotation()[1] = settings.rotatePitch ? 0.0f : settings.rotation;
    app->CameraRotation()[2] = 0.0f;
    ResetCameraAttachmentBasis(app);
    ViewResetRefresh(app);
}

} // namespace

// The frame/30 conversion of case 408 (0x487446): fild (int -> double),
// fadd 4294967296.0 when negative (i.e. unsigned re-interpretation), then
// fdiv by 30.0 - stored as float.
float FrameToSeconds(std::int32_t v) {
    const double d = static_cast<double>(static_cast<std::uint32_t>(v));
    return static_cast<float>(d / 30.0); // dbl_52BA68
}

// First selected frame in the app+0x374 table (0x54 stride, selection byte
// +0x48); returns -1 when none found within the 0x2710-entry scan (cases
// 430/431/432, 0x2F8 != 0 branch).
std::int32_t FindFirstSelectedCameraKey(MMDApp* app) {
    auto* keys = app->CameraKeys();
    for (std::int32_t i = 0; i < 0x2710; ++i) {
        if (keys[i].selected != 0) {
            return i;
        }
    }
    return -1;
}

template <typename Key>
void SelectGlobalFrameRange(Key* keys, std::uint32_t from, std::uint32_t to) {
    for (std::uint32_t i = 0; i < 10000; ++i) {
        Key& key = keys[i];
        key.selected = 0;
        if (from <= key.frame && key.frame <= to && (i == 0 || key.frame != 0)) {
            key.selected = 1;
        }
    }
}

// First marked bone key; returns -1 when none is present.
std::int32_t FirstSelectedBoneKey(mdl::ModelRecord* model) {
    mdl::BoneKey* keys = model->boneKeys;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(mdl::kBoneKeyCapacity); ++i) {
        if (keys[i].allocated != 0) {
            return i;
        }
    }
    return -1;
}

static void ReadBoneCurveChannel(MMDApp* app, const mdl::BoneKey& key, std::size_t channel,
                                 std::size_t row) {
    app->state.lightA[row] = key.interpolation[channel];
    app->state.lightB[row] = key.interpolation[4 + channel];
    app->state.lightC[row] = key.interpolation[8 + channel];
    app->state.lightD[row] = key.interpolation[12 + channel];
}

static void WriteBoneCurveChannel(MMDApp* app, mdl::BoneKey& key, std::size_t channel,
                                  std::size_t row) {
    key.interpolation[channel] = app->state.lightA[row];
    key.interpolation[4 + channel] = app->state.lightB[row];
    key.interpolation[8 + channel] = app->state.lightC[row];
    key.interpolation[12 + channel] = app->state.lightD[row];
}

static void ResetBoneCurveChannel(mdl::BoneKey& key, std::size_t channel) {
    key.interpolation[channel] = 0x14;
    key.interpolation[4 + channel] = 0x14;
    key.interpolation[8 + channel] = 0x6B;
    key.interpolation[12 + channel] = 0x6B;
}

static void ReadCameraCurveChannel(MMDApp* app, const mdl::CameraKey& key, std::size_t channel,
                                   std::size_t row) {
    app->state.lightA[row] = key.interpolation[0][channel];
    app->state.lightB[row] = key.interpolation[1][channel];
    app->state.lightC[row] = key.interpolation[2][channel];
    app->state.lightD[row] = key.interpolation[3][channel];
}

static void WriteCameraCurveChannel(MMDApp* app, mdl::CameraKey& key, std::size_t channel,
                                    std::size_t row) {
    key.interpolation[0][channel] = app->state.lightA[row];
    key.interpolation[1][channel] = app->state.lightB[row];
    key.interpolation[2][channel] = app->state.lightC[row];
    key.interpolation[3][channel] = app->state.lightD[row];
}

static void ResetCameraCurveChannel(mdl::CameraKey& key, std::size_t channel) {
    key.interpolation[0][channel] = 0x14;
    key.interpolation[1][channel] = 0x14;
    key.interpolation[2][channel] = 0x6B;
    key.interpolation[3][channel] = 0x6B;
}

// Case-415 combo text compare (the original `repe cmpsb` of exactly `n`
// bytes against the literal, NUL position included - GetWindowTextA only
// zero-terminates, so a shorter edit text fails on the NUL byte).
static bool TextEqN(const char* buf, const char* lit, std::size_t n) {
    return memcmp(buf, lit, n) == 0;
}

// Case-415 frame-range editor combo literals.  The JP strings mix SJIS
// kanji with half-width katakana in the original (0x530BF0 "all frames",
// 0x530BD8 "all morph frames", 0x530BBC "all bone frames", 0x530BA4 "selected bones",
// 0x530B8C "selected morphs").
static const char kJpAllFrame[] = "\x91\x53\xCC\xDA\xB0\xD1";                  // all frames
static const char kJpAllFacial[] = "\x91\x53\x95\x5C\x8F\xEE\xCC\xDA\xB0\xD1"; // all morph frames
static const char kJpAllBone[] =
    "\x91\x53\x83\x7B\x81\x5B\x83\x93\xCC\xDA\xB0\xD1";                // all bone frames
static const char kJpSelBone[] = "\x91\x49\x91\xF0\xCE\xDE\xB0\xDD";   // selected bones
static const char kJpSelFacial[] = "\x91\x49\x91\xF0\x95\x5C\x8F\xEE"; // selected morphs
// 0x52D354: light-record name "ルート" (root) used by case 420.

template <typename Key>
static void MarkFrameSelected(Key* keys, std::uint32_t start, std::uint32_t frame) {
    std::uint32_t index = start;
    while (keys[index].frame < frame) {
        const std::uint32_t next = keys[index].next;
        if (next == 0) {
            break;
        }
        index = next;
    }
    if (keys[index].frame == frame) {
        keys[index].selected = 1;
    }
}

template <typename Key>
static void MarkModelFrameAllocated(Key* keys, std::uint32_t start, std::uint32_t frame) {
    std::uint32_t index = start;
    while (keys[index].frame < frame) {
        const std::uint32_t next = keys[index].next;
        if (next == 0) {
            break;
        }
        index = next;
    }
    if (keys[index].frame == frame) {
        keys[index].allocated = 1;
    }
}

template <typename Key>
static void SelectModelFrameRange(Key* keys, std::size_t capacity, std::uint32_t activeHeadCount,
                                  std::uint32_t from, std::uint32_t to) {
    for (std::size_t i = 0; i < capacity; ++i) {
        Key& key = keys[i];
        key.allocated = 0;
        if (from <= key.frame && key.frame <= to && (i < activeHeadCount || key.frame != 0)) {
            key.allocated = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// Case-437 strings (Shift-JIS byte-exact).
// 0x530C10: "Delete model: %s.\nAll of the model's frame data will also be deleted.\n
//            All items attached to this model, including accessories, will also be changed.\n
//            (This operation cannot be undone.)\n\nAre you sure you want to delete it?"
static const char kMsgDelModelJp[] =
    "\x83\x82\x83\x66\x83\x8b\x81\x46"
    "%s"
    "\x82\xF0\x8D\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7\n"
    "\x83\x82\x83\x66\x83\x8b\x82\xCC\x83\x74\x83\x8C\x81\x5B\x83\x80"
    "\x83\x66\x81\x5B\x83\x5E\x82\xE0\x91\x53\x82\xC4\x8D\xED\x8F\x9C"
    "\x82\xB3\x82\xEA\x82\xDC\x82\xB7\n"
    "\x82\xDC\x82\xBD\x83\x41\x83\x4E\x83\x5A\x83\x54\x83\x8A\x82\xC5"
    "\x82\xB1\x82\xCC\x83\x82\x83\x66\x83\x8B\x82\xC9\x92\xC7\x8F\x5D"
    "\x82\xB5\x82\xE9\x82\xE0\x82\xCC\x82\xCD\x92\x6E\x96\xCA\x82\xC9"
    "\x92\xC7\x8F\x5D\x82\xB5\x82\xE9\x82\xE6\x82\xA4\x82\xC9\x95\xCF"
    "\x8D\x58\x82\xB3\x82\xEA\x82\xDC\x82\xB7\n"
    "\x28\x82\xB1\x82\xCC\x91\x80\x8D\xEC\x82\xCD\x8C\xB3\x82\xC9"
    "\x96\xDF\x82\xB7\x8E\x96\x82\xCD\x82\xC5\x82\xAB\x82\xDC\x82\xB9"
    "\x82\xF1\x29\n\n"
    "\x8D\xED\x8F\x9C\x82\xB5\x82\xC4\x82\xE0\x82\xE6\x82\xEB\x82\xB5"
    "\x82\xA2\x82\xC5\x82\xB7\x82\xA9\x81\x48";
// 0x530C00: "Delete model"
static const char kCaptionDelModelJp[] = "\x83\x82\x83\x66\x83\x8b\x8d\xed\x8f\x9c";
// 0x530CF0: EN delete-model message.
static const char kMsgDelModelEn[] = "Trying to delete Model(%s). \n"
                                     "All flame data about this model will be deleted too.\n"
                                     "(This operation cannot undo!!)\n\nAre you OK?";

// Case-435 open-file strings (wide).
// 0x52DCE0: "All Model files(*.pmd,*.pmx)" (used for both locales)
static const wchar_t kFilterModel[] = L"All Model files(*.pmd,*.pmx)\x00*.pmd;*.pmx\x00";
// 0x52DCBC: "UserFile\\Model"
static const wchar_t kInitDirModel[] = L"UserFile\\Model";
// 0x52DCAC: "pmd;pmx" def-ext
static const wchar_t kDefExtPmd[] = L"pmd;pmx";
// 0x52DC94: "load model"
static const wchar_t kTitleLoadModel[] = L"load model";
// 0x52DC84: JP title "Open file"
static const wchar_t kTitleOpenFileJp[] = L"\x30D5\x30A1\x30A4\x30EB\x3092\x958B\x304F";

// ---------------------------------------------------------------------------
// Editor actions preserve command notification and refresh order.
// ---------------------------------------------------------------------------

// ------------------------------------------------------------------
// 400 (0x00482766): undo button 0x190.  Gated on byte 0x2F8; the
// paste-undo table model+0x26EC[28*cnt] and the cnt==cur equality both
// disable 0x190 / clear paste-mode byte 0x31BC; 0x191 enabled and mode
// byte 0x31BD = 1, then PanelPaint + SelectionReeval + SetFocus and
// undo-available flag 0x9EDB5.  The original falls through into
// def_47E903, a no-op for this id (control 0x190 != 0x1B4).
// ------------------------------------------------------------------
static void UndoKeyframeEdit(MMDApp* app, HWND hwnd) {
    if (app->CameraMode() != 0) {
        return; // was: break (switch exit)
    }
    app->SceneModified() = 1;
    mdl::ModelRecord* model = ActiveModel(app);
    mdl::ModelRecord& record = *model;
    UndoModelEdit(model, app->state.currentFrame);
    const std::int32_t cnt = record.UndoCursor();
    if (record.undoRings[0].slots[cnt].operation == mdl::UndoOperation::Empty) {
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), FALSE);
        record.undoDirty = 0;
        SetFocus(hwnd);
    }
    if (record.UndoCursor() == record.LatestUndoCursor()) {
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), FALSE);
        record.undoDirty = 0;
        SetFocus(hwnd);
    }
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), TRUE);
    record.redoDirty = 1;
    PanelPaint(app);
    SelectionReeval(app);
    SetFocus(hwnd);
    app->PhysicsResetPending() = 1;
}
// ------------------------------------------------------------------
// 401 (0x004828DF): redo button 0x191 - mirror of 400 with
// RedoModelEdit, 0x191/0x190 swapped and mode bytes inverted; when the
// undo-table entry model+0x26EC[28*cnt] != 1, SeekModelFrame is invoked
// (original: thiscall(ecx = model, frame = dword app+0x980,
// app+0xA0CC4) - the stub only takes this + pos).
// ------------------------------------------------------------------
static void RedoKeyframeEdit(MMDApp* app, HWND hwnd) {
    if (app->CameraMode() != 0) {
        return; // was: break (switch exit)
    }
    app->SceneModified() = 1;
    mdl::ModelRecord* model = ActiveModel(app);
    mdl::ModelRecord& record = *model;
    RedoModelEdit(model, app->state.currentFrame);
    if (record.UndoCursor() == record.LatestUndoCursor()) {
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
        record.redoDirty = 0;
        SetFocus(hwnd);
    }
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    record.undoDirty = 1;
    PanelPaint(app);
    SelectionReeval(app);
    const std::int32_t cnt = record.UndoCursor();
    if (record.undoRings[0].slots[cnt].operation != mdl::UndoOperation::BonePose) {
        // original: sub_4B4260(ecx = model, frame = app+0x980,
        // app+0xA0CC4)
        SeekModelFrame(*model, app->state.currentFrame, app->PlaybackPhysicsMode());
    }
    SetFocus(hwnd);
    app->PhysicsResetPending() = 1;
}
// ------------------------------------------------------------------
// 407 (0x0047F29C): view reset + model reload.  Fast path (0x2F8!=0):
// 0x310 = +PI/2, 0x314/0x318 = 0, angle -45, refresh.  Full path:
// gate (slot==0xA0430 && 0x330==0 && 0x9ED98!=0 && 0xA0430>=0 -> skip
// the reset, only refresh), else flag 0xA0478 = 0, 0x308/0x30C = 0,
// ReloadModels (0x42E640) + PostModelReload, refresh.
// ------------------------------------------------------------------
static void ResetViewAndReloadModels(MMDApp* app) {
    if (app->CameraMode() != 0) {
        app->CameraRotation()[0] = 1.5707964f;
        app->CameraRotation()[1] = 0.0f;
        app->CameraRotation()[2] = 0.0f;
        app->CameraDistance() = -45.0f;
        ViewResetRefresh(app);
        return; // was: break (switch exit)
    }
    const std::int32_t a0430 = app->CameraParentModel();
    const std::uint8_t slot = app->SelectedModelSlot();
    const bool hit = (slot == a0430) && app->PlaybackActive() == 0 &&
                     app->state.followCameraEnabled != 0 && a0430 >= 0;
    if (!hit) {
        app->CameraAttachmentTransformSuppressed() = 0;
        app->ViewOffsetX() = 0.0f;
        app->ViewOffsetY() = 0.0f;
        ReloadModels(app); // 0x42E640
        PostModelReload(app);
    }
    ViewResetRefresh(app);
}
// ------------------------------------------------------------------
// 408 (0x00487446): play-range edits.  When byte 0x330 != 0: clear it,
// StopPlayback, and (byte 0xA06CC != 0) 0x4C2680/0x4C2760 on the 0xCC
// subsystem.  Otherwise read the 0x199 (start) / 0x19A (end) edits via
// atol; frameA/30 (0x9E654) comes from dword 0x980 when byte 0x9ED99
// != 0 (0x9EDB6 = 1) or from the 0x199 edit (0x9EDB6 = 1 only when the
// edit EQUALS 0x980 - x64 0x7FF7CB46B6DA cmp/jnz; the x86 jnz/jz was
// transcribed inverted here before); frameB/30 (0x9E658) from the 0x19A edit
// (fallback dword 0x9E16C when 0); 0x9E64C = frameA/30; snapshot
// IsWindowEnabled of 0x1F1/0x1F2/0x1AF/0x1A5/0x1A6/0x190/0x191 into
// 0x9EB77..0x9EB7D; byte 0x330 = 1; UpdateBoneFrames; then
// KillTimer(0x64) + 0x4C2680/0x4C2760 when 0xA02B6, and 0x4C2B80 +
// WaveSeekAndFeed((double)0x9E654) when 0xA03E9 == 0 (both gated on 0xA06CC);
// finally timeGetTime copies 0x9EDA8/0x9EDAC.
// ------------------------------------------------------------------
static void ApplyPlaybackRangeEdits(MMDApp* app, HWND hwnd) {
    if (app->PlaybackActive() != 0) {
        app->PlaybackActive() = 0;
        StopPlayback(app);
        if (app->WaveEnabled() != 0) {
            CloseDataFile(app->Audio());
            WaveStartPlayback(app->Audio());
        }
        return; // was: break (switch exit)
    }
    char text[0x100];
    GetWindowTextA(GetDlgItem(hwnd, panel::kPlayStartFrameEdit), text, 8);
    const std::int32_t start = atol(text); // ebx
    GetWindowTextA(GetDlgItem(hwnd, panel::kPlayStopFrameEdit), text, 8);
    std::int32_t end = atol(text); // eax
    if (app->PlaybackStartsAtCurrentFrame() != 0) {
        app->PlaybackStartSeconds() = FrameToSeconds(app->state.currentFrame);
        app->PlaybackFrameChanged() = 1;
    } else {
        app->PlaybackStartSeconds() = FrameToSeconds(start);
        // x64 0x7FF7CB46B6DA: cmp start, currentFrame; jnz skip ->
        // the changed-flag is set only when the typed start frame
        // EQUALS the current frame (not "differs").
        if (start == app->state.currentFrame) {
            app->PlaybackFrameChanged() = 1;
        }
    }
    if (end == 0) {
        end = app->state.lastRegisteredFrame;
    }
    app->PlaybackEndSeconds() = FrameToSeconds(end);
    app->PlaybackCursorSeconds() = app->PlaybackStartSeconds();
    app->state.playbackEnabledSnapshot[0] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kBonePasteButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[1] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kBoneReversePasteButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[4] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kCurvePasteButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[2] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kPasteButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[3] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kReversePasteButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[5] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kUndoButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[6] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kRedoButton)) ? 1 : 0;
    app->PlaybackActive() = 1;
    UpdateBoneFrames(app); // 0x433A40
    if (app->WaveEnabled() != 0) {
        if (app->AudioSeekReady() != 0) {
            app->AudioSeekReady() = 0;
            KillTimer(hwnd, 0x64);
            CloseDataFile(app->Audio());
            WaveStartPlayback(app->Audio());
        }
        if (app->AutomaticFrameAdvanceEnabled() == 0) {
            SetFrameNormalized(app->FrameNormalization());
            WaveSeekAndFeed(app->Audio(), static_cast<double>(app->PlaybackStartSeconds()));
        }
    }
    app->PlaybackClockAnchorLow() = app->TimeNowLow();
    app->PlaybackClockAnchorHigh() = app->TimeNowHigh();
}
// ------------------------------------------------------------------
// 415 (0x004829E4): frame-range editor.  Edits 0x1A9/0x1AA hold the
// from/to frames (atol); 0x1B2 holds the target text/combo.  With
// byte 0x2F8 == 0 the 0x1B2 text is compared byte-exact (repe cmpsb
// over N bytes incl. the NUL) against JP "all frames"/"All frame" (bone+morph+
// camera sweep), JP "all morph frames"/"All facial" (morph), JP "all bone frames"/
// "All bone" (bone), JP "selected bones"/"Sel Bone" and JP "selected morphs"/"Sel facial"
// (0x4A27F0 per selected item), else 0x4A27F0 with the raw text.
// With 0x2F8 != 0 the 0x1B2 combo cursor selects the app-level table
// (0x374/0x378/0x37C/0x380 or the accessory slot whose byte +0x49D
// equals cursor-4, scanning its 0x384 blob).  Every sweep clears the
// mark byte first, then sets it when from <= frame <= to (unsigned)
// and (first entry or frame != 0); the model-mode bone/morph sweeps
// additionally require (counter < count || frame != 0).  Tail:
// PanelPaint + SelectionReeval + SetFocus.
// ------------------------------------------------------------------
static void SelectKeyframesInRange(MMDApp* app, HWND hwnd) {
    char text[0x100];
    GetWindowTextA(GetDlgItem(hwnd, panel::kRangeStartEdit), text, 8);
    const std::int32_t from = atol(text); // ebx
    GetWindowTextA(GetDlgItem(hwnd, panel::kRangeStopEdit), text, 8);
    const std::int32_t to = atol(text); // var_A40
    GetWindowTextA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), text, 20);
    if (app->CameraMode() == 0) {
        // ---- name-compare chain (loc_482CF2..loc_4831CB) ----
        if (TextEqN(text, kJpAllFrame, 7) || TextEqN(text, "All frame", 10)) {
            // bone sweep (loc_482D40..482DB2)
            {
                mdl::ModelRecord* model = ActiveModel(app);
                SelectModelFrameRange(model->boneKeys, mdl::kBoneKeyCapacity, model->boneCount,
                                      static_cast<std::uint32_t>(from),
                                      static_cast<std::uint32_t>(to));
            }
            // morph sweep (loc_482DE0..482E52, gated on 0x2D80 > 0)
            {
                mdl::ModelRecord* model = ActiveModel(app);
                const std::uint32_t morphCount = model->morphCount;
                if (morphCount > 0) {
                    SelectModelFrameRange(model->morphKeys, mdl::kMorphKeyCapacity, morphCount,
                                          static_cast<std::uint32_t>(from),
                                          static_cast<std::uint32_t>(to));
                }
            }
            // camera sweep (loc_482E60..482ED8)
            {
                mdl::ModelRecord* model = ActiveModel(app);
                SelectModelFrameRange(model->displayKeys, mdl::kDisplayKeyCapacity, 1,
                                      static_cast<std::uint32_t>(from),
                                      static_cast<std::uint32_t>(to));
            }
        } else if (TextEqN(text, kJpAllFacial, 11) || TextEqN(text, "All facial", 11)) {
            // morph sweep only (loc_482F30..482FA2)
            mdl::ModelRecord* model = ActiveModel(app);
            const std::uint32_t morphCount = model->morphCount;
            if (morphCount > 0) {
                SelectModelFrameRange(model->morphKeys, mdl::kMorphKeyCapacity, morphCount,
                                      static_cast<std::uint32_t>(from),
                                      static_cast<std::uint32_t>(to));
            }
        } else if (TextEqN(text, kJpAllBone, 13) || TextEqN(text, "All bone", 9)) {
            // bone sweep only (loc_482FF2..483064)
            mdl::ModelRecord* model = ActiveModel(app);
            SelectModelFrameRange(model->boneKeys, mdl::kBoneKeyCapacity, model->boneCount,
                                  static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(to));
        } else if (TextEqN(text, kJpSelBone, 9) || TextEqN(text, "Sel Bone", 9)) {
            // selected-bone sweep (loc_4830D0..48310F): 0x4A27F0 per
            // selected bone (0x2D94 byte array), name at 0x26BC +
            // i*0x25C
            mdl::ModelRecord* model = ActiveModel(app);
            const std::int32_t boneCount = model->boneCount;
            if (boneCount > 0) {
                const mdl::ModelRecord& record = *model;
                unsigned char* sel = record.boneSelection;
                const mdl::BoneRecord* bones = record.boneTable;
                for (std::int32_t i = 0; i < boneCount; ++i) {
                    if (sel[i] != 0) {
                        MarkKeyTrackRangeByName(model, static_cast<std::uint32_t>(from),
                                                static_cast<std::uint32_t>(to), bones[i].name);
                    }
                }
            }
        } else if (TextEqN(text, kJpSelFacial, 9) || TextEqN(text, "Sel facial", 11)) {
            // selected-morph sweep (loc_483173..4831B2): morph table
            // 0x26DC, 0x2E stride, selected byte +0x2C, count byte
            // 0x2DAC
            mdl::ModelRecord* model = ActiveModel(app);
            const mdl::ModelRecord& record = *model;
            const std::uint8_t morphCount = record.facialFrameCount;
            if (morphCount != 0) {
                mdl::FrameGroup* groups = record.displayFrames;
                for (std::uint8_t i = 0; i < morphCount; ++i) {
                    mdl::FrameGroup& group = groups[i];
                    if (group.selected != 0) {
                        MarkKeyTrackRangeByName(model, static_cast<std::uint32_t>(from),
                                                static_cast<std::uint32_t>(to), group.name);
                    }
                }
            }
        } else {
            // fallback (loc_4831B6..4831CB): treat the text as a name
            mdl::ModelRecord* model = ActiveModel(app);
            MarkKeyTrackRangeByName(model, static_cast<std::uint32_t>(from),
                                    static_cast<std::uint32_t>(to), text);
        }
        PanelPaint(app);
        SelectionReeval(app);
        SetFocus(hwnd);
    } else {
        // ---- 0x2F8 != 0: CB_GETCURSEL(0x1B2) dispatch ----
        // (loc_482A92..loc_482CED)
        const LRESULT sel =
            SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_GETCURSEL, 0, 0);
        if (sel == 0) {
            SelectGlobalFrameRange(app->CameraKeys(), static_cast<std::uint32_t>(from),
                                   static_cast<std::uint32_t>(to));
        } else if (sel == 1) {
            SelectGlobalFrameRange(app->LightKeys(), static_cast<std::uint32_t>(from),
                                   static_cast<std::uint32_t>(to));
        } else if (sel == 2) {
            SelectGlobalFrameRange(app->ShadowKeys(), static_cast<std::uint32_t>(from),
                                   static_cast<std::uint32_t>(to));
        } else if (sel == 3) {
            SelectGlobalFrameRange(app->GravityKeys(), static_cast<std::uint32_t>(from),
                                   static_cast<std::uint32_t>(to));
        } else {
            // accessory slot scan (loc_482C54..loc_482CED): the slot
            // whose byte +0x49D equals cursor-4; its AccessoryKey
            // timeline is swept.
            for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
                mdl::AccessoryRecord* acc = app->AccessorySlot(slot);
                if (acc != nullptr &&
                    static_cast<unsigned int>(acc->order) == static_cast<unsigned int>(sel - 4)) {
                    auto* keys = app->AccessoryKeys(slot);
                    SelectGlobalFrameRange(keys, static_cast<std::uint32_t>(from),
                                           static_cast<std::uint32_t>(to));
                    break;
                }
            }
        }
        PanelPaint(app);
        SelectionReeval(app);
        SetFocus(hwnd);
    }
}
// ------------------------------------------------------------------
// 416 (0x0048073E): selection-mark synchronisation.  For every entry
// of each frame table whose selection mark is set, the equal-frame
// entry of the *other* tables is marked via their frame-sorted linked
// lists (frame dword +0, next index +8; walks start at the current
// index for the model-mode bone/morph searches and at the head
// otherwise).  0x2F8 == 0: tables 0x26E0 (+0x38) / 0x26E4 (+0x10) /
// 0x26E8 (+0x14), camera entries only propagate into bone+morph.
// 0x2F8 != 0: app tables 0x374 (+0x48) / 0x378 (+0x24) / 0x37C
// (+0x14) / 0x380 (+0x21) plus the 0x384 accessory blobs (+0x18),
// then every marked blob entry re-marks the four app tables and the
// other blobs.  Tail: PanelPaint + SelectionReeval (no SetFocus).
// ------------------------------------------------------------------
static void SyncKeyframeSelection(MMDApp* app) {
    if (app->CameraMode() == 0) {
        // ---- model mode (loc_480D1D..loc_4810F3) ----
        mdl::ModelRecord* model = ActiveModel(app);
        mdl::ModelRecord* modelRecord = model;
        mdl::BoneKey* boneKeys = model->boneKeys;
        mdl::MorphKey* morphKeys = model->morphKeys;
        mdl::DisplayKey* displayKeys = model->displayKeys;

        // loop A: bone marks -> bone/morph/camera
        for (std::uint32_t keyIndex = 0;
             keyIndex < static_cast<std::uint32_t>(mdl::kBoneKeyCapacity); ++keyIndex) {
            if (boneKeys[keyIndex].allocated != 0) {
                const std::uint32_t frame = boneKeys[keyIndex].frame;
                if (modelRecord->boneCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->boneCount; ++i) {
                        MarkModelFrameAllocated(boneKeys, i, frame);
                    }
                }
                if (modelRecord->morphCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->morphCount; ++i) {
                        MarkModelFrameAllocated(morphKeys, i, frame);
                    }
                }
                MarkModelFrameAllocated(displayKeys, 0, frame);
            }
        }
        // loop B: morph marks -> bone/morph/camera
        for (std::uint32_t keyIndex = 0; keyIndex < mdl::kMorphKeyCapacity; ++keyIndex) {
            if (morphKeys[keyIndex].allocated != 0) {
                const std::uint32_t frame = morphKeys[keyIndex].frame;
                if (modelRecord->boneCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->boneCount; ++i) {
                        MarkModelFrameAllocated(boneKeys, i, frame);
                    }
                }
                if (modelRecord->morphCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->morphCount; ++i) {
                        MarkModelFrameAllocated(morphKeys, i, frame);
                    }
                }
                MarkModelFrameAllocated(displayKeys, 0, frame);
            }
        }
        // loop C: camera marks -> bone/morph only
        for (std::uint32_t keyIndex = 0; keyIndex < mdl::kDisplayKeyCapacity; ++keyIndex) {
            if (displayKeys[keyIndex].allocated != 0) {
                const std::uint32_t frame = displayKeys[keyIndex].frame;
                if (modelRecord->boneCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->boneCount; ++i) {
                        MarkModelFrameAllocated(boneKeys, i, frame);
                    }
                }
                if (modelRecord->morphCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->morphCount; ++i) {
                        MarkModelFrameAllocated(morphKeys, i, frame);
                    }
                }
            }
        }
        PanelPaint(app);
        SelectionReeval(app);
    } else {
        // ---- camera mode (loc_480750..loc_480D18) ----
        auto* cameraKeys = app->CameraKeys();
        auto* lightKeys = app->LightKeys();
        auto* shadowKeys = app->ShadowKeys();
        auto* gravityKeys = app->GravityKeys();

        const auto markAccessories = [app](std::uint32_t frame, std::int32_t excludedSlot) {
            for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
                if (slot == excludedSlot || app->AccessorySlot(slot) == nullptr) {
                    continue;
                }
                auto* keys = app->AccessoryKeys(slot);
                MarkFrameSelected(keys, 0, frame);
            }
        };

        for (std::int32_t i = 0; i < 0x2710; ++i) {
            if (cameraKeys[i].selected != 0) {
                const std::uint32_t frame = cameraKeys[i].frame;
                MarkFrameSelected(lightKeys, 0, frame);
                MarkFrameSelected(shadowKeys, 0, frame);
                MarkFrameSelected(gravityKeys, 0, frame);
                markAccessories(frame, -1);
            }
            if (lightKeys[i].selected != 0) {
                const std::uint32_t frame = lightKeys[i].frame;
                MarkFrameSelected(cameraKeys, 0, frame);
                MarkFrameSelected(shadowKeys, 0, frame);
                MarkFrameSelected(gravityKeys, 0, frame);
                markAccessories(frame, -1);
            }
            if (shadowKeys[i].selected != 0) {
                const std::uint32_t frame = shadowKeys[i].frame;
                MarkFrameSelected(cameraKeys, 0, frame);
                MarkFrameSelected(lightKeys, 0, frame);
                MarkFrameSelected(gravityKeys, 0, frame);
                markAccessories(frame, -1);
            }
            if (gravityKeys[i].selected != 0) {
                const std::uint32_t frame = gravityKeys[i].frame;
                MarkFrameSelected(cameraKeys, 0, frame);
                MarkFrameSelected(lightKeys, 0, frame);
                MarkFrameSelected(shadowKeys, 0, frame);
                markAccessories(frame, -1);
            }
        }
        // accessory-blob marks (loc_480B7C..480D04): each marked blob
        // entry re-marks the four app tables and every *other* blob
        for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
            if (app->AccessorySlot(slot) == nullptr) {
                continue;
            }
            auto* keys = app->AccessoryKeys(slot);
            for (std::int32_t i = 0; i < 0x2710; ++i) {
                if (keys[i].selected == 0) {
                    continue;
                }
                const std::uint32_t frame = keys[i].frame;
                MarkFrameSelected(cameraKeys, 0, frame);
                MarkFrameSelected(lightKeys, 0, frame);
                MarkFrameSelected(shadowKeys, 0, frame);
                MarkFrameSelected(gravityKeys, 0, frame);
                markAccessories(frame, slot);
            }
        }
        PanelPaint(app);
        SelectionReeval(app);
    }
}
// ------------------------------------------------------------------
// 430 (0x00481109): light colour <- selected frame.  Combo 0x1B1
// cursor selects the source row; the source frame is the first frame
// whose selection mark is set (0x2F8 != 0: app+0x374 table, 0x54
// stride, mark +0x48, 0x2710 limit; 0x2F8 == 0: model+0x26E0, 0x3C
// stride, mark +0x38, kBoneKeyCapacity limit).  With cursor < 6 (or < 4) a
// single frame is copied into all rows, otherwise the first 6 (4)
// frames are copied row by row.  Colour bytes: R/G/B/aux at +0x28/
// +0x2E/+0x34/+0x3A (0x54-stride) or +0x0C/+0x10/+0x14/+0x18
// (0x3C-stride) -> 0x9DA0A/0x9DA10/0x9DA16/0x9DA1C rows.  Ends with
// EnableWindow(0x1AF) + SetFocus.
// ------------------------------------------------------------------
static void CopyInterpolationCurve(MMDApp* app, HWND hwnd) {
    const LRESULT sel =
        SendMessageA(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_GETCURSEL, 0, 0);
    if (app->CameraMode() != 0) {
        const std::int32_t idx = FindFirstSelectedCameraKey(app);
        if (idx < 0) {
            SetFocus(hwnd);
            return; // was: break (switch exit)
        }
        EnableWindow(GetDlgItem(hwnd, panel::kCurvePasteButton), TRUE);
        auto* keys = app->CameraKeys();
        for (std::int32_t i = 0; i < 6; ++i) {
            const std::int32_t row = (sel < 6) ? sel : i;
            ReadCameraCurveChannel(app, keys[idx], row, i);
        }
        SetFocus(hwnd);
    } else {
        mdl::ModelRecord* model = ActiveModel(app);
        const std::int32_t idx = FirstSelectedBoneKey(model);
        if (idx < 0) {
            SetFocus(hwnd);
            return; // was: break (switch exit)
        }
        EnableWindow(GetDlgItem(hwnd, panel::kCurvePasteButton), TRUE);
        mdl::BoneKey* keys = model->boneKeys;
        if (sel < 4) {
            // 6 rows duplicated from the single frame at idx+sel
            for (std::int32_t i = 0; i < 6; ++i) {
                ReadBoneCurveChannel(app, keys[idx], static_cast<std::size_t>(sel), i);
            }
        } else {
            // All four interpolation channels into the first four rows.
            for (std::int32_t i = 0; i < 4; ++i) {
                ReadBoneCurveChannel(app, keys[idx], i, i);
            }
        }
        SetFocus(hwnd);
    }
}
// ------------------------------------------------------------------
// 431 (0x00481430): light colour -> selected frames (inverse of 430).
// Every frame with its selection mark set receives the colour of the
// combo-0x1B1 row (single frame at cursor offset) or all 6/4 rows;
// byte 0xA0B0D dirty; 0x2F8==0 branch runs SnapshotSelectedKeysForUndo first and uses
// the model+0x26E0 table; ends SelectionReeval + SetFocus.
// ------------------------------------------------------------------
static void PasteInterpolationCurve(MMDApp* app, HWND hwnd) {
    app->SceneModified() = 1;
    const LRESULT sel =
        SendMessageA(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_GETCURSEL, 0, 0);
    if (app->CameraMode() != 0) {
        auto* keys = app->CameraKeys();
        for (std::int32_t i = 0; i < 10000; ++i) {
            mdl::CameraKey& key = keys[i];
            if (key.selected == 0) {
                continue;
            }
            if (sel < 6) {
                WriteCameraCurveChannel(app, key, static_cast<std::size_t>(sel), sel);
            } else {
                for (std::int32_t channel = 0; channel < 6; ++channel) {
                    WriteCameraCurveChannel(app, key, channel, channel);
                }
            }
        }
        SelectionReeval(app);
        SetFocus(hwnd);
    } else {
        mdl::ModelRecord* model = ActiveModel(app);
        // original: 0x4A1510(ecx = model, frame = dword app+0x980)
        SnapshotSelectedKeysForUndo(model, app->state.currentFrame);
        mdl::BoneKey* keys = model->boneKeys;
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(mdl::kBoneKeyCapacity); ++i) {
            mdl::BoneKey& key = keys[i];
            if (key.allocated == 0) {
                continue;
            }
            if (sel < 4) {
                WriteBoneCurveChannel(app, key, static_cast<std::size_t>(sel), sel);
            } else {
                for (std::int32_t channel = 0; channel < 4; ++channel) {
                    WriteBoneCurveChannel(app, key, channel, channel);
                }
            }
        }
        SelectionReeval(app);
        SetFocus(hwnd);
    }
}
// ------------------------------------------------------------------
// 432 (0x004816E6): light colour reset - same sweep as 431 but the
// colour bytes are fixed to 0x14/0x14/0x6B/0x6B instead of the row
// values (0x2F8==0 branch uses model+0x26E0 with +0x0C/+0x10/+0x14/
// +0x18; 0x2F8!=0 uses app+0x374 with +0x28/+0x2E/+0x34/+0x3A).
// ------------------------------------------------------------------
static void ResetInterpolationCurves(MMDApp* app, HWND hwnd) {
    app->SceneModified() = 1;
    const LRESULT sel =
        SendMessageA(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_GETCURSEL, 0, 0);
    if (app->CameraMode() != 0) {
        auto* keys = app->CameraKeys();
        for (std::int32_t i = 0; i < 10000; ++i) {
            mdl::CameraKey& key = keys[i];
            if (key.selected == 0) {
                continue;
            }
            if (sel < 6) {
                ResetCameraCurveChannel(key, static_cast<std::size_t>(sel));
            } else {
                for (std::int32_t channel = 0; channel < 6; ++channel) {
                    ResetCameraCurveChannel(key, channel);
                }
            }
        }
        SelectionReeval(app);
        SetFocus(hwnd);
    } else {
        mdl::ModelRecord* model = ActiveModel(app);
        // original: 0x4A1510(ecx = model, frame = dword app+0x980)
        SnapshotSelectedKeysForUndo(model, app->state.currentFrame);
        mdl::BoneKey* keys = model->boneKeys;
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(mdl::kBoneKeyCapacity); ++i) {
            mdl::BoneKey& key = keys[i];
            if (key.allocated == 0) {
                continue;
            }
            if (sel < 4) {
                ResetBoneCurveChannel(key, static_cast<std::size_t>(sel));
            } else {
                for (std::int32_t channel = 0; channel < 4; ++channel) {
                    ResetBoneCurveChannel(key, channel);
                }
            }
        }
        SelectionReeval(app);
        SetFocus(hwnd);
    }
}
// ------------------------------------------------------------------
// 435 (0x0047EB7B): load model.  SetCurrentDirectoryW(ExeDir), dirty
// 0xBC, zeroed 0x200-wide file buffer + OPENFILENAMEW (0x4C),
// owner = dword 0xA0D38 ?: hwnd, filter "All Model files(*.pmd,*.pmx)"
// (both locales), initial dir = DirModel() when menu 0x12D checked
// else "UserFile\\Model", def-ext "pmd;pmx", title "load model" (JP:
// JP "Open file"); on OK and menu gate: ExtractDirFromPath(app+0x9F338,
// path) -> CopyDirPathW(DirModel(), dir); LoadModelFile(app, path);
// always SelectTimelineTrack(GlobalTimelineTrack::Camera) (also when cancelled).
// ------------------------------------------------------------------
static void OpenModelFile(MMDApp* app, HWND hwnd) {
    SetCurrentDirectoryW(app->ExeDir());
    app->state.enterKeyState = 1;
    wchar_t fileBuf[0x100];
    memset(fileBuf, 0, sizeof(fileBuf));
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner =
        app->state.floatingWindow != 0 ? reinterpret_cast<HWND>(app->state.floatingWindow) : hwnd;
    ofn.lpstrFilter = kFilterModel;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = 0x100;
    ofn.Flags = OFN_FILEMUSTEXIST;
    ofn.lpstrInitialDir = (GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) != 0
                              ? app->DirModel()
                              : kInitDirModel;
    ofn.lpstrDefExt = kDefExtPmd;
    ofn.nMaxFileTitle = 0x100;
    ofn.lpstrFileTitle = nullptr;
    ofn.lpstrTitle = app->state.englishUI != 0 ? kTitleLoadModel : kTitleOpenFileJp;
    if (GetOpenFileNameW(&ofn) != 0) {
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) != 0) {
            wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, fileBuf);
            CopyDirPathW(app->DirModel(), dir);
        }
        LoadModelFile(app, fileBuf);
    }
    SelectTimelineTrack(GlobalTimelineTrack::Camera);
}
// ------------------------------------------------------------------
// 437 (0x0047FCF5): delete model selected in combo 0x1B4.  Gate
// dword[0xA0B50]; CB_GETCURSEL(0x1B4) -> slot lookup by model byte
// +0x2D7C (x64 has 255 slots: search loop 0x7FF7CB460A73 mov r13d,0FFh,
// any-model loop 0x7FF7CB460C2C and per-slot fixup loop 0x7FF7CB460DB5 all use
// r13=0xFF as the bound); confirm MessageBox (JP caption "Delete model"
// unconditionally; EN text "Trying to delete Model(%s)...", JP
// JP "Delete model: %s..."; name at model+0x227A EN / +0x2248 JP;
// flags 0x40001 when 0xA0D38 else 1); teardown of the model-edit
// sub-window (SeekSelectedModelToCurrentFrame, free 0xA0B7C/0xA0C30, DestroyWindow 0xA0B74,
// enable 0x1B4/0x198); dispose model (0x40A710(model, 1)), clear the
// slot, 0xA042C = any-model flag; combo rebuild (CB_RESETCONTENT
// 0x1B4/0x1DA/0x1C1 with old id, CB_SETCURSEL 0x1B4, CB_GETLBTEXT +
// CB_ADDSTRING 0x1D7 -> 0x1B2); menu 0x120/0x121 enable, then per
// remaining model: menu disable + id fixups (2D7C/2D7D decrement, bone
// table 0x4CCE4, camera frame pointers model+0x26E8); when the deleted
// slot was the edited one (0xA0430) reset it and re-sync combos 0x1C1/
// 0x1C2; clear bone-frame references (0x374 +0x4C/+0x50) and
// accessory-frame references (0x9DD70 +0x230/+0x234 and the 0x384
// blob +0x10/+0x0C); ApplyModelComboSelection.
// ------------------------------------------------------------------
static void DeleteSelectedModel(MMDApp* app, HWND hwnd) {
    if (app->EnglishNameDialog() != nullptr) {
        return; // was: break (switch exit)
    }
    const LRESULT sel = SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_GETCURSEL, 0, 0);
    app->state.enterKeyState = 1;
    std::int32_t found = -1;
    // Slot search loop: x64 0x7FF7CB460A73 mov r13d,0FFh (bound 255)
    for (std::int32_t i = 0; i < kModelSlotCount; ++i) {
        mdl::ModelRecord* m = app->ModelSlot(i);
        if (m != nullptr && m->comboSelIndex == sel) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        return; // was: break (switch exit)
    }
    app->state.enterKeyState = 1;
    mdl::ModelRecord* model = app->ModelSlot(found);
    const mdl::ModelRecord& record = *model;
    char text[0x100];
    if (app->state.englishUI != 0) {
        sprintf_s(text, 0x100, kMsgDelModelEn, record.nameEn);
    } else {
        sprintf_s(text, 0x100, kMsgDelModelJp, record.name);
    }
    const std::uint32_t flags =
        app->state.floatingWindow != 0 ? (MB_OKCANCEL | MB_TOPMOST) : MB_OKCANCEL;
    if (MessageBoxA(hwnd, text, kCaptionDelModelJp, flags) != IDOK) {
        return; // was: break (switch exit)
    }
    if (app->FrameCopyOrPhysicsDialog() != 0) {
        SeekSelectedModelToCurrentFrame(app);
        if (app->RigidScratchArray() != nullptr) {
            free(app->RigidScratchArray());
            app->RigidScratchArray() = nullptr;
        }
        if (app->JointScratchArray() != nullptr) {
            free(app->JointScratchArray());
            app->JointScratchArray() = nullptr;
        }
        DestroyWindow(reinterpret_cast<HWND>(app->FrameCopyOrPhysicsDialog()));
        app->FrameCopyOrPhysicsDialog() = nullptr;
        EnableWindow(GetDlgItem(hwnd, panel::kMainComboModel), TRUE);
        EnableWindow(GetDlgItem(hwnd, panel::kPlayButton), TRUE);
    }
    const std::int32_t oldSel = record.comboSelIndex;
    const std::int32_t oldIdx = record.comboSelIndex2;
    if (model != nullptr) {
        DeleteModel(model, 1); // model dispose (thiscall)
    }
    app->ModelSlot(found) = nullptr;
    app->SceneModified() = 1;
    app->state.mainModelComboSelection = 0;
    // Any-model scan loop: x64 0x7FF7CB460C2C mov rcx,r13 (=0xFF, same bound)
    for (std::int32_t i = 0; i < kModelSlotCount; ++i) {
        if (app->ModelSlot(i) != nullptr) {
            app->state.mainModelComboSelection = 1;
        }
    }
    // combo rebuild: 0x1B4 / 0x1DA / 0x1C1 reset with the old id as
    // wParam, 0x1B4 cursor 0, accessory names 0x1D7 -> 0x1B2
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_DELETESTRING,
                 static_cast<WPARAM>(sel), 0);
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboGround), CB_DELETESTRING,
                 static_cast<WPARAM>(sel), 0);
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboNormal), CB_DELETESTRING,
                 static_cast<WPARAM>(sel), 0);
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_SETCURSEL, 0, 0);
    const LRESULT accCount =
        SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo), CB_GETCOUNT, 0, 0);
    for (std::int32_t i = 0; i < accCount; ++i) {
        char buf[0x100];
        SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo), CB_GETLBTEXT, static_cast<WPARAM>(i),
                     reinterpret_cast<LPARAM>(buf));
        SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(buf));
    }
    EnableMenuItem(GetMenu(hwnd), MDS_CMD_MODEL_DRAW_ORDER, 1);
    EnableMenuItem(GetMenu(hwnd), MDS_CMD_MODEL_CALCULATION_ORDER, 1);
    // Per-slot fixup: x64 0x7FF7CB460DB5 mov rax,r13 (=0xFF) + trailing dec rax/jne
    for (std::int32_t j = 0; j < kModelSlotCount; ++j) {
        mdl::ModelRecord* m = app->ModelSlot(j);
        if (m == nullptr) {
            continue;
        }
        EnableMenuItem(GetMenu(hwnd), MDS_CMD_MODEL_DRAW_ORDER, 0);
        EnableMenuItem(GetMenu(hwnd), MDS_CMD_MODEL_CALCULATION_ORDER, 0);
        if (m->comboSelIndex > oldSel) {
            --m->comboSelIndex;
        }
        if (m->comboSelIndex2 > oldIdx) {
            --m->comboSelIndex2;
        }
        // bone parent table 0x4CCE4 (0x14 stride) - entries pointing at
        // the deleted slot index are severed
        mdl::ModelRecord& record = *m;
        mdl::DetachBoneBindings(record.boneOrderTable, record.boneOrderCount, found);
        mdl::DisplayKey* displayKeys = m->displayKeys;
        for (std::int32_t keyIndex = 0; keyIndex < static_cast<int>(mdl::kDisplayKeyCapacity);
             ++keyIndex) {
            auto* states = mdl::SelectorStates(displayKeys[keyIndex]);
            const std::int32_t cnt = record.boneOrderCount;
            for (std::int32_t k = 0; k < cnt; ++k) {
                if (states[k].modelIndex == found) {
                    states[k].modelIndex = -1;
                    states[k].boneIndex = 0;
                }
            }
        }
    }
    if (app->CameraParentModel() == found) {
        app->CameraParentModel() = -1;
        app->CameraParentBone() = 0;
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboNormal), CB_SETCURSEL, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneRegisterCombo), CB_RESETCONTENT, 0, 0);
    }
    // Clear camera key attachment references to the deleted model.
    {
        auto* keys = app->CameraKeys();
        for (std::int32_t i = 0; i < 10000; ++i) {
            if (keys[i].parentModel == found) {
                keys[i].parentModel = -1;
                keys[i].parentBone = 0;
            }
        }
    }
    // clear accessory references (0x9DD70 slots + 0x384 frame blobs)
    for (std::int32_t i = 0; i < 0xFF; ++i) {
        mdl::AccessoryRecord* acc = app->AccessorySlot(i);
        if (acc == nullptr) {
            continue;
        }
        if (acc->parentModel == found) {
            acc->parentModel = -1;
            acc->parentBone = 0;
        }
        auto* keys = app->AccessoryKeys(i);
        if (keys != nullptr) {
            for (std::int32_t keyIndex = 0; keyIndex < 10000; ++keyIndex) {
                mdl::AccessoryKey& key = keys[keyIndex];
                if (key.parentModel == found) {
                    key.parentModel = -1;
                    key.visible = 0;
                }
            }
        }
    }
    ApplyModelComboSelection(app);
}
// ------------------------------------------------------------------
// 442 (0x0048D759): accessory edit dialog.  Dirty 0xBC, dialog flag
// byte 0xA0665 = 1, DialogBoxParamA(instance, template 0x329 EN /
// 0x328 JP, hwnd, sub_47A3F0, 0); on result != 2 (cancelled): dirty
// 0xA0B0D, flag 0xA0665 = 0, free the dialog buffers 0xA0B1C /
// 0xA0B24 / 0xA0668; when byte 0xA0664 != 0:
// SeekModelFrame(*model, frame 0x980, 0xA0CC4) + RebuildTimelineTrackPanel +
// SelectionReeval.
// ------------------------------------------------------------------
static void EditAccessoryAttachments(MMDApp* app, HWND hwnd) {
    app->state.enterKeyState = 1;
    app->state.accessoryEditDialogOpen = 1;
    const std::intptr_t result =
        DialogBoxParamA(static_cast<HINSTANCE>(app->HInstance()),
                        app->state.englishUI != 0 ? MAKEINTRESOURCEA(MDS_DLG_OUTSIDE_PARENT_EN)
                                                  : MAKEINTRESOURCEA(MDS_DLG_OUTSIDE_PARENT_JP),
                        hwnd, &SelectNavDlgProc, 0);
    if (result == IDCANCEL) {
        return; // was: break (switch exit)
    }
    app->SceneModified() = 1;
    app->state.accessoryEditDialogOpen = 0;
    if (app->DialogOrders().modelIndices != nullptr) {
        app->DialogOrders().modelIndices.reset();
    }
    if (app->AccessoryEditArray() != nullptr) {
        ::operator delete(app->AccessoryEditArray());
        app->AccessoryEditArray() = nullptr;
    }
    if (app->SelectNavRecords() != nullptr) {
        delete[] app->SelectNavRecords();
        app->SelectNavRecords() = nullptr;
    }
    if (app->AccessoryApplyGate() == 0) {
        return; // was: break (switch exit)
    }
    mdl::ModelRecord* model = ActiveModel(app);
    // original: sub_4B4260(ecx = model, frame = app+0x980,
    // app+0xA0CC4)
    SeekModelFrame(*model, app->state.currentFrame, app->PlaybackPhysicsMode());
    RebuildTimelineTrackPanel(app);
    SelectionReeval(app);
}

void HandleFrameEditControl(MMDApp* app, HWND hwnd, std::uint16_t id, std::uint16_t notify) {
    switch (id) {
    case panel::kUndoButton: // undo (0x00482766) -> UndoKeyframeEdit
        UndoKeyframeEdit(app, hwnd);
        break;

    case panel::kRedoButton: // redo (0x004828DF) -> RedoKeyframeEdit
        RedoKeyframeEdit(app, hwnd);
        break;

    // ------------------------------------------------------------------
    // 402 (0x0047EEDF): camera/view reset - 0x308..0x318 cleared, angle
    // -45, 0x30C = 0 (byte 0x340 == 2) or 10, light matrix identity; the
    // 0x2F8==0 path is gated: skipped when dword[0xA0430] >= 0 AND slot
    // == dword[0xA0430] AND byte 0x330 == 0 AND byte 0x9ED98 != 0; flag
    // 0xA0478 set when (0xA0430 >= 0 && 0x9ED98 != 0).  All paths end at
    // loc_47EFF6 (PostViewRefresh + SelectTimelineTrack(GlobalTimelineTrack::Camera)).
    // 403 (0x0047F163): same with 0x314 = PI (flt_52B738).
    // 404 (0x0047F211): 0x310 = -PI/2 (flt_52E8FC), 0x30C = 0 without the
    // byte-0x340 gate.
    // 405 (0x0047F00B): 0x314 = +PI/2 (flt_52E900).
    // 406 (0x0047F0B5): 0x314 = -PI/2 (flt_52E8FC).
    // ------------------------------------------------------------------
    case panel::kViewFrontButton:
        ResetViewPreset(app, ViewPreset::Front);
        break;
    case panel::kViewBackButton:
        ResetViewPreset(app, ViewPreset::Back);
        break;
    case panel::kViewTopButton:
        ResetViewPreset(app, ViewPreset::Top);
        break;
    case panel::kViewLeftButton:
        ResetViewPreset(app, ViewPreset::Left);
        break;
    case panel::kViewRightButton:
        ResetViewPreset(app, ViewPreset::Right);
        break;

    case panel::kViewBottomButton: // view reset + reload (0x0047F29C) -> ResetViewAndReloadModels
        ResetViewAndReloadModels(app);
        break;

    case panel::kPlayButton: // play-range edits (0x00487446) -> ApplyPlaybackRangeEdits
        ApplyPlaybackRangeEdits(app, hwnd);
        break;

    // ------------------------------------------------------------------
    // 411/413/414 (0x0047FAE5 / 0x0047FAFA / 0x0047FB0F): toggle bytes
    // 0x341 / 0x342 / 0x9ED99 (setz cl / al / cl; mov [..], cl).
    // ------------------------------------------------------------------
    case panel::kPlaybackLoopCheckbox:
        app->PlaybackLoopEnabled() = app->PlaybackLoopEnabled() == 0 ? 1 : 0;
        break;
    case panel::kPlaybackReturnCheckbox:
        app->state.playbackReturnsToStartFrame =
            app->state.playbackReturnsToStartFrame == 0 ? 1 : 0;
        break;
    case panel::kPlaybackFromCurrentCheckbox:
        app->PlaybackStartsAtCurrentFrame() = app->PlaybackStartsAtCurrentFrame() == 0 ? 1 : 0;
        break;

    // ------------------------------------------------------------------
    // 412 (0x0047F9FE): checkbox 0x19C - view-mode byte 0x340 := 1 when
    // checked (BM_GETCHECK) / 0 when not; 0x213 is unchecked (BM_SETCHECK
    // 0) in the checked branch; ApplyCameraReferenceModeChange(app, old value) runs only when
    // byte 0x2F8 == 0.
    // ------------------------------------------------------------------
    case panel::kCameraRefModelCheckbox: {
        const LRESULT checked =
            SendMessageA(GetDlgItem(hwnd, panel::kCameraRefModelCheckbox), BM_GETCHECK, 0, 0);
        const int prev = static_cast<int>(app->CameraReferenceMode());
        if (checked == 1) {
            app->CameraReferenceMode() = CameraAttachmentReference::ModelRoot;
            SendMessageA(GetDlgItem(hwnd, panel::kCameraRefBoneCheckbox), BM_SETCHECK, 0, 0);
            if (app->CameraMode() != 0) {
                break;
            }
            ApplyCameraReferenceModeChange(app, prev);
        } else {
            app->CameraReferenceMode() = CameraAttachmentReference::None;
            if (app->CameraMode() != 0) {
                break;
            }
            ApplyCameraReferenceModeChange(app, prev);
        }
        break;
    }

    // ------------------------------------------------------------------
    // 418/419 (0x00480734 / 0x0048072A): single refresh helpers.
    // ------------------------------------------------------------------
    case panel::kPreviousFrameButton:
        StepFrame(app, false);
        break;
    case panel::kNextFrameButton:
        StepFrame(app, true);
        break;

    // ------------------------------------------------------------------
    // 429 (0x004862E0): frame-scroll gate.  count = (sidebar - 0x54) / 26
    // (magic 0x4EC4EC4F, sar 3); dword 0x97C = frame(0x980) - count when
    // positive else 0; PanelPaint; when byte 0xA06CC != 0:
    // 0x4C2A00(0xCC sub, x = 0x97C, ho = sidebar) + InvalidateRect of
    // {6, 0x5F, sidebar-3, 0x92}.
    // ------------------------------------------------------------------
    case panel::kShowCurrentFrameButton: {
        const std::int32_t sidebar = app->SidebarWidth();
        const std::int32_t count = (sidebar - 0x54) / 26;
        const std::int32_t frame = app->state.currentFrame;
        if (frame > count) {
            app->state.timelineStartFrame = frame - count;
        } else {
            app->state.timelineStartFrame = 0;
        }
        PanelPaint(app);
        if (app->state.waveEnabled != 0) {
            TimelineDrawTicks(app->state.timelineStartFrame, sidebar);
            RECT rc;
            rc.left = 6;
            rc.top = 0x5F;
            rc.right = sidebar - 3;
            rc.bottom = 0x92;
            InvalidateRect(hwnd, &rc, FALSE);
        }
        break;
    }

    case panel::kCurveCopyButton: // curve <- frame (0x00481109) -> CopyInterpolationCurve
        CopyInterpolationCurve(app, hwnd);
        break;

    case panel::kCurvePasteButton: // curve -> frames (0x00481430) -> PasteInterpolationCurve
        PasteInterpolationCurve(app, hwnd);
        break;

    case panel::kCurveLinearButton: // curve reset (0x004816E6) -> ResetInterpolationCurves
        ResetInterpolationCurves(app, hwnd);
        break;

    case panel::kLoadModelButton: // load model (0x0047EB7B) -> OpenModelFile
        OpenModelFile(app, hwnd);
        break;

    // ------------------------------------------------------------------
    // 436 is handled by the original dispatcher's default chain rather
    // than its command-ID jump table: CBN_SELCHANGE from the model combo
    // enters ApplyModelComboSelection when the modal/model-load gate at 0xA0B50 is clear.
    // ------------------------------------------------------------------
    case panel::kMainComboModel:
        if (notify == CBN_SELCHANGE && app->EnglishNameDialog() == nullptr) {
            ApplyModelComboSelection(app);
            // x86 0x48F2DA / x64 RVA 0x53DE0: return arrow keys to
            // the editor after changing the selected model/camera.
            SetFocus(app->MainWindow());
        }
        break;

    case panel::kDeleteModelButton: // delete model (0x0047FCF5) -> DeleteSelectedModel
        DeleteSelectedModel(app, hwnd);
        break;

    // ------------------------------------------------------------------
    // 438 (0x00480651): clear keyframe selection marks: 0x26E0 (0x3C
    // stride, +0x38), 0x26E4 (0x14 stride, +0x10), 0x26E8 (0x1C stride,
    // +0x14) of the active model; RegisterDisplayKeyCurrent(model, dword 0x980);
    // 0x9E16C = max(0x9E16C, model+0x31B0); PanelPaint.  Dirty 0xA0B0D.
    // ------------------------------------------------------------------
    case panel::kRegisterModelDisplayButton: {
        app->SceneModified() = 1;
        mdl::ModelRecord* model = ActiveModel(app);
        ClearModelKeyMarks(model->boneKeys, mdl::kBoneKeyCapacity);
        ClearModelKeyMarks(model->morphKeys, mdl::kMorphKeyCapacity);
        ClearModelKeyMarks(model->displayKeys, mdl::kDisplayKeyCapacity);
        RegisterDisplayKeyCurrent(model, app->state.currentFrame);
        const std::int32_t v = static_cast<std::int32_t>(model->maxFrame);
        if (app->state.lastRegisteredFrame < v) {
            app->state.lastRegisteredFrame = v;
        }
        PanelPaint(app);
        break;
    }

    // ------------------------------------------------------------------
    // 439 (0x0047F3AF): checkbox 0x1B7 -> model byte 0x2D8D (1/0).
    // ------------------------------------------------------------------
    case panel::kModelVisibleCheckbox: {
        ActiveModel(app)->loadComplete =
            IsDlgButtonChecked(hwnd, panel::kModelVisibleCheckbox) == 1 ? 1 : 0;
        break;
    }

    // ------------------------------------------------------------------
    // 440/441 (0x0048A166 / 0x0048A133): model-flag toggles 0x37C0 /
    // 0x31BE when the active model exists; the original then jumps into
    // def_47E903, a no-op for these ids.
    // ------------------------------------------------------------------
    case panel::kShadowCheckbox: {
        mdl::ModelRecord* model = ActiveModel(app);
        if (model != nullptr) {
            mdl::ModelRecord& record = *model;
            record.toonFlag = record.toonFlag == 0 ? 1 : 0;
        }
        break;
    }
    case panel::kAddBlendCheckbox: {
        mdl::ModelRecord* model = ActiveModel(app);
        if (model != nullptr) {
            mdl::ModelRecord& record = *model;
            record.postLoadFlag2 = record.postLoadFlag2 == 0 ? 1 : 0;
        }
        break;
    }

    // ------------------------------------------------------------------
    // 444/445 (0x0047F319 / 0x0047F364): combo 0x1BB selection writes the
    // flag byte +0x12 of the 24-byte-stride table model+0x26C0 (1 / 0).
    // ------------------------------------------------------------------
    case panel::kIkOnRadio: {
        const LRESULT sel =
            SendMessageA(GetDlgItem(hwnd, panel::kIkChainCombo), CB_GETCURSEL, 0, 0);
        mdl::ModelRecord* model = ActiveModel(app);
        mdl::IkChain* chains = model->ikChains;
        if (chains != nullptr) {
            chains[sel].enabled = 1;
        }
        break;
    }
    case panel::kIkOffRadio: {
        const LRESULT sel =
            SendMessageA(GetDlgItem(hwnd, panel::kIkChainCombo), CB_GETCURSEL, 0, 0);
        mdl::ModelRecord* model = ActiveModel(app);
        mdl::IkChain* chains = model->ikChains;
        if (chains != nullptr) {
            chains[sel].enabled = 0;
        }
        break;
    }

    // ------------------------------------------------------------------
    // 446 (0x00486392): perspective FOV toggle.  When byte 0x31C != 0:
    // clear it and rebuild the projection - D3DXMatrixPerspectiveFovLH(
    // &m, fov = 0x9E1E8 * pi/180, aspect = locale->aspectRatio (0x1D4EC),
    // zn = 1, zf = 100000.0), scene (locale->device, 0x1D4E0) vtable slot 0xB0
    // (obj, 3, &m), SelectTimelineTrack(GlobalTimelineTrack::Camera).  Else: byte 0x31C = 1 +
    // SelectTimelineTrack(GlobalTimelineTrack::Camera).
    // ------------------------------------------------------------------
    case panel::kPerspectiveCheckbox: {
        if (app->CameraPerspective() != 0) {
            app->CameraPerspective() = 0;
            d3dx::D3DXMATRIXF mat{};
            auto* d3dx = &d3dx::Get();
            D3DRenderer* locale = app->Renderer();
            const float fov = static_cast<float>(static_cast<double>(app->CameraFov()) *
                                                 0.0174532925199433); // dbl_52BB20 (pi/180)
            const float aspect = locale->aspectRatio;                 // 0x1D4EC
            { d3dx->perspectiveFovLH(&mat, fov, aspect, 1.0f, 100000.0f); }
            IDirect3DDevice9* device = locale->device;
            device->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(&mat));
            SelectTimelineTrack(GlobalTimelineTrack::Camera);
        } else {
            app->CameraPerspective() = 1;
            SelectTimelineTrack(GlobalTimelineTrack::Camera);
        }
        break;
    }

    // ------------------------------------------------------------------
    // 423 (0x004810F8): DeleteMarkedKeyframes(app) then dirty 0xA0B0D.
    // ------------------------------------------------------------------
    case panel::kDeleteFramesButton:
        DeleteMarkedKeyframes(app);
        app->SceneModified() = 1;
        break;

    case panel::
        kEditExternalParentButton: // accessory edit dialog (0x0048D759) -> EditAccessoryAttachments
        EditAccessoryAttachments(app, hwnd);
        break;

    case panel::kSelectRangeButton: // frame-range selection (0x004829E4) -> SelectKeyframesInRange
        SelectKeyframesInRange(app, hwnd);
        break;

    case panel::kSelectColumnButton: // selection-mark sync (0x0048073E) -> SyncKeyframeSelection
        SyncKeyframeSelection(app);
        break;

    case panel::kCopyFramesButton: // frame copy (0x004834D2) -> CopySelectedKeyframes
        CopySelectedKeyframes(app, hwnd);
        break;

    case panel::kPasteButton: // frame paste (0x00484973) -> PasteSelectedKeyframes
        PasteSelectedKeyframes(app, hwnd);
        break;

    case panel::kReversePasteButton: // reverse paste (0x00485CF2) -> PasteMirroredBoneKeyframes
        PasteMirroredBoneKeyframes(app, hwnd);
        break;

    // ------------------------------------------------------------------
    // 424 (0x00488300): frame-edit dialog.  0x2F8 == 0 only; dirty
    // 0xBC, then DialogBoxParamA with the locale-gated template (0x25E
    // when byte 0xA0B4C == 0 i.e. JP UI, 0x28C otherwise) and the
    // sub_44C5D0 dialog proc.
    // ------------------------------------------------------------------
    case panel::kExpandShrinkButton: {
        if (app->CameraMode() != 0) {
            break;
        }
        app->state.enterKeyState = 1;
        if (app->state.englishUI != 0) {
            DialogBoxParamA(static_cast<HINSTANCE>(app->HInstance()),
                            MAKEINTRESOURCEA(MDS_DLG_FRAME_SCALE_EN), hwnd, &FrameRangeDlgProc, 0);
        } else {
            DialogBoxParamA(static_cast<HINSTANCE>(app->HInstance()),
                            MAKEINTRESOURCEA(MDS_DLG_FRAME_SCALE_JP), hwnd, &FrameRangeDlgProc, 0);
        }
        break;
    }

    default:
        // Remaining controls in this family have no action in the original
        // default chain.
        break;
    }
}

} // namespace mikudancestudio
