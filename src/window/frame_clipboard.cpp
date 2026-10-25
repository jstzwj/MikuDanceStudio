// Copy and paste selected keyframes, preserving track and undo ordering.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>

#include <cstdarg>
#include <cstdint>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

#include "frame_clipboard.hpp"
#include "frame_key_selection.hpp"

namespace mikudancestudio {

static const char kLightNameRoot[] = "\x83\x8B\x81\x5B\x83\x67";

using editor::ClearModelKeyMarks;
using mdl::BoneClipboardRecord;
using mdl::CameraClipboardRecord;
using mdl::DisplayClipboardRecord;
using mdl::GravityClipboardRecord;
using mdl::IkClipboardState;
using mdl::LightClipboardRecord;
using mdl::MorphClipboardRecord;
using mdl::SelectorClipboardState;
using mdl::ShadowClipboardRecord;

template <typename Record>
static void ReplaceClipboardBuffer(Record*& buffer, std::uint32_t count) {
    if (buffer != nullptr) {
        free(buffer);
        buffer = nullptr;
    }
    if (count == 0) {
        return;
    }
    buffer = static_cast<Record*>(malloc(count * sizeof(Record)));
    memset(buffer, 0, count * sizeof(Record));
}

// Porting-era trace under MIKUDANCESTUDIO_STATE_DUMP_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub keeps the call sites
// valid and inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
static void TraceModelPaste(const char* format, ...) {
    char directory[MAX_PATH]{};
    const DWORD length =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\model_paste_trace.log", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    va_list args;
    va_start(args, format);
    std::vfprintf(stream, format, args);
    va_end(args);
    std::fputc('\n', stream);
    std::fclose(stream);
}
#else
static inline void TraceModelPaste(const char*, ...) {
}
#endif

template <typename Key>
static std::uint32_t CountMarkedModelKeys(const Key* keys, std::size_t capacity,
                                          std::uint32_t& minFrame) {
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < capacity; ++i) {
        const Key& key = keys[i];
        if (key.allocated == 0) {
            continue;
        }
        ++count;
        if (key.frame < minFrame) {
            minFrame = key.frame;
        }
    }
    return count;
}

template <typename Key>
static std::uint32_t CountSelectedKeys(const Key* keys, std::size_t capacity,
                                       std::uint32_t& minFrame) {
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < capacity; ++i) {
        const Key& key = keys[i];
        if (key.selected == 0) {
            continue;
        }
        ++count;
        if (key.frame < minFrame) {
            minFrame = key.frame;
        }
    }
    return count;
}

template <typename Key> static void ClearSelectedKeys(Key* keys, std::size_t capacity) {
    for (std::size_t i = 0; i < capacity; ++i) {
        keys[i].selected = 0;
    }
}

