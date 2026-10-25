// ===========================================================================
// Edit-menu frame-line commands + select-all frame groups.
//   VA 0x00439E40  insert frame line (bone / camera)    menu 0xFF,  key I
//   VA 0x0043A650  delete frame line (bone / camera)    menu 0x100, key K
//   VA 0x0043B720  insert frame line (facial / light)   menu 0x101, key U
//   VA 0x0043BB30  delete frame line (facial / light)   menu 0x102, key J
//   SelectFrameGroup factors the inline dispatcher blocks at
//   0x4831EA (0xD9 bone) / 0x4832C8 (0xDA disp-IK-OP) / 0x483258 (0xDC facial)
// ===========================================================================
// All four frame-line commands branch on the edit-state byte app+0x2F8
// (kByteOptflag0): clear  -> the active model's frame tables, set -> the
// global camera (0x374, 84B records) or light (0x378, 40B records) table.
// "Insert frame line" shifts every key with frame >= current frame
// (app+0x980) up by one; "delete frame line" unlinks keys exactly at the
// current frame and shifts everything above down by one, merging a key
// that lands on frame 0 into its predecessor record slot.
//
// The bone variants additionally take an undo snapshot (model+0x26EC ring,
// 30 slots x 28B: type=2 / count / frame / pose ptr / keys ptr); the
// facial/light variants and the camera variants have NO undo in the
// original.  Only records at index >= bone/morph count participate - the
// per-bone/per-morph head records (index == track id) are never moved,
// matching the original's guard `(index < count) ^ 1`.
//
// Ghidra quirks verified against the already-ported undo machinery in
// src/model/model_keyframe_edit.cpp (0x49D410 family):
//   * SBORROW4(cursor,0x1E)==(cursor-0x1E<0)  ==  signed cursor >= 0x1E
//     -> cursor wraps to 0 at 30.
//   * the EnableWindow argument triples are (hwnd, item, bEnable): both
//     commands enable the undo button 0x190 and disable the redo button
//     0x191 - the same pair as the 0xFA paste - corroborated by the
//     cleared redo flag (model+0x31BD = 0) both commands write.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <new>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

// App-field offsets (file-local).

mdl::ModelRecord* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

mikudancestudio::mdl::UndoRecord& CurrentUndo(mdl::ModelRecord* model) {
    return model->undoRings[0].slots[model->UndoCursor()];
}

// Undo slot header (0x43A055..0x43A2D2 / 0x43AC..0x43B0x in both bone
// frame-line commands): dirty/redo flags, cursor advance with the 30-slot
// wrap, type 2, current frame, then the pose snapshot.
void BeginUndoEdit(mdl::ModelRecord* model, std::uint32_t frame) {
    model->undoDirty = 1;
    model->redoDirty = 0;
    std::int32_t cursor = model->UndoCursor() + 1;
    if (cursor >= mdl::kUndoHistoryDepth)
        cursor = 0;
    model->UndoCursor() = cursor;
    model->LatestUndoCursor() = cursor;
    auto& undo = CurrentUndo(model);
    undo.operation = mdl::UndoOperation::BoneKeys;
    undo.frame = frame;
}

// Pose snapshot: boneCount x 0x24 records {boneIdx, pos(3f), quat(4f),
// pending-pose-edit byte}. The flag comes from bonePhysicsState, not
// boneSelection (x64 0x7FF7CB4A9EC0 / 0x7FF7CB4AA8C0 read model+0x3128).
void SnapshotPose(mdl::ModelRecord* model) {
    auto& undo = CurrentUndo(model);
    auto*& slot = undo.bonePose;
    if (slot != nullptr) {
        ::operator delete(slot);
        slot = nullptr;
    }
    const std::int32_t count = model->boneCount;
    slot = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(::operator new(
        static_cast<std::size_t>(count) * sizeof(mikudancestudio::mdl::BonePoseSnapshot)));
    std::memset(slot, 0, static_cast<std::size_t>(count) * sizeof(*slot));
    mikudancestudio::mdl::BoneRecord* bones = model->boneTable;
    const unsigned char* editFlags = model->bonePhysicsState;
    for (std::int32_t i = 0; i < count; ++i) {
        auto& out = slot[i];
        mikudancestudio::mdl::BoneRecord* bone = &bones[i];
        out.boneIndex = i;
        std::memcpy(out.position, bone->trans, sizeof out.position);
        std::memcpy(out.rotation, bone->rotQuat, sizeof out.rotation);
        out.poseEdited = editFlags[i];
    }
}

