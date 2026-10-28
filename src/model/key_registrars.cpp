// ===========================================================================
// VA 0x0049D880 / 0x0049E310 / 0x0049F190 / 0x0049F8C0 /
//    0x004A4940 / 0x004A49A0 / 0x004A27F0
// - key registrars + allocator resets for the VMD load path, plus the
//   name-based key-track frame marker of the frame-range editor
// ===========================================================================
// BoneClipboardRecord, MorphClipboardRecord and the display input entries are
// runtime work records shared by VMD loading, clipboard paste and range edits.
// Their field offsets are pinned in clipboard_layout.hpp; VMD disk records are
// decoded field by field and have different sizes and ordering.
//
// Registrars walk a sorted doubly linked track, overwrite exact frames, or
// allocate and splice a free key. The model search cursor advances persistently.
// Bone changes append undo entries before changing either key or link. Fixed-axis
// correction can modify the input quaternion, so bone input is intentionally mutable.
// Mirrored paste reflects position X and quaternion Y/Z, including name matching.
// Morph lookup advances one native MorphRecord (x86 136 / x64 192 bytes).
// Display input names resolve IK flags and external-parent selector pairs.
//
// Verified original entry points:
//   bone:    x86 0x49D880 / x64 0x7FF7CB4E9390
//   mirror:  x86 0x49E310
//   morph:   x86 0x49F190 / x64 0x7FF7CB4EACF0
//   display: x86 0x49F8C0 / x64 0x7FF7CB4EB3C0
// Preserve the original raw-frameOffset max-frame update in the bone overwrite
// and insert paths. MarkKeyTrackRangeByName retains its original name-resolution
// priority and inclusive, unsigned frame bounds.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/bone_axis.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/clipboard_layout.hpp"