// ------------------------------------------------------------------
// 420 (0x004834D2): frame copy.  Counts the marked entries and the
// minimum frame, then (re)allocates the copy buffers and copies every
// marked record.  0x2F8 == 0: counts 0x9DA28/0x9DA2C/0x9DA30, buffers
// 0x354 (bone, 0x54-stride) / 0x358 (morph, 0x28) / 0x35C (camera,
// 0x18; per-record bone-name/light arrays at +0x0C/+0x14).  0x2F8 !=
// 0: counts 0x9DA34..0x9DA44, buffers 0x360..0x370.  Controls 0x1A5
// (paste), 0x1A6 (delete) and menu 0xFA are gated on the totals; the
// record index for the name lookups walks the +4 rank pointers while
// idx >= count.  Tail: nothing (jmp loc_48F2E0).
// ------------------------------------------------------------------
void CopySelectedKeyframes(MMDApp* app, HWND hwnd) {
    std::uint32_t minFrame = 0xFFFFFFFFu; // ebx
    if (app->CameraMode() == 0) {
        // ---- model mode (loc_483E7C..) ----
        // free the previous camera records' sub-buffers (loc_483E7C)
        if (app->DisplayClipboard() != nullptr && app->ClipboardCounts().displays != 0) {
            auto* records = app->DisplayClipboard();
            const std::uint32_t camCount = app->ClipboardCounts().displays;
            for (std::uint32_t i = 0; i < camCount; ++i) {
                if (records[i].ikStates != nullptr) {
                    free(records[i].ikStates);
                    records[i].ikStates = nullptr;
                }
                if (records[i].selectorStates != nullptr) {
                    free(records[i].selectorStates);
                    records[i].selectorStates = nullptr;
                }
            }
        }
        auto& clipboardCounts = app->ClipboardCounts();
        clipboardCounts.bones = 0;
        clipboardCounts.morphs = 0;
        clipboardCounts.displays = 0;
        // selected-entry counts + min frame
        {
            unsigned char* model = app->SelectedModel();
            clipboardCounts.bones =
                CountMarkedModelKeys(mdl::BoneKeys(model), mdl::kBoneKeyCapacity, minFrame);
            clipboardCounts.morphs =
                CountMarkedModelKeys(mdl::MorphKeys(model), mdl::kMorphKeyCapacity, minFrame);
            clipboardCounts.displays =
                CountMarkedModelKeys(mdl::DisplayKeys(model), mdl::kDisplayKeyCapacity, minFrame);
        }
        const std::uint32_t boneSel = clipboardCounts.bones;
        const std::uint32_t morphSel = clipboardCounts.morphs;
        const std::uint32_t camSel = clipboardCounts.displays;
        if (boneSel == 0 && morphSel == 0 && camSel == 0) {
            // loc_48402C..48406D: nothing selected
            EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), FALSE);
            EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), FALSE);
            EnableMenuItem(GetMenu(hwnd), MDS_CMD_PASTE_AT_DIFFERENT_FRAME, TRUE);
            return; // was: break (switch exit)
        }
        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), TRUE);
        EnableMenuItem(GetMenu(hwnd), MDS_CMD_PASTE_AT_DIFFERENT_FRAME, FALSE);
        EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), boneSel != 0 ? TRUE : FALSE);
        // bone buffer 0x354 (0x54-stride records)
        if (app->BoneClipboard() != nullptr) {
            free(app->BoneClipboard());
            app->BoneClipboard() = nullptr;
        }
        if (boneSel != 0) {
            void* p = malloc(boneSel * sizeof(BoneClipboardRecord));
            if (p != nullptr) {
                ConstructArrayElements(p, sizeof(BoneClipboardRecord), boneSel,
                                       reinterpret_cast<void*>(&IdentityCtor));
            }
            app->BoneClipboard() = static_cast<BoneClipboardRecord*>(p);
            memset(p, 0, boneSel * sizeof(BoneClipboardRecord));
        }
        // morph buffer 0x358 (0x28-stride records)
        if (app->MorphClipboard() != nullptr) {
            free(app->MorphClipboard());
            app->MorphClipboard() = nullptr;
        }
        if (morphSel != 0) {
            void* p = malloc(morphSel * sizeof(MorphClipboardRecord));
            app->MorphClipboard() = static_cast<MorphClipboardRecord*>(p);
            memset(p, 0, morphSel * sizeof(MorphClipboardRecord));
        }
        // camera buffer 0x35C (0x18-stride records) + per-record
        // bone-name (0x2D88 * 0x15) and light (0x4CCE8 * 0x1C) arrays
        if (app->DisplayClipboard() != nullptr) {
            free(app->DisplayClipboard());
            app->DisplayClipboard() = nullptr;
        }
        if (camSel != 0) {
            unsigned char* model = app->SelectedModel();
            void* p = malloc(camSel * sizeof(DisplayClipboardRecord));
            app->DisplayClipboard() = static_cast<DisplayClipboardRecord*>(p);
            memset(p, 0, camSel * sizeof(DisplayClipboardRecord));
            if (mdl::Mdl(model)->ikChainCount > 0 && camSel != 0) {
                auto* records = app->DisplayClipboard();
                const std::uint32_t cnt = mdl::Mdl(model)->ikChainCount;
                for (std::uint32_t i = 0; i < camSel; ++i) {
                    records[i].ikStates =
                        static_cast<IkClipboardState*>(malloc(cnt * sizeof(IkClipboardState)));
                }
            }
            if (mdl::Mdl(model)->boneOrderCount > 0 && camSel != 0) {
                auto* records = app->DisplayClipboard();
                const std::uint32_t cnt = mdl::Mdl(model)->boneOrderCount;
                for (std::uint32_t i = 0; i < camSel; ++i) {
                    records[i].selectorStates = static_cast<SelectorClipboardState*>(
                        malloc(cnt * sizeof(SelectorClipboardState)));
                }
            }
        }
        // bone record copy (loc_484321..484551)
        {
            unsigned char* model = app->SelectedModel();
            mdl::BoneKey* boneKeys = mdl::BoneKeys(model);
            const mdl::BoneRecord* bones = mdl::Mdl(model)->boneTable;
            const std::uint32_t boneCount = mdl::Mdl(model)->boneCount;
            auto* dst = app->BoneClipboard();
            std::uint32_t cnt = 0;
            for (std::uint32_t recordIndex = 0;
                 recordIndex < static_cast<std::uint32_t>(mdl::kBoneKeyCapacity); ++recordIndex) {
                const mdl::BoneKey& key = boneKeys[recordIndex];
                if (key.allocated == 0) {
                    continue;
                }
                // rank walk: while idx >= boneCount follow the +4
                // pointer (loc_484350)
                std::uint32_t idx = recordIndex;
                while (idx >= boneCount) {
                    idx = boneKeys[idx].previous;
                }
                BoneClipboardRecord& rec = dst[cnt];
                strcpy_s(rec.name, sizeof(rec.name), bones[idx].name);
                rec.frame = key.frame - minFrame;
                std::memcpy(rec.rotation, key.rotation, sizeof(rec.rotation));
                std::memcpy(rec.position, key.position, sizeof(rec.position));
                rec.physicsDisabled = key.physicsDisabled;
                for (std::int32_t i = 0; i < 4; ++i) {
                    rec.interpolation[i] = key.interpolation[i];
                    rec.interpolation[4 + i] = key.interpolation[4 + i];
                    rec.interpolation[8 + i] = key.interpolation[8 + i];
                    rec.interpolation[12 + i] = key.interpolation[12 + i];
                }
                ++cnt;
            }
        }
        // morph record copy (loc_484590..484667, gated 0x2D80 > 0)
        if (mdl::Mdl(app->SelectedModel())->morphCount > 0) {
            unsigned char* model = app->SelectedModel();
            mdl::MorphKey* morphKeys = mdl::MorphKeys(model);
            const mdl::MorphRecord* morphs = mdl::Mdl(model)->morphs;
            const std::uint32_t morphCount = mdl::Mdl(model)->morphCount;
            auto* dst = app->MorphClipboard();
            std::uint32_t cnt = 0;
            for (std::uint32_t recordIndex = 0;
                 recordIndex < static_cast<int>(mdl::kMorphKeyCapacity); ++recordIndex) {
                const mdl::MorphKey& key = morphKeys[recordIndex];
                if (key.allocated == 0) {
                    continue;
                }
                std::uint32_t idx = recordIndex;
                while (idx >= morphCount) {
                    idx = morphKeys[idx].previous;
                }
                MorphClipboardRecord& rec = dst[cnt];
                strcpy_s(rec.name, sizeof(rec.name), morphs[idx].name);
                rec.frame = key.frame - minFrame;
                rec.value = key.value;
                ++cnt;
            }
        }
        // display record copy (loc_484675..)
        {
            unsigned char* model = app->SelectedModel();
            mdl::DisplayKey* displayKeys = mdl::DisplayKeys(model);
            const mdl::BoneRecord* bones = mdl::Mdl(model)->boneTable;
            auto* records = app->DisplayClipboard();
            const std::int32_t boneIdxCount = mdl::Mdl(model)->ikChainCount;
            const std::int32_t lightCount = mdl::Mdl(model)->boneOrderCount;
            std::uint32_t cnt = 0;
            for (std::uint32_t keyIndex = 0; keyIndex < mdl::kDisplayKeyCapacity; ++keyIndex) {
                const mdl::DisplayKey& key = displayKeys[keyIndex];
                if (key.allocated == 0) {
                    continue;
                }
                DisplayClipboardRecord& rec = records[cnt];
                rec.frame = key.frame - minFrame;
                rec.visible = key.visible;
                rec.ikCount = boneIdxCount;
                // per-bone records at +0x0C (0x15 stride: 0x14-byte
                // name + value byte)
                if (boneIdxCount > 0) {
                    const mdl::IkChain* chains = mdl::Mdl(model)->ikChains;
                    const auto* pose = mdl::IkStates(key);
                    for (std::int32_t i = 0; i < boneIdxCount; ++i) {
                        const std::uint32_t boneIdx = chains[i].boneIndex;
                        strcpy_s(rec.ikStates[i].boneName, sizeof(rec.ikStates[i].boneName),
                                 bones[boneIdx].name);
                        rec.ikStates[i].enabled = pose[i];
                    }
                }
                // light records at +0x14 (0x1C stride)
                rec.selectorCount = lightCount;
                const auto* lightData = mdl::SelectorStates(key);
                strcpy_s(rec.selectorStates[0].boneName, sizeof(rec.selectorStates[0].boneName),
                         kLightNameRoot);
                rec.selectorStates[0].modelIndex = lightData[0].modelIndex;
                rec.selectorStates[0].boneIndex = lightData[0].boneIndex;
                if (lightCount > 1) {
                    const auto* order = mdl::BoneOrder(model);
                    for (std::int32_t i = 1; i < lightCount; ++i) {
                        const std::uint32_t boneIdx = order[i].boneIndex;
                        strcpy_s(rec.selectorStates[i].boneName,
                                 sizeof(rec.selectorStates[i].boneName), bones[boneIdx].name);
                        rec.selectorStates[i].modelIndex = lightData[i].modelIndex;
                        rec.selectorStates[i].boneIndex = lightData[i].boneIndex;
                    }
                }
                ++cnt;
            }
        }
    } else {
        // ---- camera mode (loc_4834E2..) ----
        auto& clipboardCounts = app->ClipboardCounts();
        clipboardCounts.cameras = 0;
        clipboardCounts.lights = 0;
        clipboardCounts.shadows = 0;
        clipboardCounts.gravity = 0;
        clipboardCounts.accessories = 0;
        clipboardCounts.cameras =
            CountSelectedKeys(app->CameraKeys(), mdl::kTimelineKeyCapacity, minFrame);
        clipboardCounts.lights =
            CountSelectedKeys(app->LightKeys(), mdl::kTimelineKeyCapacity, minFrame);
        clipboardCounts.shadows =
            CountSelectedKeys(app->ShadowKeys(), mdl::kTimelineKeyCapacity, minFrame);
        clipboardCounts.gravity =
            CountSelectedKeys(app->GravityKeys(), mdl::kTimelineKeyCapacity, minFrame);
        {
            std::uint32_t accessoryCount = 0;
            for (std::size_t keyIndex = 0; keyIndex < mdl::kTimelineKeyCapacity; ++keyIndex) {
                for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
                    auto* keys = app->AccessoryKeys(slot);
                    const mdl::AccessoryKey& key = keys[keyIndex];
                    if (key.selected != 0) {
                        ++accessoryCount;
                        if (key.frame < minFrame) {
                            minFrame = key.frame;
                        }
                    }
                }
            }
            clipboardCounts.accessories = accessoryCount;
        }
        const std::uint32_t cameraCount = clipboardCounts.cameras;
        const std::uint32_t lightCount = clipboardCounts.lights;
        const std::uint32_t shadowCount = clipboardCounts.shadows;
        const std::uint32_t gravityCount = clipboardCounts.gravity;
        const std::uint32_t accessoryCount = clipboardCounts.accessories;
        if (cameraCount == 0 && lightCount == 0 && shadowCount == 0 && gravityCount == 0 &&
            accessoryCount == 0) {
            // loc_483657..48369C
            EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), FALSE);
            EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), FALSE);
            EnableMenuItem(GetMenu(hwnd), MDS_CMD_PASTE_AT_DIFFERENT_FRAME, TRUE);
            return; // was: break (switch exit)
        }
        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), TRUE);
        EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), FALSE);
        EnableMenuItem(GetMenu(hwnd), MDS_CMD_PASTE_AT_DIFFERENT_FRAME, FALSE);
        // buffers 0x360..0x370 (0x48/0x1C/0x0C/0x1C/0x34 strides)
        ReplaceClipboardBuffer(app->CameraClipboard(), cameraCount);
        ReplaceClipboardBuffer(app->LightClipboard(), lightCount);
        ReplaceClipboardBuffer(app->ShadowClipboard(), shadowCount);
        ReplaceClipboardBuffer(app->GravityClipboard(), gravityCount);
        ReplaceClipboardBuffer(app->AccessoryClipboard(), accessoryCount);
        // copy loop 1: 0x374 marked -> 0x360 records (0x48 stride)
        {
            auto* keys = app->CameraKeys();
            auto* dst = app->CameraClipboard();
            std::uint32_t cnt = 0;
            for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
                const mdl::CameraKey& key = keys[i];
                if (key.selected == 0) {
                    continue;
                }
                CameraClipboardRecord& rec = dst[cnt];
                rec.frame = key.frame - minFrame;
                std::memcpy(rec.eye, key.eye, sizeof(rec.eye));
                std::memcpy(rec.target, key.target, sizeof(rec.target));
                rec.fov = key.fov;
                rec.perspective = key.perspective;
                std::memcpy(rec.interpolation, key.interpolation, sizeof(rec.interpolation));
                rec.distance = key.distance;
                rec.parentModel = key.parentModel;
                rec.parentBone = key.parentBone;
                ++cnt;
            }
        }
        // copy loop 2: 0x378 marked -> 0x364 records (0x1C stride)
        {
            auto* keys = app->LightKeys();
            auto* dst = app->LightClipboard();
            std::uint32_t cnt = 0;
            for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
                const mdl::LightKey& key = keys[i];
                if (key.selected == 0) {
                    continue;
                }
                LightClipboardRecord& rec = dst[cnt];
                rec.frame = key.frame - minFrame;
                std::memcpy(rec.direction, key.direction, sizeof(rec.direction));
                std::memcpy(rec.color, key.color, sizeof(rec.color));
                ++cnt;
            }
        }
        // copy loop 3: 0x37C marked -> 0x368 records (0x0C stride)
        {
            auto* keys = app->ShadowKeys();
            auto* dst = app->ShadowClipboard();
            std::uint32_t cnt = 0;
            for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
                const mdl::SelfShadowKey& key = keys[i];
                if (key.selected == 0) {
                    continue;
                }
                ShadowClipboardRecord& rec = dst[cnt];
                rec.frame = key.frame - minFrame;
                rec.mode = key.mode;
                rec.distance = key.distance;
                ++cnt;
            }
        }
        // copy loop 4: 0x380 marked -> 0x36C records (0x1C stride)
        {
            auto* keys = app->GravityKeys();
            auto* dst = app->GravityClipboard();
            std::uint32_t cnt = 0;
            for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
                const mdl::GravityKey& key = keys[i];
                if (key.selected == 0) {
                    continue;
                }
                GravityClipboardRecord& rec = dst[cnt];
                rec.frame = key.frame - minFrame;
                rec.acceleration = key.acceleration;
                std::memcpy(rec.direction, key.direction, sizeof(rec.direction));
                rec.noise = key.noise;
                rec.noiseEnabled = key.noiseEnabled;
                ++cnt;
            }
        }
        // copy loop 5: accessory blobs -> 0x370 records (0x34 stride,
        // slot index at +4)
        {
            auto* dst = app->AccessoryClipboard();
            std::uint32_t cnt = 0;
            for (std::size_t keyIndex = 0; keyIndex < mdl::kTimelineKeyCapacity; ++keyIndex) {
                for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
                    auto* keys = app->AccessoryKeys(slot);
                    const mdl::AccessoryKey& key = keys[keyIndex];
                    if (key.selected == 0) {
                        continue;
                    }
                    mdl::AccessoryClipboardKey& rec = dst[cnt];
                    rec.frameOffset = key.frame - minFrame;
                    rec.slot = static_cast<std::uint8_t>(slot);
                    rec.visible = key.visible;
                    rec.shadowEnabled = key.shadowEnabled;
                    rec.parentModel = key.parentModel;
                    rec.parentBone = key.parentBone;
                    std::memcpy(rec.position, key.position, sizeof(rec.position));
                    std::memcpy(rec.rotation, key.rotation, sizeof(rec.rotation));
                    rec.scale = key.scale;
                    rec.opacity = key.opacity;
                    ++cnt;
                }
            }
        }
    }
}
// ------------------------------------------------------------------
// 421 (0x00484973): frame paste.  0x2F8 == 0: when any model-mode
// count is non-zero, clear all marks, open an undo-table entry
// (0x26EC, 0x1C stride: type 2 at +0, frame at +0x0C, bone-name
// snapshot at +0x10, light snapshot at +0x14; second 0x1C-stride
// history at 0x23F0 zeroed), snapshot the bone records, then paste
// the copy buffers: RegisterBoneKey per 0x354 bone record, RegisterMorphKeyFromRecord per
// 0x358 morph record (0x28), RegisterDisplayKeyFromRecord per 0x35C camera record (0x18)
// with the light-record slot fixup (stale model slots severed).
// 0x2F8 != 0: same over the 0x360..0x370 camera-mode buffers with
// RegisterCameraKey/RegisterLightKey/RegisterSelfShadowKey/RegisterGravityKey/0x414110.  Common tail:
// byte 0x9EDB5 = 1, PanelPaint, SelectionReeval, SeekModelFrame(model,
// frame, 0xA0CC4), and for camera mode the reload chain (ReloadModels,
// RefreshLightPanel, RefreshSelfShadowPanel, ApplyGravityTrack, ApplyAccessoryTrack per slot, SyncAccessoryEditPanel).
// ------------------------------------------------------------------
void PasteSelectedKeyframes(MMDApp* app, HWND hwnd) {
    if (app->CameraMode() == 0) {
        const auto& clipboardCounts = app->ClipboardCounts();
        const std::uint32_t boneSel = clipboardCounts.bones;
        const std::uint32_t morphSel = clipboardCounts.morphs;
        const std::uint32_t camSel = clipboardCounts.displays;
        TraceModelPaste("enter bone=%u morph=%u display=%u", boneSel, morphSel, camSel);
        if (boneSel == 0 && morphSel == 0 && camSel == 0) {
            return; // was: break (switch exit)
        }
        app->SceneModified() = 1;
        const std::int32_t frame = app->state.currentFrame;
        unsigned char* model = app->SelectedModel();
        TraceModelPaste("active model=%p frame=%d", model, frame);
        // clear all model-mode marks (loc_4849C0..)
        ClearModelKeyMarks(mdl::BoneKeys(model), mdl::kBoneKeyCapacity);
        ClearModelKeyMarks(mdl::MorphKeys(model), mdl::kMorphKeyCapacity);
        ClearModelKeyMarks(mdl::DisplayKeys(model), mdl::kDisplayKeyCapacity);
        if (boneSel != 0) {
            // undo/redo buttons + undo-table entry (loc_484C84..)
            EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
            EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
            mdl::Mdl(model)->undoDirty = 1;
            mdl::Mdl(model)->redoDirty = 0;
            mdl::ModelRecord& record = *mdl::Mdl(model);
            const std::int32_t next = record.UndoCursor() + 1;
            record.UndoCursor() = next >= mdl::kUndoHistoryDepth ? 0 : next;
            const std::int32_t cur = record.UndoCursor();
            record.LatestUndoCursor() = cur;
            auto& undo = mikudancestudio::mdl::Mdl(model)->undoRings[0].slots[cur];
            undo.operation = mdl::UndoOperation::BoneKeys;
            undo.frame = frame;
            // bone-name snapshot buffer (+0x10 of the entry, 0x24-
            // stride records)
            void* p = undo.bonePose;
            if (p != nullptr) {
                ::operator delete(p);
                undo.bonePose = nullptr;
            }
            const std::int32_t boneCount = mdl::Mdl(model)->boneCount;
            auto* undoBone = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
                ::operator new(static_cast<std::uint32_t>(boneCount) *
                               sizeof(mikudancestudio::mdl::BonePoseSnapshot)));
            if (undoBone != nullptr) {
                ConstructArrayElements(undoBone, sizeof(mikudancestudio::mdl::BonePoseSnapshot),
                                       static_cast<std::uint32_t>(boneCount),
                                       reinterpret_cast<void*>(&IdentityCtor));
            }
            undo.bonePose = undoBone;
            memset(undoBone, 0,
                   static_cast<std::uint32_t>(boneCount) *
                       sizeof(mikudancestudio::mdl::BonePoseSnapshot));
            if (boneCount > 0) {
                const mdl::ModelRecord& record = *mdl::Mdl(model);
                const mdl::BoneRecord* bones = record.boneTable;
                unsigned char* boneState = record.bonePhysicsState;
                for (std::int32_t i = 0; i < boneCount; ++i) {
                    auto& rec = undoBone[i];
                    const auto& src = bones[i];
                    rec.boneIndex = i;
                    memcpy(rec.position, src.trans, sizeof rec.position);
                    memcpy(rec.rotation, src.rotQuat, sizeof rec.rotation);
                    rec.physicsDisabled = boneState[i];
                }
            }
            // second undo-history table (0x23F0 base, 0x1C stride):
            // entry cnt+0x164 cleared
            undo.recordCount = 0;
            // light snapshot buffer (+0x14 of the entry, 3*0x40 *
            // boneSel bytes)
            p = undo.auxiliaryPose;
            if (p != nullptr) {
                ::operator delete(p);
                undo.auxiliaryPose = nullptr;
            }
            unsigned char* undoLight =
                static_cast<unsigned char*>(::operator new(boneSel * 3 * 0x40));
            if (undoLight != nullptr) {
                ConstructArrayElements(undoLight, 0x40, boneSel * 3,
                                       reinterpret_cast<void*>(&IdentityCtor));
            }
            undo.auxiliaryPose = undoLight;
            memset(undoLight, 0, boneSel * 3 * 0x40);
        }
        std::memset(mdl::Mdl(model)->keyVisitMap, 0, sizeof(mdl::Mdl(model)->keyVisitMap));
        ResetBoneKeyCursor(model);
        TraceModelPaste("undo-ready free=%d", mdl::Mdl(model)->searchCursor);
        // bone paste loop (loc_485130..485174): 0x54-byte record +
        // frame + 0 flag; original is __userpurge(ecx = model,
        // ebx = i) - the stub drops the index
        if (boneSel != 0) {
            const auto* src = app->BoneClipboard();
            std::uint32_t i = 0;
            do {
                BoneClipboardRecord rec = src[i];
                if (RegisterBoneKey(model, rec, frame, 0) == 0) {
                    break;
                }
                ++i;
            } while (i < boneSel);
        }
        ResetMorphKeyCursor(model);
        TraceModelPaste("bone-done free=%d", mdl::Mdl(model)->searchCursor);
        // morph paste loop (loc_48519B..4851DD)
        if (morphSel != 0) {
            const auto* src = app->MorphClipboard();
            std::uint32_t i = 0;
            do {
                const MorphClipboardRecord rec = src[i];
                if (RegisterMorphKeyFromRecord(model, rec, frame) == 0) {
                    break;
                }
                ++i;
            } while (i < morphSel);
        }
        ResetDisplayKeyCursor(model);
        TraceModelPaste("morph-done display-free=%d", mdl::Mdl(model)->searchCursor);
        // camera paste loop (loc_485210..4852E4): light-record slot
        // fixup + RegisterDisplayKeyFromRecord with the 6 record dwords + frame
        if (camSel != 0) {
            auto* records = app->DisplayClipboard();
            std::uint32_t i = 0;
            do {
                DisplayClipboardRecord& rec = records[i];
                const std::int32_t lightCount = rec.selectorCount;
                TraceModelPaste("display[%u] rec=%p rel=%d relptr=%p ik=%d ikptr=%p", i, &rec,
                                lightCount, rec.selectorStates, rec.ikCount, rec.ikStates);
                if (lightCount > 0) {
                    for (std::int32_t j = 0; j < lightCount; ++j) {
                        SelectorClipboardState& state = rec.selectorStates[j];
                        const std::int32_t slotIdx = state.modelIndex;
                        TraceModelPaste("display[%u] relation[%d] slot=%d bone=%d", i, j, slotIdx,
                                        state.boneIndex);
                        if (slotIdx < 0) {
                            continue;
                        }
                        unsigned char* slotModel = app->ModelSlot(slotIdx);
                        if (slotModel == nullptr) {
                            state.modelIndex = -1;
                            state.boneIndex = 0;
                        } else if (static_cast<std::int32_t>(mdl::Mdl(slotModel)->boneCount) <
                                   state.boneIndex) {
                            state.boneIndex = 0;
                        }
                    }
                }
                TraceModelPaste("display[%u] fixup-done", i);
                if (RegisterDisplayKeyFromRecord(model, rec.frame, rec.visible, rec.ikCount,
                                                 rec.ikStates, rec.selectorCount,
                                                 rec.selectorStates, frame) == 0) {
                    break;
                }
                TraceModelPaste("display[%u] registrar-done", i);
                ++i;
            } while (i < camSel);
        }
        app->PhysicsResetPending() = 1;
        TraceModelPaste("model-paste loops done");
    } else {
        // ---- camera mode (loc_4849A0..) ----
        const auto& clipboardCounts = app->ClipboardCounts();
        const std::uint32_t cameraCount = clipboardCounts.cameras;
        const std::uint32_t lightCount = clipboardCounts.lights;
        const std::uint32_t shadowCount = clipboardCounts.shadows;
        const std::uint32_t gravityCount = clipboardCounts.gravity;
        const std::uint32_t accessoryCount = clipboardCounts.accessories;
        if (cameraCount == 0 && lightCount == 0 && shadowCount == 0 && gravityCount == 0 &&
            accessoryCount == 0) {
            return; // was: break (switch exit)
        }
        app->SceneModified() = 1;
        // clear all camera-mode marks (single sweep over the four
        // tables + the blobs)
        ClearSelectedKeys(app->CameraKeys(), mdl::kTimelineKeyCapacity);
        ClearSelectedKeys(app->LightKeys(), mdl::kTimelineKeyCapacity);
        ClearSelectedKeys(app->ShadowKeys(), mdl::kTimelineKeyCapacity);
        ClearSelectedKeys(app->GravityKeys(), mdl::kTimelineKeyCapacity);
        // (the original dereferences every blob pointer here - the
        // 0x384 table is expected to be fully populated)
        for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
            auto* keys = app->AccessoryKeys(slot);
            ClearSelectedKeys(keys, mdl::kTimelineKeyCapacity);
        }
        const std::int32_t frame = app->state.currentFrame;
        // paste loops (loc_484A54..484Bxx): record-by-value + bool
        if (cameraCount != 0) {
            const auto* src = app->CameraClipboard();
            std::uint32_t i = 0;
            do {
                const CameraClipboardRecord rec = src[i];
                if (RegisterCameraKey(app, &rec) == 0) {
                    break;
                }
                ++i;
            } while (i < cameraCount);
        }
        if (lightCount != 0) {
            const auto* src = app->LightClipboard();
            std::uint32_t i = 0;
            do {
                const LightClipboardRecord rec = src[i];
                if (RegisterLightKey(app, &rec) == 0) {
                    break;
                }
                ++i;
            } while (i < lightCount);
        }
        if (shadowCount != 0) {
            const auto* src = app->ShadowClipboard();
            std::uint32_t i = 0;
            do {
                const ShadowClipboardRecord rec = src[i];
                if (RegisterSelfShadowKey(app, &rec) == 0) {
                    break;
                }
                ++i;
            } while (i < shadowCount);
        }
        if (gravityCount != 0) {
            const auto* src = app->GravityClipboard();
            std::uint32_t i = 0;
            do {
                const GravityClipboardRecord rec = src[i];
                if (RegisterGravityKey(app, &rec) == 0) {
                    break;
                }
                ++i;
            } while (i < gravityCount);
        }
        if (accessoryCount != 0) {
            auto* src = app->AccessoryClipboard();
            std::uint32_t i = 0;
            do {
                mdl::AccessoryClipboardKey rec = src[i];
                if (PasteAccessoryKeyRecord(app, &rec, 0) == 0) {
                    break;
                }
                ++i;
            } while (i < accessoryCount);
        }
        app->PhysicsResetPending() = 1;
    }
    // common tail (loc_4852F1..485382)
    PanelPaint(app);
    SelectionReeval(app);
    unsigned char* model = app->SelectedModel();
    if (model != nullptr) {
        SeekModelFrame(*mdl::Mdl(model), app->state.currentFrame, app->PlaybackPhysicsMode());
    }
    if (app->CameraMode() != 0) {
        ReloadModels(app);
        RefreshLightPanel(app);
        RefreshSelfShadowPanel(app);
        ApplyGravityTrack(app);
        for (std::int32_t i = 0; i < 0xFF; ++i) {
            if (app->AccessorySlot(i) != nullptr) {
                ApplyAccessoryTrack(app, i);
            }
        }
        SyncAccessoryEditPanel(app);
    }
}
// ------------------------------------------------------------------
// 422 (0x00485CF2): reverse paste.  0x2F8 == 0 with a non-zero bone
// count: identical undo-table setup as 421 (bone snapshot), then the
// RegisterMirroredBoneKey mirrored insert loop over 0x354 bone records; morph/camera
// buffers are left untouched.  Tail: PanelPaint, SelectionReeval,
// SeekModelFrame(model, frame, 0xA0CC4) and byte 0x9EDB5 = 1 (no camera-
// mode branch at all).
// ------------------------------------------------------------------
void PasteMirroredBoneKeyframes(MMDApp* app, HWND hwnd) {
    if (app->CameraMode() != 0) {
        return; // was: break (switch exit)
    }
    const std::uint32_t boneSel = app->ClipboardCounts().bones;
    if (boneSel == 0) {
        return; // was: break (switch exit)
    }
    app->SceneModified() = 1;
    const std::int32_t frame = app->state.currentFrame;
    unsigned char* model = app->SelectedModel();
    // clear all model-mode marks (loc_485D60..)
    ClearModelKeyMarks(mdl::BoneKeys(model), mdl::kBoneKeyCapacity);
    ClearModelKeyMarks(mdl::MorphKeys(model), mdl::kMorphKeyCapacity);
    ClearModelKeyMarks(mdl::DisplayKeys(model), mdl::kDisplayKeyCapacity);
    // undo-table entry (loc_485D88..) - identical shape to 421
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    mdl::Mdl(model)->undoDirty = 1;
    mdl::Mdl(model)->redoDirty = 0;
    mdl::ModelRecord& record = *mdl::Mdl(model);
    const std::int32_t next = record.UndoCursor() + 1;
    record.UndoCursor() = next >= mdl::kUndoHistoryDepth ? 0 : next;
    const std::int32_t cur = record.UndoCursor();
    record.LatestUndoCursor() = cur;
    auto& undo = mikudancestudio::mdl::Mdl(model)->undoRings[0].slots[cur];
    undo.operation = mdl::UndoOperation::BoneKeys;
    undo.frame = frame;
    void* p = undo.bonePose;
    if (p != nullptr) {
        ::operator delete(p);
        undo.bonePose = nullptr;
    }
    const std::int32_t boneCount = mdl::Mdl(model)->boneCount;
    auto* undoBone = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(::operator new(
        static_cast<std::uint32_t>(boneCount) * sizeof(mikudancestudio::mdl::BonePoseSnapshot)));
    if (undoBone != nullptr) {
        ConstructArrayElements(undoBone, sizeof(mikudancestudio::mdl::BonePoseSnapshot),
                               static_cast<std::uint32_t>(boneCount),
                               reinterpret_cast<void*>(&IdentityCtor));
    }
    undo.bonePose = undoBone;
    memset(undoBone, 0,
           static_cast<std::uint32_t>(boneCount) * sizeof(mikudancestudio::mdl::BonePoseSnapshot));
    if (boneCount > 0) {
        const mdl::BoneRecord* bones = record.boneTable;
        unsigned char* boneState = record.bonePhysicsState;
        for (std::int32_t i = 0; i < boneCount; ++i) {
            auto& rec = undoBone[i];
            const auto& src = bones[i];
            rec.boneIndex = i;
            memcpy(rec.position, src.trans, sizeof rec.position);
            memcpy(rec.rotation, src.rotQuat, sizeof rec.rotation);
            rec.physicsDisabled = boneState[i];
        }
    }
    undo.recordCount = 0;
    p = undo.auxiliaryPose;
    if (p != nullptr) {
        ::operator delete(p);
        undo.auxiliaryPose = nullptr;
    }
    unsigned char* undoLight = static_cast<unsigned char*>(::operator new(boneSel * 3 * 0x40));
    if (undoLight != nullptr) {
        ConstructArrayElements(undoLight, 0x40, boneSel * 3,
                               reinterpret_cast<void*>(&IdentityCtor));
    }
    undo.auxiliaryPose = undoLight;
    memset(undoLight, 0, boneSel * 3 * 0x40);
    std::memset(record.keyVisitMap, 0, sizeof(record.keyVisitMap));
    ResetBoneKeyCursor(model);
    // mirrored bone paste loop (loc_486255..486291)
    if (boneSel != 0) {
        const auto* src = app->BoneClipboard();
        std::uint32_t i = 0;
        do {
            BoneClipboardRecord rec = src[i];
            if (RegisterMirroredBoneKey(model, rec, frame) == 0) {
                break;
            }
            ++i;
        } while (i < boneSel);
    }
    // tail (loc_4862A5..4862DB)
    PanelPaint(app);
    SelectionReeval(app);
    SeekModelFrame(*mdl::Mdl(model), frame, app->PlaybackPhysicsMode());
    app->PhysicsResetPending() = 1;
}

} // namespace mikudancestudio