// Key-record snapshot buffer: count field zeroed, keys buffer reallocated
// (freed then malloc'ed and zeroed; original 0x43A4DE..0x43A555).
void AllocUndoKeys(mdl::ModelRecord* model, std::size_t count) {
    auto& undo = CurrentUndo(model);
    undo.recordCount = 0;
    auto*& slot = undo.keySnapshots;
    if (slot != nullptr) {
        ::operator delete(slot);
        slot = nullptr;
    }
    slot = static_cast<mdl::BoneKeyUndoEntry*>(::operator new(count * sizeof(*slot)));
    std::memset(slot, 0, count * sizeof(*slot));
}

// Clear a removed bone-key record (0x43B1F9..0x43B20D / 0x43B637..):
// frame, used flag, position, quaternion (w reset to 1.0f).  prev/next
// (+4/+8) are cleared separately AFTER the interpolation rebuild.
void ClearBoneKeyRecord(mdl::BoneKey& key) {
    key.frame = 0;
    key.allocated = 0;
    std::memset(key.position, 0, sizeof(key.position));
    key.rotation[0] = 0.0f;
    key.rotation[1] = 0.0f;
    key.rotation[2] = 0.0f;
    key.rotation[3] = 1.0f;
}

// Copy every payload field of a bone-key record into another slot
// (0x43B2..0x43B6x merge-into-predecessor): the 16 interpolation bytes
// (+0xC..+0x1B, copied as the original's individual byte/dword stores),
// position, quaternion, used flag.  frame/prev/next are not copied.
// x64 twin sub_7FF7CB4AA110 copies +0x0C..+0x27 (interp), +0x1C..+0x33
// (pos/quat) and the +0x38 flag BYTE only - physics (+0x39) and the
// +0x3A..+0x3B padding stay untouched (the x86 build's dword store at
// +0x38 used to sweep them along).
void CopyBoneKeyPayload(mdl::BoneKey& dst, const mdl::BoneKey& src) {
    std::memcpy(dst.interpolation, src.interpolation, sizeof(dst.interpolation));
    std::memcpy(dst.position, src.position, sizeof(dst.position));
    std::memcpy(dst.rotation, src.rotation, sizeof(dst.rotation));
    dst.allocated = src.allocated;
}

// Copy every payload field of an 84B camera record into another slot
// (0x43AA6A..0x43ABF5): the frame-value dwords and the 16 interpolation
// bytes - 4 per curve at +0x28..+0x2B/+0x2E..+0x31/+0x34..+0x37/+0x3A..+0x3D
// (x64 0x7FF7CB4AA3BD..0x7FF7CB4AA4BC; the two padding bytes between rows are not copied).
void CopyCameraKeyPayload(mdl::CameraKey& dst, const mdl::CameraKey& src) {
    dst.distance = src.distance;
    std::memcpy(dst.eye, src.eye, sizeof(dst.eye));
    std::memcpy(dst.target, src.target, sizeof(dst.target));
    for (int c = 0; c < 4; ++c)
        std::memcpy(dst.interpolation[c], src.interpolation[c], 4);
    dst.perspective = src.perspective;
    dst.fov = src.fov;
}

// Zero a removed camera record (0x43A939..0x43A96F): the exact dword/byte
// set of the original - eye[2](+0x18)/target[2](+0x24) and the per-curve
// 2-byte padding are NOT cleared (frame 0 marks the slot free).
// Zero interpolation bytes per curve: the first four bytes of each (x64 0x7FF7CB4AA173..0x7FF7CB4AA2E7,
// namely +0x28..+0x2B/+0x2E..+0x31/+0x34..+0x37/+0x3A..+0x3D).
void ClearCameraKeyRecord(mdl::CameraKey& key) {
    key.next = 0;
    key.previous = 0;
    key.frame = 0;
    key.perspective = 0;
    key.distance = 0.0f;
    key.fov = 0;
    key.eye[0] = key.eye[1] = 0.0f;
    key.target[0] = key.target[1] = 0.0f;
    for (auto& row : key.interpolation)
        std::memset(row, 0, 4);
    key.selected = 0;
}

} // namespace

