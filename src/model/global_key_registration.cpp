// Insert camera, light, shadow and gravity keys into their sorted tracks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

using mdl::BoneClipboardRecord;
using mdl::CameraClipboardRecord;
using mdl::DisplayClipboardRecord;
using mdl::GravityClipboardRecord;
using mdl::IkClipboardState;
using mdl::LightClipboardRecord;
using mdl::MorphClipboardRecord;
using mdl::SelectorClipboardState;
using mdl::ShadowClipboardRecord;

// 0x410AA0/0x411900/0x4120B0/0x412DF0 all insert into a 10000-record,
// frame-sorted list.  Record 0 is the list head; +4/+8 are prev/next indices,
// while an unused non-head slot is identified by frame==0.  The original
// functions duplicate this allocator/splice sequence byte for byte for each
// payload shape.
template <typename Key, typename Source, typename Fill>
static int InsertGlobalFrame(MMDApp* app, Key* table, std::uint32_t relativeFrame,
                             const Source& source, Fill fill,
                             int advertisedCapacity = mdl::kTimelineKeyCapacity) {
    if (table == nullptr) {
        return 0;
    }

    const std::uint32_t frame = relativeFrame + static_cast<std::uint32_t>(app->CurrentFrame());
    // x64 camera RVA 0x5B497 (the other three registrars have the same walk):
    // compare the head before following next. Frame 0 must overwrite slot 0;
    // a second frame-0 node would also be mistaken for a free slot below.
    std::uint32_t index = 0;
    while (table[index].frame < frame) {
        const std::uint32_t next = table[index].next;
        if (next == 0)
            break;
        index = next;
    }

    Key& current = table[index];
    if (current.frame == frame) {
        fill(current, source);
        return 1;
    }

    std::uint32_t freeIndex = 1;
    while (freeIndex < static_cast<int>(mdl::kTimelineKeyCapacity) && table[freeIndex].frame != 0) {
        ++freeIndex;
    }
    if (freeIndex >= static_cast<int>(mdl::kTimelineKeyCapacity)) {
        char message[0x100];
        if (app->state.englishUI != 0) {
            sprintf_s(message, sizeof(message),
                      "You cannot regist over %dpoint.\n"
                      "Please execute 'delete unused frame'",
                      advertisedCapacity);
            MessageBoxA(app->MainWindow(), message, "register frame", 0);
        } else {
            // 0x52B918 / 0x52B908, original Shift-JIS resources.
            static const char kJpOverflow[] = "\x93\x6f\x98\x5e\x83\x7c\x83\x43\x83\x93\x83\x67"
                                              "\x90\x94\x82\xaa%d\x8c\xc2\x82\xf0\x89\x7a\x82\xa6"
                                              "\x82\xdc\x82\xb5\x82\xbd\n\x82\xb1\x82\xea\x88\xc8"
                                              "\x8f\xe3\x82\xcc\x93\x6f\x98\x5e\x82\xcd\x8d\x73"
                                              "\x82\xa6\x82\xdc\x82\xb9\x82\xf1\n\x81\x75\xcc\xda"
                                              "\xb0\xd1\x95\xd2\x8f\x57\x81\x76\x82\xcc\x81\x75"
                                              "\x95\x73\x97\x70\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c"
                                              "\x81\x76\x82\xf0\x8e\xc0\x8d\x73\x82\xb5\x82\xc4"
                                              "\x89\xba\x82\xb3\x82\xa2";
            static const char kJpTitle[] = "\xcc\xda\xb0\xd1\x93\x6f\x98\x5e";
            sprintf_s(message, sizeof(message), kJpOverflow, advertisedCapacity);
            MessageBoxA(app->MainWindow(), message, kJpTitle, 0);
        }
        return 0;
    }

    Key& added = table[freeIndex];
    if (current.frame < frame) {
        // Append after the last record reached by the walk.
        current.next = freeIndex;
        added.previous = index;
        added.next = 0;
    } else {
        // Insert immediately before current and preserve both neighbours.
        const std::uint32_t previous = current.previous;
        table[previous].next = freeIndex;
        added.previous = previous;
        current.previous = freeIndex;
        added.next = index;
    }
    added.frame = frame;
    fill(added, source);
    if (frame > app->state.lastRegisteredFrame) {
        app->state.lastRegisteredFrame = frame;
    }
    return 1;
}

static void FillCameraFrame(mdl::CameraKey& key, const CameraClipboardRecord& source) {
    key.distance = source.distance;
    std::memcpy(key.eye, source.eye, sizeof(key.eye));
    std::memcpy(key.target, source.target, sizeof(key.target));
    std::memcpy(key.interpolation, source.interpolation, sizeof(key.interpolation));
    key.perspective = source.perspective;
    key.fov = source.fov;
    key.selected = 1;
    key.parentModel = source.parentModel;
    key.parentBone = source.parentBone;
}

static void FillLightFrame(mdl::LightKey& key, const LightClipboardRecord& source) {
    std::memcpy(key.direction, source.direction, sizeof(key.direction));
    std::memcpy(key.color, source.color, sizeof(key.color));
    key.selected = 1;
}