namespace mikudancestudio {
namespace {

// JP overflow strings (0x52B918 / 0x52B908, SJIS byte-exact)
const char kJpOverflow[] = "\x93\x6f\x98\x5e\x83\x7c\x83\x43\x83\x93\x83\x67\x90\x94\x82\xaa%d"
                           "\x8c\xc2\x82\xf0\x89\x7a\x82\xa6\x82\xdc\x82\xb5\x82\xbd\n"
                           "\x82\xb1\x82\xea\x88\xc8\x8f\xe3\x82\xcc\x93\x6f\x98\x5e\x82\xcd\x8d"
                           "\x73\x82\xa6\x82\xdc\x82\xb9\x82\xf1\n"
                           "\x81\x75\xcc\xda\xb0\xd1\x95\xd2\x8f\x57\x81\x76\x82\xcc\x81\x75\x95"
                           "\x73\x97\x70\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c\x81\x76\x82\xf0\x8e\xc0"
                           "\x8d\x73\x82\xb5\x82\xc4\x89\xba\x82\xb3\x82\xa2";
const char kJpFrameRegTitle[] = // 0x52B908 "Register frame"
    "\xcc\xda\xb0\xd1\x93\x6f\x98\x5e";

// shared overflow message (limit differs per registrar); the box owner
// HWND is read from model+0
void OverflowBox(const mdl::ModelRecord* model, int limit) {
    char text[256];
    // x64 sub_7FF7CB4E9390 @0x7FF7CB4E9D4B: language byte selects both the
    // format and the caption ("register frame" / JP "register frame").
    if (model->physicsFlags != 0) {
        sprintf_s(text, 0x100,
                  "You cannot regist over %d point\n"
                  "Please execute 'delete unused frame'",
                  limit);
        MessageBoxA(static_cast<HWND>(model->hwnd), text, "register frame", 0);
    } else {
        sprintf_s(text, 0x100, kJpOverflow, limit);
        MessageBoxA(static_cast<HWND>(model->hwnd), text, kJpFrameRegTitle, 0);
    }
}

// copy the parsed bone-key fields into a 60-byte record (the exact-
// overwrite and fresh-record paths share this; dst frame/links excluded)
void FillBoneRecord(mdl::BoneKey& dst, const mdl::BoneClipboardRecord& rec, bool writePos) {
    std::memcpy(dst.rotation, rec.rotation, sizeof dst.rotation);
    if (writePos)
        std::memcpy(dst.position, rec.position, sizeof dst.position);
    dst.physicsDisabled = rec.physicsDisabled;
    std::memcpy(dst.interpolation, rec.interpolation, sizeof dst.interpolation);
    dst.allocated = 1;
}

// 0x49E310 stores the copied pose after reflecting it across the model's
// YZ plane. Quaternion Y/Z and position X change sign.
void FillMirroredBoneRecord(mdl::BoneKey& dst, const mdl::BoneClipboardRecord& rec) {
    std::memcpy(dst.rotation, rec.rotation, sizeof dst.rotation);
    dst.rotation[1] = -dst.rotation[1];
    dst.rotation[2] = -dst.rotation[2];
    std::memcpy(dst.position, rec.position, sizeof dst.position);
    dst.position[0] = -dst.position[0];
    dst.physicsDisabled = rec.physicsDisabled;
    std::memcpy(dst.interpolation, rec.interpolation, sizeof dst.interpolation);
    dst.allocated = 1;
}

int FindMirroredBone(const mdl::ModelRecord* model, const mdl::BoneClipboardRecord& rec) {
    // Shift-JIS: 0x89 0x45 = right, 0x8D 0xB6 = left. MMD swaps the first
    // matching marker and compares the suffix after the two-byte character.
    static const char kRight[] = "\x89\x45";
    static const char kLeft[] = "\x8D\xB6";
    const char* const name = rec.name;
    const mdl::ModelRecord& state = *model;
    const mdl::BoneRecord* const bones = state.boneTable;
    const int boneCount = static_cast<int>(state.boneCount);

    const auto findOpposite = [&](const char* marker, const char* opposite) -> int {
        const char* hit = std::strstr(name, marker);
        if (hit == nullptr || boneCount <= 0)
            return -1;
        const char* const suffix = hit + 2;
        for (int i = 0; i < boneCount; ++i) {
            const char* const boneName = bones[i].name;
            const char* other = std::strstr(boneName, opposite);
            if (other != nullptr && std::strcmp(suffix, other + 2) == 0)
                return i;
        }
        return -1;
    };

    // x64 sub_7FF7CB4E9DB0: first "left" in the source name (0x7FF7CB4E9DE6,
    // strstr at 0x7FF7CB4E9DF7) -> bones containing "right"; only when the name
    // lacks "left" does the right-in-source -> left-in-bones pass run (0x7FF7CB4E9E7C).
    int index = findOpposite(kLeft, kRight);
    if (index >= 0)
        return index;
    index = findOpposite(kRight, kLeft);
    if (index >= 0)
        return index;
    for (int i = 0; i < boneCount; ++i) {
        if (std::strcmp(name, bones[i].name) == 0)
            return i;
    }
    return -1;
}

// Name probes of 0x4A27F0 (SJIS byte-exact, compare lengths as in the
// binary, NUL included; x86 0x531120 / 0x531118 = x64 0x552048 /
// 0x5524D8).
const unsigned char kJpDispIkOp[] = // Display/IK/external parent (13)
    {0x95, 0x5C, 0x8E, 0xA6, 0xA5, 0x49, 0x4B, 0xA5, 0x8A, 0x4F, 0x90, 0x65, 0x00};
const unsigned char kJpBone01[] = // ﾎﾞｰﾝ01 (7)
    {0xCE, 0xDE, 0xB0, 0xDD, 0x30, 0x31, 0x00};

// Track walk + mark shared by every branch of 0x4A27F0: `root` is the
// track's head record (record i of the bone/morph arrays, 0 for the
// display array).  Advance while frame < from (a chain that ends before
// `from` marks nothing - 0x4A2B15/0x4A2955/0x4A28B5), then set the mark
// byte of every record with frame <= to.  Returns the last record index
// marked, 0 when nothing was in range.
template <typename Key>
int MarkTrackRange(Key* keys, int root, std::uint32_t from, std::uint32_t to) {
    int cur = root;
    if (keys[cur].frame < from) {
        for (;;) {
            const int next = static_cast<int>(keys[cur].next);
            if (next == 0)
                return 0;
            cur = next;
            if (keys[cur].frame >= from)
                break;
        }
    }
    if (keys[cur].frame > to)
        return 0; // 0x4A2B3C
    int last = 0;
    for (;;) {
        keys[cur].allocated = 1; // 0x4A2B50
        last = cur;
        const int next = static_cast<int>(keys[cur].next);
        if (next == 0)
            break; // 0x4A2B60
        if (keys[next].frame > to)
            break; // 0x4A2B79
        cur = next;
    }
    return last;
}

} // namespace

// ---- VA 0x004A4940 --------------------------------------------------------
void ResetBoneKeyCursor(mdl::ModelRecord* model) {
    mdl::BoneKey* const keys = model->boneKeys;
    int& cursor = model->searchCursor;
    cursor = static_cast<int>(model->boneCount);
    if (keys[cursor].frame != 0) {
        do {
            if (++cursor >= static_cast<int>(mdl::kBoneKeyCapacity))
                break;
        } while (keys[cursor].frame != 0);
    }
}

// ---- VA 0x004A49A0 --------------------------------------------------------
int ResetMorphKeyCursor(mdl::ModelRecord* model) {
    mdl::MorphKey* const keys = model->morphKeys;
    int& cursor = model->searchCursor;
    cursor = static_cast<int>(model->morphCount);
    int result = 5 * cursor;
    if (keys[cursor].frame != 0) {
        do {
            if (++cursor >= static_cast<int>(mdl::kMorphKeyCapacity))
                break;
            result = 5 * cursor;
        } while (keys[cursor].frame != 0);
    }
    return result;
}

// ---- VA 0x0049EEE0 --------------------------------------------------------
// RegisterMorphKeyCurrent: register the current weight of
// one morph at an absolute frame.  Unlike the VMD/paste registrar below,
// this function takes a morph index directly and reads its live value from
// model+0x26C4[index].weight (+0x30).
void RegisterMorphKeyCurrent(mdl::ModelRecord* model, int morph, int frameArg) {
    if (model == nullptr || morph < 0 || (model->physicsMode != 2 && morph == 0) ||
        model->morphs == nullptr)
        return;

    mdl::MorphKey* const keys = model->morphKeys;
    const std::uint32_t frame = static_cast<std::uint32_t>(frameArg);
    const float value = model->morphs[morph].value;

    const auto fill = [&](int index) {
        keys[index].frame = frame;
        keys[index].value = value;
        keys[index].allocated = 1;
        if (frame > model->maxFrame)
            model->maxFrame = frame;
    };
    const auto allocate = [&]() -> int {
        int index = static_cast<int>(model->morphCount);
        while (index < static_cast<int>(mdl::kMorphKeyCapacity) && keys[index].frame != 0)
            ++index;
        if (index >= static_cast<int>(mdl::kMorphKeyCapacity)) {
            OverflowBox(model, mdl::kMorphKeyCapacity);
            return -1;
        }
        return index;
    };

    int current = morph;
    if (keys[current].frame < frame) {
        for (;;) {
            const int next = static_cast<int>(keys[current].next);
            if (next == 0)
                break;
            current = next;
            if (keys[current].frame >= frame)
                break;
        }
        if (keys[current].frame < frame) {
            const int fresh = allocate();
            if (fresh < 0)
                return;
            keys[current].next = static_cast<std::uint32_t>(fresh);
            keys[fresh].previous = static_cast<std::uint32_t>(current);
            fill(fresh);
            return;
        }
    }

    if (keys[current].frame == frame) {
        keys[current].value = value;
        keys[current].allocated = 1;
        return;
    }

    const int fresh = allocate();
    if (fresh < 0)
        return;
    const int previous = static_cast<int>(keys[current].previous);
    keys[previous].next = static_cast<std::uint32_t>(fresh);
    keys[fresh].previous = static_cast<std::uint32_t>(previous);
    keys[current].previous = static_cast<std::uint32_t>(fresh);
    keys[fresh].next = static_cast<std::uint32_t>(current);
    fill(fresh);
}

// ---- VA 0x0049D880 -----------------------------------------
bool RegisterBoneKey(mdl::ModelRecord* model, mdl::BoneClipboardRecord& rec, int frameOffset,
                     unsigned char useSelected) {
    mdl::BoneKey* const keys = model->boneKeys;
    const std::uint32_t frame = static_cast<std::uint32_t>(frameOffset) + rec.frame;

    // bone lookup by name (or the selected index)
    int boneIdx;
    if (useSelected != 0) {
        boneIdx = model->selectedBone;
    } else {
        boneIdx = 0;
        const int boneCnt = static_cast<int>(model->boneCount);
        if (boneCnt <= 0)
            return true;
        const mdl::BoneRecord* bone = model->boneTable;
        while (std::strcmp(rec.name, bone->name) != 0) {
            if (++boneIdx >= boneCnt)
                return true;
            ++bone;
        }
    }
    if (boneIdx == -1)
        return true;

    mdl::BoneRecord& bone = model->boneTable[boneIdx];
    const mdl::BoneType btype = bone.type;
    const bool writePos = btype == mdl::BoneType::RotateMove || btype == mdl::BoneType::Move ||
                          btype == mdl::BoneType::Ik;

    // twist correction: force the rotation axis onto the bone's own axis
    if (btype == mdl::BoneType::FixedAxis ||
        (btype == mdl::BoneType::UnderIk &&
         (bone.flags & mdl::kBoneFlagFixedAxis) == mdl::kBoneFlagFixedAxis)) {
        float boneAxis[3];
        if (model->physicsMode == 2) { // PMX
            std::memcpy(boneAxis, bone.axis, sizeof boneAxis);
        } else {
            const mdl::BoneRecord& tail = model->boneTable[bone.tailBone];
            boneAxis[0] = tail.position[0] - bone.position[0];
            boneAxis[1] = tail.position[1] - bone.position[1];
            boneAxis[2] = tail.position[2] - bone.position[2];
            auto* d3 = &d3dx::Get();
            d3->vec3Normalize(boneAxis, boneAxis);
        }
        if (!mdl::ConstrainBoneRotationAxis(rec.rotation, boneAxis))
            return false;
    }

    // walk the per-bone chain from the bone index itself
    int cur = boneIdx;
    if (keys[cur].frame < frame) {
        for (;;) {
            const int next = static_cast<int>(keys[cur].next);
            if (next == 0)
                break;
            cur = next;
            if (keys[cur].frame >= frame)
                break;
        }
        if (keys[cur].frame < frame) {
            // append after the last record (0x49DDA1 path)
            int free_ = model->searchCursor;
            if (keys[free_].frame != 0) {
                for (;;) {
                    ++model->searchCursor;
                    ++free_;
                    if (free_ >= static_cast<int>(mdl::kBoneKeyCapacity)) {
                        OverflowBox(model, static_cast<int>(mdl::kBoneKeyCapacity));
                        return false;
                    }
                    if (keys[free_].frame == 0)
                        break;
                }
            }
            AppendBoneKeyToUndo(model, cur);
            AppendBoneKeyToUndo(model, free_);
            keys[cur].next = static_cast<std::uint32_t>(free_);
            keys[free_].previous = static_cast<std::uint32_t>(cur);
            keys[free_].frame = frame;
            FillBoneRecord(keys[free_], rec, writePos);
            if (frame > model->maxFrame)
                model->maxFrame = frame;
            return true;
        }
    }

    // at/before the found record (0x49DBE4 path)
    if (frame == keys[cur].frame) {
        // exact frame: overwrite in place
        AppendBoneKeyToUndo(model, cur);
        FillBoneRecord(keys[cur], rec, writePos);
    } else {
        // insert before cur
        int free_ = model->searchCursor;
        if (keys[free_].frame != 0) {
            for (;;) {
                ++model->searchCursor;
                ++free_;
                if (free_ >= static_cast<int>(mdl::kBoneKeyCapacity)) {
                    OverflowBox(model, static_cast<int>(mdl::kBoneKeyCapacity));
                    return false;
                }
                if (keys[free_].frame == 0)
                    break;
            }
        }
        const int prev = static_cast<int>(keys[cur].previous);
        AppendBoneKeyToUndo(model, prev);
        AppendBoneKeyToUndo(model, cur);
        AppendBoneKeyToUndo(model, free_);
        keys[prev].next = static_cast<std::uint32_t>(free_);
        keys[free_].previous = static_cast<std::uint32_t>(prev);
        keys[cur].previous = static_cast<std::uint32_t>(free_);
        keys[free_].next = static_cast<std::uint32_t>(cur);
        keys[free_].frame = frame;
        FillBoneRecord(keys[free_], rec, writePos);
    }
    // max-frame bump uses the RAW frameOffset here (original quirk)
    if (static_cast<std::uint32_t>(frameOffset) > model->maxFrame)
        model->maxFrame = static_cast<std::uint32_t>(frameOffset);
    return true;
}

// ---- VA 0x0049E310 -----------------------------------------
// Register a left/right-reflected bone key. This is frame-edit command 422
// ("reverse paste"), not a delete routine.
bool RegisterMirroredBoneKey(mdl::ModelRecord* model, mdl::BoneClipboardRecord& rec,
                             int frameOffset) {
    mdl::BoneKey* const keys = model->boneKeys;
    const std::uint32_t frame = static_cast<std::uint32_t>(frameOffset) + rec.frame;
    const int boneIdx = FindMirroredBone(model, rec);
    if (boneIdx < 0)
        return true;

    mdl::BoneRecord& bone = model->boneTable[boneIdx];
    const mdl::BoneType btype = bone.type;
    if (btype == mdl::BoneType::FixedAxis ||
        (btype == mdl::BoneType::UnderIk &&
         (bone.flags & mdl::kBoneFlagFixedAxis) == mdl::kBoneFlagFixedAxis)) {
        float boneAxis[3];
        if (model->physicsMode == 2) {
            std::memcpy(boneAxis, bone.axis, sizeof boneAxis);
        } else {
            const mdl::BoneRecord& tail = model->boneTable[bone.tailBone];
            boneAxis[0] = tail.position[0] - bone.position[0];
            boneAxis[1] = tail.position[1] - bone.position[1];
            boneAxis[2] = tail.position[2] - bone.position[2];
            auto* d3 = &d3dx::Get();
            if (d3->vec3Normalize != nullptr)
                d3->vec3Normalize(boneAxis, boneAxis);
        }

        float* const q = rec.rotation;
        float sourceAxis[3], angle;
        auto* d3 = &d3dx::Get();
        if (d3->quatToAxisAngle == nullptr)
            return false;
        d3->quatToAxisAngle(q, sourceAxis, &angle);
        if (!_finite(q[3]))
            q[3] = 1.0f;
        float sinHalf;
        const float ww = q[3] * q[3];
        if (ww <= 1.0f) {
            sinHalf = sqrtf(1.0f - ww);
        } else {
            q[3] = 1.0f;
            sinHalf = 0.0f;
        }
        sourceAxis[0] = -sourceAxis[0];
        sinHalf = -sinHalf;
        const float dot =
            boneAxis[0] * sourceAxis[0] + boneAxis[1] * sourceAxis[1] + boneAxis[2] * sourceAxis[2];
        const float nA = sqrtf(boneAxis[0] * boneAxis[0] + boneAxis[1] * boneAxis[1] +
                               boneAxis[2] * boneAxis[2]);
        const float nB = sqrtf(sourceAxis[0] * sourceAxis[0] + sourceAxis[1] * sourceAxis[1] +
                               sourceAxis[2] * sourceAxis[2]);
        if (dot / (nA * nB) < 0.0f)
            sinHalf = -sinHalf;
        q[0] = sinHalf * boneAxis[0];
        q[1] = -sinHalf * boneAxis[1];
        q[2] = -sinHalf * boneAxis[2];
        if (d3->quatNormalize != nullptr)
            d3->quatNormalize(q, q);
    }

    int cur = boneIdx;
    if (keys[cur].frame < frame) {
        for (;;) {
            const int next = static_cast<int>(keys[cur].next);
            if (next == 0)
                break;
            cur = next;
            if (keys[cur].frame >= frame)
                break;
        }
        if (keys[cur].frame < frame) {
            int free_ = model->searchCursor;
            if (keys[free_].frame != 0) {
                for (;;) {
                    ++model->searchCursor;
                    ++free_;
                    if (free_ >= static_cast<int>(mdl::kBoneKeyCapacity)) {
                        OverflowBox(model, static_cast<int>(mdl::kBoneKeyCapacity));
                        return false;
                    }
                    if (keys[free_].frame == 0)
                        break;
                }
            }
            AppendBoneKeyToUndo(model, cur);
            AppendBoneKeyToUndo(model, free_);
            keys[cur].next = static_cast<std::uint32_t>(free_);
            keys[free_].previous = static_cast<std::uint32_t>(cur);
            keys[free_].frame = frame;
            FillMirroredBoneRecord(keys[free_], rec);
            if (frame > model->maxFrame)
                model->maxFrame = frame;
            return true;
        }
    }

    if (frame == keys[cur].frame) {
        AppendBoneKeyToUndo(model, cur);
        FillMirroredBoneRecord(keys[cur], rec);
    } else {
        int free_ = model->searchCursor;
        if (keys[free_].frame != 0) {
            for (;;) {
                ++model->searchCursor;
                ++free_;
                if (free_ >= static_cast<int>(mdl::kBoneKeyCapacity)) {
                    OverflowBox(model, static_cast<int>(mdl::kBoneKeyCapacity));
                    return false;
                }
                if (keys[free_].frame == 0)
                    break;
            }
        }
        const int prev = static_cast<int>(keys[cur].previous);
        AppendBoneKeyToUndo(model, prev);
        AppendBoneKeyToUndo(model, cur);
        AppendBoneKeyToUndo(model, free_);
        keys[prev].next = static_cast<std::uint32_t>(free_);
        keys[free_].previous = static_cast<std::uint32_t>(prev);
        keys[cur].previous = static_cast<std::uint32_t>(free_);
        keys[free_].next = static_cast<std::uint32_t>(cur);
        keys[free_].frame = frame;
        FillMirroredBoneRecord(keys[free_], rec);
    }
    if (static_cast<std::uint32_t>(frameOffset) > model->maxFrame)
        model->maxFrame = static_cast<std::uint32_t>(frameOffset);
    return true;
}

// ---- VA 0x0049F190 --------------------------------------------------------
//  - name-matched morph registrar over a parsed 0x28 record.
bool RegisterMorphKeyFromRecord(mdl::ModelRecord* model, const mdl::MorphClipboardRecord& rec,
                                int frameOffset) {
    mdl::MorphKey* const keys = model->morphKeys;
    const std::uint32_t frame = static_cast<std::uint32_t>(frameOffset) + rec.frame;

    int morphIdx = 0;
    const int morphCnt = static_cast<int>(model->morphCount);
    if (morphCnt <= 0)
        return true;
    const mikudancestudio::mdl::MorphRecord* mrec = model->morphs;
    while (std::strcmp(rec.name, mrec->name) != 0) {
        if (++morphIdx >= morphCnt)
            return true;
        ++mrec; // One native MorphRecord (x86 136 / x64 192 bytes).
    }
    if (morphIdx == -1)
        return true;

    int cur = morphIdx;
    if (keys[cur].frame < frame) {
        for (;;) {
            const int next = static_cast<int>(keys[cur].next);
            if (next == 0)
                break;
            cur = next;
            if (keys[cur].frame >= frame)
                break;
        }
        if (keys[cur].frame < frame) {
            // append
            int free_ = model->searchCursor;
            if (keys[free_].frame != 0) {
                for (;;) {
                    ++model->searchCursor;
                    ++free_;
                    if (free_ >= static_cast<int>(mdl::kMorphKeyCapacity)) {
                        OverflowBox(model, mdl::kMorphKeyCapacity);
                        return false;
                    }
                    if (keys[free_].frame == 0)
                        break;
                }
            }
            keys[cur].next = static_cast<std::uint32_t>(free_);
            keys[free_].previous = static_cast<std::uint32_t>(cur);
            keys[free_].frame = frame;
            keys[free_].value = rec.value;
            keys[free_].allocated = 1;
            if (frame > model->maxFrame)
                model->maxFrame = frame;
            return true;
        }
    }

    if (frame == keys[cur].frame) {
        keys[cur].value = rec.value;
        keys[cur].allocated = 1;
        return true;
    }
    // insert before cur
    int free_ = model->searchCursor;
    if (keys[free_].frame != 0) {
        for (;;) {
            ++model->searchCursor;
            ++free_;
            if (free_ >= static_cast<int>(mdl::kMorphKeyCapacity)) {
                OverflowBox(model, mdl::kMorphKeyCapacity);
                return false;
            }
            if (keys[free_].frame == 0)
                break;
        }
    }
    const int prev = static_cast<int>(keys[cur].previous);
    keys[prev].next = static_cast<std::uint32_t>(free_);
    keys[free_].previous = static_cast<std::uint32_t>(prev);
    keys[cur].previous = static_cast<std::uint32_t>(free_);
    keys[free_].next = static_cast<std::uint32_t>(cur);
    keys[free_].frame = frame;
    keys[free_].value = rec.value;
    keys[free_].allocated = 1;
    if (frame > model->maxFrame)
        model->maxFrame = frame;
    return true;
}

// ---- VA 0x0049F8C0 --------------------------------------------------------
//  - display/IK registrar over parsed 21-/28-byte entries.
bool RegisterDisplayKeyFromRecord(mdl::ModelRecord* model, int frameArg, unsigned char view,
                                  int cnt, const mdl::IkClipboardState* ikEntries, int cnt2,
                                  const mdl::SelectorClipboardState* selectorEntries,
                                  int frameOffset) {
    mdl::DisplayKey* const keys = model->displayKeys;
    const std::uint32_t frame = static_cast<std::uint32_t>(frameOffset + frameArg);
    // Note: x64 (complete decompilation of sub_7FF7CB4EB3C0) does not write
    // displayKeyframesPresent when registering display keys. This field selects the left-panel display/IK track,
    // toggles only on row clicks (0x7FF7CB45A2DD..0x7FF7CB45A2EA; port in
    // ui_editor_click.cpp), and controls the search range for keyframe-jump commands.

    // fill the per-IK byte array and the selector pair array of the
    // record at index (shared by all three insert paths)
    auto FillEntries = [&](int index) {
        const int ikCnt = static_cast<int>(model->ikChainCount);
        auto* const ikBase = mdl::IkStates(keys[index]);
        for (int i = 0; i < ikCnt; ++i) {
            if (cnt > 0) {
                const mdl::IkClipboardState* entry = ikEntries;
                const char* chainBoneName = model->boneTable[model->ikChains[i].boneIndex].name;
                int k = 0;
                while (std::strcmp(entry->boneName, chainBoneName) != 0) {
                    ++k;
                    ++entry;
                    if (k >= cnt)
                        break;
                }
                // The search cursor advances for strcmp, but the original
                // value load at 0x49FCA4/0x49FB7A/0x49FF58 re-indexes from
                // the unmodified a5 base (imul index, 0x15), not from the
                // already-advanced cursor.
                if (k < cnt)
                    ikBase[i] = ikEntries[k].enabled;
            }
        }
        const int selCnt = static_cast<int>(model->boneOrderCount);
        mdl::BoneReference* const pairBase = mdl::SelectorStates(keys[index]);
        const mdl::BoneOrderEntry* const relationships = model->boneOrderTable;
        for (int i = 0; i < selCnt; ++i) {
            if (cnt2 > 0) {
                const mdl::SelectorClipboardState* entry = selectorEntries;
                const char* selectorBoneName = model->boneTable[relationships[i].boneIndex].name;
                int k = 0;
                while (std::strcmp(entry->boneName, selectorBoneName) != 0) {
                    ++k;
                    ++entry;
                    if (k >= cnt2)
                        break;
                }
                if (k < cnt2) {
                    // Likewise 0x49FD4D..0x49FD7F and its two sibling paths
                    // derive the payload address from the original a7 base.
                    pairBase[i].modelIndex = selectorEntries[k].modelIndex;
                    pairBase[i].boneIndex = selectorEntries[k].boneIndex;
                }
            }
        }
    };

    int cur = 0;
    if (keys[0].frame < frame) {
        for (;;) {
            const int next = static_cast<int>(keys[cur].next);
            if (next == 0)
                break;
            cur = next;
            if (keys[cur].frame >= frame)
                break;
        }
        if (keys[cur].frame < frame) {
            // append
            int free_ = model->searchCursor;
            if (keys[free_].frame != 0) {
                for (;;) {
                    ++model->searchCursor;
                    ++free_;
                    if (free_ >= static_cast<int>(mdl::kDisplayKeyCapacity)) {
                        OverflowBox(model, mdl::kDisplayKeyCapacity);
                        return false;
                    }
                    if (keys[free_].frame == 0)
                        break;
                }
            }
            keys[cur].next = static_cast<std::uint32_t>(free_);
            keys[free_].previous = static_cast<std::uint32_t>(cur);
            keys[free_].frame = frame;
            keys[free_].visible = view;
            FillEntries(free_);
            keys[free_].allocated = 1;
            if (frame > model->maxFrame)
                model->maxFrame = frame;
            return true;
        }
    }

    if (frame == keys[cur].frame) {
        keys[cur].visible = view;
        FillEntries(cur);
        keys[cur].allocated = 1;
        return true;
    }
    // insert before cur
    int free_ = model->searchCursor;
    if (keys[free_].frame != 0) {
        for (;;) {
            ++model->searchCursor;
            ++free_;
            if (free_ >= static_cast<int>(mdl::kDisplayKeyCapacity)) {
                OverflowBox(model, mdl::kDisplayKeyCapacity);
                return false;
            }
            if (keys[free_].frame == 0)
                break;
        }
    }
    const int prev = static_cast<int>(keys[cur].previous);
    keys[prev].next = static_cast<std::uint32_t>(free_);
    keys[free_].previous = static_cast<std::uint32_t>(prev);
    keys[cur].previous = static_cast<std::uint32_t>(free_);
    keys[free_].next = static_cast<std::uint32_t>(cur);
    keys[free_].frame = frame;
    keys[free_].visible = view;
    FillEntries(free_);
    keys[free_].allocated = 1;
    if (frame > model->maxFrame)
        model->maxFrame = frame;
    return true;
}

// ---- VA 0x0049F480 --------------------------------------------------------
// RegisterDisplayKeyCurrent: register the model-wide
// display/IK/relationship state at one frame.  The record owns two arrays
// allocated by ModelInitDefaults: one byte per IK chain at +16 and one
// {dword,dword} pair per relationship at +24.
void RegisterDisplayKeyCurrent(mdl::ModelRecord* model, int frameArg) {
    mdl::DisplayKey* const keys = model->displayKeys;
    const std::uint32_t frame = static_cast<std::uint32_t>(frameArg);
    // As above: x64 (sub_7FF7CB4EAF80) does not write displayKeyframesPresent here either;
    // it is the left-panel display/IK row selection toggle (toggled on clicks, 0x7FF7CB45A2DD..EA),
    // not a flag indicating that the model has display keys.

    const auto fill = [&](int index) {
        mdl::DisplayKey& rec = keys[index];
        rec.visible = model->loadComplete;
        auto* const ik = mdl::IkStates(rec);
        mdl::IkChain* const chains = model->ikChains;
        for (std::uint32_t i = 0; i < model->ikChainCount; ++i)
            ik[i] = chains[i].enabled;
        mdl::BoneReference* const pairs = mdl::SelectorStates(rec);
        mdl::BoneOrderEntry* const relationships = model->boneOrderTable;
        for (std::uint32_t i = 0; i < model->boneOrderCount; ++i) {
            pairs[i].modelIndex = relationships[i].linkedModel;
            pairs[i].boneIndex = relationships[i].linkedBone;
        }
        rec.allocated = 1;
    };

    int current = 0;
    if (keys[0].frame < frame) {
        for (;;) {
            const int next = static_cast<int>(keys[current].next);
            if (next == 0)
                break;
            current = next;
            if (keys[current].frame >= frame)
                break;
        }
    }
    if (keys[current].frame == frame) {
        fill(current);
        return;
    }

    int freeIndex = 1;
    while (freeIndex < static_cast<int>(mdl::kDisplayKeyCapacity) && keys[freeIndex].frame != 0)
        ++freeIndex;
    if (freeIndex >= static_cast<int>(mdl::kDisplayKeyCapacity)) {
        OverflowBox(model, mdl::kDisplayKeyCapacity);
        return;
    }

    if (keys[current].frame < frame) {
        // Append after the terminal record.
        keys[current].next = static_cast<std::uint32_t>(freeIndex);
        keys[freeIndex].previous = static_cast<std::uint32_t>(current);
    } else {
        // Insert immediately before current.
        const int previous = static_cast<int>(keys[current].previous);
        keys[previous].next = static_cast<std::uint32_t>(freeIndex);
        keys[freeIndex].previous = static_cast<std::uint32_t>(previous);
        keys[current].previous = static_cast<std::uint32_t>(freeIndex);
        keys[freeIndex].next = static_cast<std::uint32_t>(current);
    }
    keys[freeIndex].frame = frame;
    fill(freeIndex);
    if (frame > model->maxFrame)
        model->maxFrame = frame;
}

// ---- VA 0x004A27F0 ----------------------------------------
// Name-based key-track frame marker (x64 sub_7FF7CB4EFED0).  See the file
// header for the resolution chain.  Walks are unsigned frame compares over
// the per-track next-linked chains, exactly like the registrar family
// above; the first branch that matches owns the walk and returns.
int MarkKeyTrackRangeByName(mdl::ModelRecord* model, std::uint32_t from, std::uint32_t to,
                            const char* name) {
    mdl::ModelRecord& record = *model;

    // 1. facial display-frame groups (0x4A2806..0x4A2855): SJIS name only
    //    (0x4A2820 inline strcmp), targetIndex roots the morph track.
    const int facialCount = record.facialFrameCount;
    if (facialCount > 0) {
        mdl::FrameGroup* const groups = model->displayFrames;
        for (int g = 0; g < facialCount; ++g) {
            if (std::strcmp(name, groups[g].name) == 0)
                return MarkTrackRange(model->morphKeys, groups[g].targetIndex, from, to);
        }
    }

    // 2. Display/IK/external parent / "disp/IK/OP" (0x4A285F..0x4A2892): display track.
    if (std::memcmp(name, kJpDispIkOp, 13) == 0 || std::strcmp(name, "disp/IK/OP") == 0) {
        return MarkTrackRange(model->displayKeys, 0, from, to);
    }

    // 3. the root bone's own name, SJIS or English (0x4A29B2..0x4A2A25) -
    //    combo 434's center-bone entry -> bone-key record 0.
    const mdl::BoneRecord* const rootBone = model->boneTable;
    if (std::strcmp(name, rootBone->name) == 0 || std::strcmp(name, rootBone->nameEn) == 0) {
        return MarkTrackRange(model->boneKeys, 0, from, to);
    }

    // 4. ﾎﾞｰﾝ01 / "bone01" (0x4A2AB8..0x4A2AEB): the bundled ダミーボーン
    //    model's combo entries -> bone-key record 0.
    if (std::memcmp(name, kJpBone01, 7) == 0 || std::strcmp(name, "bone01") == 0) {
        return MarkTrackRange(model->boneKeys, 0, from, to);
    }

    // 5. bone display-frame groups (0x4A2B88..0x4A2C25): SJIS or English
    //    name, targetIndex roots the bone track.
    const std::int32_t groupCount = static_cast<std::int32_t>(record.displayBoneCount);
    if (groupCount > 0) {
        mdl::FrameGroup* const groups = model->displayBones;
        for (std::int32_t g = 0; g < groupCount; ++g) {
            if (std::strcmp(name, groups[g].name) == 0 || std::strcmp(name, groups[g].nameEn) == 0)
                return MarkTrackRange(model->boneKeys, groups[g].targetIndex, from, to);
        }
    }
    return 0;
}

} // namespace mikudancestudio