// ---- VA 0x00439E40: insert frame line (bone / camera) ----
void InsertBoneCameraFrameLine(MMDApp* app) {
    const std::uint32_t currentFrame = static_cast<std::uint32_t>(app->CurrentFrame());

    if (app->CameraMode() == 0) {
        // ---- bone mode: shift every non-head key at frame >= currentFrame up one
        mdl::ModelRecord* model = ActiveModel(app);
        const std::int32_t boneCount = model->boneCount;
        mdl::BoneKey* keys = model->boneKeys;

        int affected = 0; // stack0xfffffff8 (0x439E75..0x439F9D)
        for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i) {
            const std::uint32_t keyFrame = keys[i].frame;
            if (keyFrame != 0 && i >= boneCount && keyFrame >= currentFrame)
                ++affected;
        }
        if (affected == 0)
            return;

        EnableWindow(GetDlgItem(app->state.hwnd, panel::kUndoButton), TRUE);
        EnableWindow(GetDlgItem(app->state.hwnd, panel::kRedoButton), FALSE);

        BeginUndoEdit(model, currentFrame); // 0x43A02F..
        SnapshotPose(model);                // 0x43A275..
        // Allocate the undo key buffer by affected key count: x64 0x7FF7CB4A9F98
        // operator new(saturated_mul(v9, 0x40)), where v9 is the affected count calculated above.
        AllocUndoKeys(model, // 0x43A4DE..
                      static_cast<std::size_t>(affected));
        std::memset(model->keyVisitMap, 0,
                    sizeof(model->keyVisitMap)); // 0x43A555

        for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i) { // 0x43A56D..
            mdl::BoneKey& key = keys[i];
            const std::uint32_t keyFrame = key.frame;
            if (keyFrame != 0 && i >= boneCount && keyFrame >= currentFrame) {
                AppendBoneKeyToUndo(model, i); // snapshot record
                key.frame = keyFrame + 1;
                if (static_cast<std::int32_t>(model->maxFrame) <
                    static_cast<std::int32_t>(keyFrame) + 1)
                    model->maxFrame = static_cast<std::int32_t>(keyFrame) + 1;
            }
        }

        SeekModelFrame(*model, static_cast<int>(currentFrame), app->PlaybackPhysicsMode());
        PanelPaint(app);
        SelectionReeval(app);
        app->SceneModified() = 1;
        return;
    }

    // ---- camera mode: shift every camera key at frame >= currentFrame up one
    // (0x439FBB..0x439FAA; only the 0x374 table moves)
    auto* keys = app->CameraKeys();
    for (int r = 0; r < static_cast<int>(mdl::kTimelineKeyCapacity); ++r) {
        mdl::CameraKey& key = keys[r];
        const std::uint32_t keyFrame = key.frame;
        if (keyFrame != 0 && keyFrame >= currentFrame) {
            key.frame = keyFrame + 1;
            if (app->LastRegisteredFrame() < static_cast<std::int32_t>(keyFrame + 1))
                app->LastRegisteredFrame() = static_cast<std::int32_t>(keyFrame + 1);
        }
    }
    ReloadModels(app); // VA 0x0042E640 (camera key seek/evaluate)
    PanelPaint(app);
    app->SceneModified() = 1;
}