static void FillShadowFrame(mdl::SelfShadowKey& key, const ShadowClipboardRecord& source) {
    key.mode = source.mode;
    key.distance = source.distance;
    key.selected = 1;
}

static void FillGravityFrame(mdl::GravityKey& key, const GravityClipboardRecord& source) {
    key.acceleration = source.acceleration;
    std::memcpy(key.direction, source.direction, sizeof(key.direction));
    key.noise = source.noise;
    key.noiseEnabled = source.noiseEnabled;
    key.selected = 1;
}

int RegisterCameraKey(MMDApp* app, const void* rec,
                      int overflowAdvertised) { // VA 0x00410AA0
    const auto& source = *static_cast<const CameraClipboardRecord*>(rec);
    return InsertGlobalFrame(app, app->CameraKeys(), source.frame, source, FillCameraFrame,
                             overflowAdvertised);
}

int RegisterLightKey(MMDApp* app, const void* rec) { // VA 0x00411900
    const auto& source = *static_cast<const LightClipboardRecord*>(rec);
    return InsertGlobalFrame(app, app->LightKeys(), source.frame, source, FillLightFrame);
}

int RegisterSelfShadowKey(MMDApp* app, const void* rec) { // VA 0x004120B0
    const auto& source = *static_cast<const ShadowClipboardRecord*>(rec);
    return InsertGlobalFrame(app, app->ShadowKeys(), source.frame, source, FillShadowFrame);
}

int RegisterGravityKey(MMDApp* app, const void* rec) { // VA 0x00412DF0
    const auto& source = *static_cast<const GravityClipboardRecord*>(rec);
    return InsertGlobalFrame(app, app->GravityKeys(), source.frame, source, FillGravityFrame);
}

template <typename Key> static Key* FindGlobalFrame(Key* table, std::uint32_t frame) {
    std::uint32_t index = 0;
    for (;;) {
        Key* rec = &table[index];
        const std::uint32_t value = rec->frame;
        if (value >= frame) {
            return value == frame ? rec : nullptr;
        }
        index = rec->next;
        if (index == 0) {
            return nullptr;
        }
    }
}

// VA 0x00410560: Camera manipulation "register" button backend
//.
void RegisterCameraState(MMDApp* app, int frame) {
    CameraClipboardRecord source{};
    const std::uint32_t absolute = static_cast<std::uint32_t>(frame);
    source.frame = absolute - static_cast<std::uint32_t>(app->CurrentFrame());
    std::memcpy(source.eye, app->CameraPosition(), sizeof(source.eye));
    std::memcpy(source.target, app->CameraRotation(), sizeof(source.target));
    source.fov = static_cast<std::int32_t>(app->state.cameraFov);
    source.perspective = app->CameraPerspective();
    source.distance = app->CameraDistance();
    source.parentModel = app->CameraParentModel();
    source.parentBone = app->CameraParentBone();

    std::uint8_t defaultInterpolation[4][6]{};
    std::memset(defaultInterpolation[0], 20, 12);
    std::memset(defaultInterpolation[2], 107, 12);
    const bool resetInterpolation =
        SendMessageA(GetDlgItem(app->MainWindow(), panel::kPhysicsFrameCheckbox), BM_GETCHECK, 0,
                     0) == BST_CHECKED;
    const std::uint8_t (*interpolation)[6] = defaultInterpolation;
    mdl::CameraKey* existing = FindGlobalFrame(app->CameraKeys(), absolute);
    if (existing != nullptr && !resetInterpolation) {
        interpolation = existing->interpolation;
    }
    std::memcpy(source.interpolation, interpolation, sizeof(source.interpolation));
    // Preserve the x64 table-full text's 600000 exactly (argument to sprintf_s at 0x7FF7CB47B18D;
    // actual capacity is 10000 records; the counterpart paste registrar at 0x410AA0/x64 0x7FF7CB47B769 prints
    // 10000. Preserve the original's inconsistency as the behavioral reference.)
    RegisterCameraKey(app, &source, 600000);
}

// VA 0x00411630: Light manipulation "register" button backend
//.
void RegisterLightState(MMDApp* app, int frame) {
    LightClipboardRecord source{};
    source.frame =
        static_cast<std::uint32_t>(frame) - static_cast<std::uint32_t>(app->CurrentFrame());
    std::memcpy(source.direction, app->LightDirection(), sizeof(source.direction));
    std::memcpy(source.color, app->LightColor(), sizeof(source.color));
    RegisterLightKey(app, &source);
}

// and stays at mikudancestudio scope.
// VA 0x00412B20 (x64 twin sub_7FF7CB47DA60, confirmed on the x64 binary;
// the pre-existing "frame-table rebuild" label was wrong): the gravity
// keyframe registrar.  Walks the gravity track's next-chain (app+0x380,
// 36-byte GravityKey records, 10000 slots) to the first record with
// frame >= target: exact hit overwrites the live gravity cluster
// (magnitude / direction xyz / noise / noise-mode byte, selected = 1)
// into that record and returns; otherwise a fresh slot is taken from the
// frame == 0 holes (records 1..9999; record 1 is checked first) and
// spliced in - before the found record, or appended after the chain's
// dead end - with lastRegisteredFrame bumped to max(current, frame).
// No free slot raises the "You cannot regist over %dpoint." box
// (limit 10000, EN/JP by englishUI) and returns without registering.
// GravitySettingDlgProc registers through ApplyGravitySettingDialog at the current frame.
void RegisterGravityKeyCurrent(MMDApp* app, std::int32_t frame) { // VA 0x00412B20
    // JP overflow strings (0x52B918 / 0x52B908, SJIS byte-exact - the
    // same pair key_registrars.cpp uses for the model-track registrars)
    static const char kJpOverflow[] =
        "\x93\x6f\x98\x5e\x83\x7c\x83\x43\x83\x93\x83\x67\x90\x94\x82\xaa%d"
        "\x8c\xc2\x82\xf0\x89\x7a\x82\xa6\x82\xdc\x82\xb5\x82\xbd\n"
        "\x82\xb1\x82\xea\x88\xc8\x8f\xe3\x82\xcc\x93\x6f\x98\x5e\x82\xcd\x8d"
        "\x73\x82\xa6\x82\xdc\x82\xb5\x82\xf1\n"
        "\x81\x75\xcc\xda\xb0\xd1\x95\xd2\x8f\x57\x81\x76\x82\xcc\x81\x75\x95"
        "\x73\x97\x70\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c\x81\x76\x82\xf0\x8e\xc0"
        "\x8d\x73\x82\xb5\x82\xc4\x89\xba\x82\xb3\x82\xa2";
    static const char kJpFrameRegTitle[] = // "Register frame"
        "\xcc\xda\xb0\xd1\x93\x6f\x98\x5e";

    mdl::GravityKey* keys = app->GravityKeys();
    const std::uint32_t target = static_cast<std::uint32_t>(frame);

    // Walk the chain from record 0 for the first frame >= target; a dead
    // end leaves `current` on the chain's last record (append there).
    std::uint32_t current = 0;
    bool spliceBefore = keys[0].frame >= target;
    if (!spliceBefore) {
        std::uint32_t node = 0;
        for (;;) {
            const std::uint32_t next = keys[node].next;
            if (next == 0)
                break;
            current = next;
            node = next;
            if (keys[current].frame >= target) {
                spliceBefore = true;
                break;
            }
        }
    }

    // Exact hit: overwrite the live cluster into the record.
    if (spliceBefore && keys[current].frame == target) {
        mdl::GravityKey& key = keys[current];
        key.acceleration = app->state.gravityMagnitude;
        key.direction[0] = app->state.gravityX;
        key.direction[1] = app->state.gravityY;
        key.direction[2] = app->state.gravityZ;
        key.noise = app->state.gravityNoise;
        key.noiseEnabled = app->GravityNoiseEnabled();
        key.selected = 1;
        return;
    }

    // Fresh slot from the frame == 0 holes (record 1 first, then 2..).
    std::uint32_t freeIndex = 1;
    if (keys[1].frame != 0) {
        bool freeFound = false;
        for (std::uint32_t index = 2; index < mdl::kTimelineKeyCapacity; ++index) {
            if (keys[index].frame == 0) {
                freeIndex = index;
                freeFound = true;
                break;
            }
        }
        if (!freeFound) {
            char text[256];
            if (app->EnglishUI() != 0) {
                sprintf_s(text, 0x100,
                          "You cannot regist over %dpoint.\n"
                          "Please execute 'delete unused frame'",
                          static_cast<int>(mdl::kTimelineKeyCapacity));
                MessageBoxA(static_cast<HWND>(app->Hwnd()), text, "register frame", 0);
            } else {
                sprintf_s(text, 0x100, kJpOverflow, static_cast<int>(mdl::kTimelineKeyCapacity));
                MessageBoxA(static_cast<HWND>(app->Hwnd()), text, kJpFrameRegTitle, 0);
            }
            return;
        }
    }

    mdl::GravityKey& added = keys[freeIndex];
    if (spliceBefore) {
        const std::uint32_t previous = keys[current].previous;
        keys[previous].next = freeIndex;
        added.previous = previous;
        keys[current].previous = freeIndex;
        added.next = current;
    } else {
        keys[current].next = freeIndex;
        added.previous = current;
        // added.next keeps the zeroed hole's value, as the original does.
    }
    added.frame = target;
    added.acceleration = app->state.gravityMagnitude;
    added.direction[0] = app->state.gravityX;
    added.direction[1] = app->state.gravityY;
    added.direction[2] = app->state.gravityZ;
    added.noise = app->state.gravityNoise;
    added.noiseEnabled = app->GravityNoiseEnabled();
    added.selected = 1;
    if (target > app->state.lastRegisteredFrame)
        app->state.lastRegisteredFrame = target;
}

} // namespace mikudancestudio