// ---- VA 0x0043A650: delete frame line (bone / camera) ----
void DeleteBoneCameraFrameLine(MMDApp* app) {
    const std::uint32_t currentFrame = static_cast<std::uint32_t>(app->CurrentFrame());

    if (app->CameraMode() == 0) {
        // ---- bone mode
        mdl::ModelRecord* model = ActiveModel(app);
        const std::int32_t boneCount = model->boneCount;
        mdl::BoneKey* keys = model->boneKeys;

        // Undo sizing (0x43A675..0x43A72E): a key exactly at the current
        // frame consumes three snapshot slots (prev/self/next), a key
        // above it one.
        int affected = 0; // uStack_c
        for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i) {
            const std::uint32_t keyFrame = keys[i].frame;
            if (i >= boneCount && keyFrame != 0) {
                if (keyFrame == currentFrame)
                    affected += 3;
                else if (currentFrame < keyFrame)
                    affected += 1;
            }
        }
        if (affected == 0)
            return;

        EnableWindow(GetDlgItem(app->state.hwnd, panel::kUndoButton), TRUE);
        EnableWindow(GetDlgItem(app->state.hwnd, panel::kRedoButton), FALSE);

        BeginUndoEdit(model, currentFrame); // 0x43AB9C..
        SnapshotPose(model);                // 0x43ACF2..
        AllocUndoKeys(model,                // 0x43AF60..
                      static_cast<std::size_t>(affected));
        std::memset(model->keyVisitMap, 0,
                    sizeof(model->keyVisitMap)); // 0x43AFD3

        // Auto-interpolation rebuild gate: checkbox 0x212 (0x43AFF4).
        const bool autoInterp =
            SendMessageA(GetDlgItem(app->state.hwnd, panel::kPhysicsFrameCheckbox), BM_GETCHECK, 0,
                         0) == 1;

        for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i) { // 0x43B018..
            mdl::BoneKey& key = keys[i];
            const std::uint32_t keyFrame = key.frame;

            if (keyFrame != 0 && i >= boneCount && keyFrame == currentFrame) {
                // Key exactly at the current frame: unlink and clear.
                const std::int32_t prev = static_cast<std::int32_t>(key.previous);
                const std::int32_t next = static_cast<std::int32_t>(key.next);
                AppendBoneKeyToUndo(model, prev); // 0x43B06B
                AppendBoneKeyToUndo(model, i);    // 0x43B07F
                AppendBoneKeyToUndo(model, next); // 0x43B09D
                keys[prev].next = next;
                keys[next].previous = prev;
                ClearBoneKeyRecord(key); // 0x43B1F9..
                if (autoInterp) {        // 0x43B20D..
                    for (int lane = 0; lane < 4; ++lane) {
                        const std::int32_t n = static_cast<std::int32_t>(key.next);
                        RebuildBoneKeyInterpolation(
                            model, n == 0 ? static_cast<std::int32_t>(key.previous) : n, lane);
                    }
                }
                key.next = 0;
                key.previous = 0;
                continue;
            }

            if (keyFrame != 0 && i >= boneCount && currentFrame < keyFrame) {
                // Key above the current frame: shift down by one.
                AppendBoneKeyToUndo(model, i); // 0x43B28B
                const std::uint32_t shiftedFrame = keyFrame - 1;
                key.frame = shiftedFrame;
                if (shiftedFrame == 0) {
                    // Landed on frame 0: merge the payload into the
                    // predecessor record slot (0x43B2B0..0x43B630).
                    const std::int32_t prev = static_cast<std::int32_t>(key.previous);
                    mdl::BoneKey& dst = keys[prev];
                    dst.next = key.next;
                    CopyBoneKeyPayload(dst, key);
                    const std::int32_t next = static_cast<std::int32_t>(key.next);
                    if (next != 0)
                        keys[next].previous = prev;
                    if (autoInterp) { // 0x43B68E..
                        for (int lane = 0; lane < 4; ++lane) {
                            const std::int32_t n = static_cast<std::int32_t>(key.next);
                            RebuildBoneKeyInterpolation(
                                model, n == 0 ? static_cast<std::int32_t>(key.previous) : n, lane);
                        }
                    }
                    ClearBoneKeyRecord(key); // 0x43B637..
                }
            }
        }

        SeekModelFrame(*model, static_cast<int>(currentFrame), app->PlaybackPhysicsMode());
        PanelPaint(app);
        SelectionReeval(app);
        app->SceneModified() = 1;
        return;
    }

    // ---- camera mode (0x43A905..0x43A9B6)
    auto* keys = app->CameraKeys();
    for (int r = 0; r < static_cast<int>(mdl::kTimelineKeyCapacity); ++r) {
        mdl::CameraKey& key = keys[r];
        const std::uint32_t keyFrame = key.frame;

        if (keyFrame != 0 && keyFrame == currentFrame) {
            // Unlink and clear (no undo for the camera table).
            const std::int32_t prev = key.previous;
            const std::int32_t next = key.next;
            keys[prev].next = next;
            keys[next].previous = prev;
            ClearCameraKeyRecord(key);
            continue;
        }

        const std::uint32_t remainingFrame = key.frame;
        if (remainingFrame != 0 && currentFrame < remainingFrame) {
            const std::uint32_t shiftedFrame = remainingFrame - 1;
            key.frame = shiftedFrame;
            if (shiftedFrame == 0) {
                // Merge into the predecessor slot; the emptied record is
                // left as-is apart from its frame number.
                const std::int32_t prev = key.previous;
                mdl::CameraKey& dst = keys[prev];
                dst.next = key.next;
                CopyCameraKeyPayload(dst, key);
                const std::int32_t next = key.next;
                if (next != 0)
                    keys[next].previous = prev;
            }
        }
    }
    ReloadModels(app);
    PanelPaint(app);
    app->SceneModified() = 1;
}

// ---- VA 0x0043B720: insert frame line (facial / light) ---
void InsertFacialLightFrameLine(MMDApp* app) {
    const std::uint32_t currentFrame = static_cast<std::uint32_t>(app->CurrentFrame());

    if (app->CameraMode() != 0) {
        // ---- light mode: shift every light key at frame >= currentFrame up one
        // (0x43B72D..0x43B878; only the 0x378 table moves; no undo)
        auto* keys = app->LightKeys();
        for (int r = 0; r < static_cast<int>(mdl::kTimelineKeyCapacity); ++r) {
            mdl::LightKey& key = keys[r];
            const std::uint32_t keyFrame = key.frame;
            if (keyFrame != 0 && keyFrame >= currentFrame) {
                key.frame = keyFrame + 1;
                if (app->LastRegisteredFrame() < static_cast<std::int32_t>(keyFrame + 1))
                    app->LastRegisteredFrame() = static_cast<std::int32_t>(keyFrame + 1);
            }
        }
        RefreshLightPanel(app);
        PanelPaint(app);
        app->SceneModified() = 1;
        return;
    }

    // ---- facial mode: morph keys, 0x14 stride x 20000 (no undo)
    mdl::ModelRecord* model = ActiveModel(app);
    const std::int32_t morphCount = model->morphCount;
    mdl::MorphKey* keys = model->morphKeys;
    for (int i = 0; i < static_cast<int>(mdl::kMorphKeyCapacity); ++i) {
        mdl::MorphKey& key = keys[i];
        const std::uint32_t keyFrame = key.frame;
        if (keyFrame != 0 && i >= morphCount && keyFrame >= currentFrame) {
            key.frame = keyFrame + 1;
            if (static_cast<std::int32_t>(model->maxFrame) <
                static_cast<std::int32_t>(keyFrame) + 1)
                model->maxFrame = static_cast<std::int32_t>(keyFrame) + 1;
        }
    }
    SeekModelFrame(*model, static_cast<int>(currentFrame), app->PlaybackPhysicsMode());
    PanelPaint(app);
    SelectionReeval(app);
    app->SceneModified() = 1;
}

// ---- VA 0x0043BB30: delete frame line (facial / light) ---
void DeleteFacialLightFrameLine(MMDApp* app) {
    const std::uint32_t currentFrame = static_cast<std::uint32_t>(app->CurrentFrame());

    if (app->CameraMode() == 0) {
        // ---- facial mode: morph keys, 0x14 stride x 20000
        mdl::ModelRecord* model = ActiveModel(app);
        const std::int32_t morphCount = model->morphCount;
        mdl::MorphKey* keys = model->morphKeys;
        for (int i = 0; i < static_cast<int>(mdl::kMorphKeyCapacity); ++i) {
            mdl::MorphKey& key = keys[i];
            const std::uint32_t keyFrame = key.frame;

            if (keyFrame != 0 && i >= morphCount && keyFrame == currentFrame) {
                // Unlink and clear (0x43BB44..0x43BBB6); no undo.
                const std::int32_t prev = key.previous;
                const std::int32_t next = key.next;
                keys[prev].next = next;
                keys[next].previous = prev;
                key.frame = 0;
                key.allocated = 0;
                key.value = 0.0f;
                key.next = 0;
                key.previous = 0;
                continue;
            }

            if (keyFrame != 0 && i >= morphCount && currentFrame < keyFrame) {
                const std::uint32_t shiftedFrame = keyFrame - 1;
                key.frame = shiftedFrame;
                if (shiftedFrame == 0) {
                    // Merge the payload (+0xC value, +0x10 used flag)
                    // into the predecessor slot (0x43BC0E..0x43BC5F);
                    // the emptied record keeps only frame 0.
                    const std::int32_t prev = key.previous;
                    mdl::MorphKey& dst = keys[prev];
                    dst.next = key.next;
                    dst.allocated = key.allocated;
                    std::memcpy(dst.reserved, key.reserved, sizeof(dst.reserved));
                    dst.value = key.value;
                    const std::int32_t next = key.next;
                    if (next != 0)
                        keys[next].previous = prev;
                }
            }
        }
        SeekModelFrame(*model, static_cast<int>(currentFrame), app->PlaybackPhysicsMode());
        PanelPaint(app);
        SelectionReeval(app);
        app->SceneModified() = 1;
        return;
    }

    // ---- light mode: 40B records x 10000 (0x43BC64..0x43BC88)
    auto* keys = app->LightKeys();
    for (int r = 0; r < static_cast<int>(mdl::kTimelineKeyCapacity); ++r) {
        mdl::LightKey& key = keys[r];
        const std::uint32_t keyFrame = key.frame;

        if (keyFrame != 0 && keyFrame == currentFrame) {
            const std::int32_t prev = key.previous;
            const std::int32_t next = key.next;
            keys[prev].next = next;
            keys[next].previous = prev;
            key.next = 0;
            key.previous = 0;
            key.frame = 0;
            key.selected = 0;
            std::memset(key.reserved1, 0, sizeof(key.reserved1));
            continue;
        }

        const std::uint32_t remainingFrame = key.frame;
        if (remainingFrame != 0 && currentFrame < remainingFrame) {
            const std::uint32_t shiftedFrame = remainingFrame - 1;
            key.frame = shiftedFrame;
            if (shiftedFrame == 0) {
                const std::int32_t prev = key.previous;
                mdl::LightKey& dst = keys[prev];
                dst.next = key.next;
                std::memcpy(dst.direction, key.direction, sizeof(dst.direction));
                std::memcpy(dst.color, key.color, sizeof(dst.color));
                const std::int32_t next = key.next;
                if (next != 0)
                    keys[next].previous = prev;
            }
        }
    }
    RefreshLightPanel(app);
    PanelPaint(app);
    app->SceneModified() = 1;
}

// ---- SelectFrameGroup -------------------------------------------------------
// Dispatcher inline blocks 0x4831EA (0xD9) / 0x4832C8 (0xDA) / 0x483258
// (0xDC): clear the used-flag of every record, then mark it selected when
// it is a head record (index < track count, bone and facial variants) or
// carries a key (frame != 0).  The 0xDA variant additionally forces
// record 0's flag after the loop.  x64 twins 0x7FF7CB465460 / 0x7FF7CB465540
// / 0x7FF7CB4654D0 use BYTE stores on the flag only (the x86 build's dword
// store also swept the physics byte and padding - not reproduced).
void SelectFrameGroup(MMDApp* app, FrameGroup group) {
    mdl::ModelRecord* model = ActiveModel(app);

    if (group == FrameGroup::Bone) {
        // 0xD9: all bone frames (0x26E0 table, 0x3C x kBoneKeyCapacity).
        // x64 dispatcher 0x7FF7CB465460..0x7FF7CB4654BA touches ONLY the
        // +0x38 flag byte (clear, then conditional byte store of 1); the
        // x86 build's dword store used to sweep +0x39 (physics) and
        // +0x3A..+0x3B along - keep them intact.
        mdl::BoneKey* keys = model->boneKeys;
        const std::int32_t boneCount = model->boneCount;
        for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i) {
            mdl::BoneKey& key = keys[i];
            key.allocated = 0;
            if (i < boneCount || key.frame != 0) {
                key.allocated = 1;
            }
        }
    } else if (group == FrameGroup::Display) {
        // 0xDA: all disp/IK/OP frames (0x26E8 table, 0x28 x 1000 on x64).
        // 0x7FF7CB465540..0x7FF7CB4655A2: byte stores at +0x18 only, plus
        // the forced record-0 flag after the loop.
        mdl::DisplayKey* frames = model->displayKeys;
        for (int i = 0; i < static_cast<int>(mdl::kDisplayKeyCapacity); ++i) {
            frames[i].allocated = 0;
            if (frames[i].frame != 0) {
                frames[i].allocated = 1;
            }
        }
        frames[0].allocated = 1;
    } else {
        // 0xDC: all facial frames (0x26E4 table, 0x14 x 20000).
        // 0x7FF7CB4654D0..0x7FF7CB46552E: byte stores at +0x10 only.
        mdl::MorphKey* frames = model->morphKeys;
        const std::int32_t morphCount = model->morphCount;
        for (int i = 0; i < static_cast<int>(mdl::kMorphKeyCapacity); ++i) {
            frames[i].allocated = 0;
            if (i < morphCount || frames[i].frame != 0) {
                frames[i].allocated = 1;
            }
        }
    }

    PanelPaint(app);
    SelectionReeval(app);
}

} // namespace mikudancestudio
