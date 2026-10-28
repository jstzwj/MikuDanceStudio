// ===========================================================================
// Accessory-frame paste/refresh helpers ("paste to difference flame",
// menu 0xFA) and the shared array constructor.
//   VA 0x00401150  ConstructArrayElements  array ctor (call
//                            a ctor over count elements)
//   VA 0x00413120  ApplyAccessoryTrack  seek an accessory's key track and push the
//                            interpolated state into the accessory object
//   VA 0x00414110  PasteAccessoryKeyRecord  paste one 0x34-byte
//                            accessory key record into the frame-sorted 60B key list
// ===========================================================================
// Accessory key lists: (app+0x384)[slot] points at 10000 records of 0x3C
// bytes - +0 frame, +4 prev, +8 next; a non-head slot with frame == 0 is
// free.  The accessory object lives at (app+0x9DD70)[slot]; its display
// state starts at +0x210.  PasteAccessoryKeyRecord mirrors
// the ported global-table inserter InsertGlobalFrame
// (command_frame_edit.cpp): walk to the first
// record with frame >= target, overwrite on exact hit, splice before it
// otherwise, or append after the last record when the walk ran off the
// end; the free-slot scan starts at index 1 and a full table raises the
// "You cannot regist over 10000point" box.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/scene_timeline_bridge.hpp"

namespace mikudancestudio {
namespace {

template <typename T> T& At(unsigned char* p, std::size_t offset) {
    return *reinterpret_cast<T*>(p + offset);
}
template <typename T> const T& At(const unsigned char* p, std::size_t offset) {
    return *reinterpret_cast<const T*>(p + offset);
}

// JP overflow strings 0x52B918 / 0x52B908, Shift-JIS byte-exact (same
// resources as the global-track registrar in command_frame_edit.cpp).
const char kJpOverflow[] = "\x93\x6f\x98\x5e\x83\x7c\x83\x43\x83\x93\x83\x67"
                           "\x90\x94\x82\xaa%d\x8c\xc2\x82\xf0\x89\x7a\x82\xa6"
                           "\x82\xdc\x82\xb5\x82\xbd\n\x82\xb1\x82\xea\x88\xc8"
                           "\x8f\xe3\x82\xcc\x93\x6f\x98\x5e\x82\xcd\x8d\x73"
                           "\x82\xa6\x82\xdc\x82\xb9\x82\xf1\n\x81\x75\xcc\xda"
                           "\xb0\xd1\x95\xd2\x8f\x57\x81\x76\x82\xcc\x81\x75"
                           "\x95\x73\x97\x70\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c"
                           "\x81\x76\x82\xf0\x8e\xc0\x8d\x73\x82\xb5\x82\xc4"
                           "\x89\xba\x82\xb3\x82\xa2";
const char kJpTitle[] = "\xcc\xda\xb0\xd1\x93\x6f\x98\x5e";

// Free-slot scan of a 10000-record accessory table starting at index 1
// (0x4143E2..0x414428 / 0x4145C9..0x414601).  Returns 0 (and raises the
// overflow box) when no frame == 0 slot remains.
std::uint32_t FindFreeAccSlot(MMDApp* app, mdl::AccessoryKey* table) {
    std::uint32_t freeIndex = 1;
    while (table[freeIndex].frame != 0) {
        ++freeIndex;
        if (freeIndex >= 10000) {
            char message[0x100];
            if (app->state.englishUI != 0) {
                sprintf_s(message, sizeof(message),
                          "You cannot regist over %dpoint.\n"
                          "Please execute 'delete unused frame'",
                          10000);
                MessageBoxA(app->state.hwnd, message, "register frame", 0);
            } else {
                sprintf_s(message, sizeof(message), kJpOverflow, 10000);
                MessageBoxA(app->state.hwnd, message, kJpTitle, 0);
            }
            return 0;
        }
    }
    return freeIndex;
}

// Payload copy of one pasted 0x34-byte source record into a 60B key
// record (0x4141.. field stores; +0x18 is the registered/selected flag).
// Source layout: +0 frame offset, +4 slot byte, then the eleven payload
// dwords at +8..+0x30.
void FillAccKeyRecord(mdl::AccessoryKey& dst, const mdl::AccessoryClipboardKey& src,
                      std::uint32_t frame) {
    dst.frame = frame;
    std::memcpy(&dst.visible, &src.visible, 4);
    dst.shadowEnabled = src.shadowEnabled;
    dst.parentModel = src.parentModel;
    dst.parentBone = src.parentBone;
    std::memcpy(dst.position, src.position, sizeof(dst.position));
    std::memcpy(dst.rotation, src.rotation, sizeof(dst.rotation));
    dst.scale = src.scale;
    dst.opacity = src.opacity;
    dst.selected = 1;
    std::memset(dst.reservedSelection, 0, sizeof(dst.reservedSelection));
}

// Unsigned frame delta as the original computes it: fild of the signed
// difference with the negative fixup +2^32 (flt_52B9F0).
float UnsignedFrameDelta(std::uint32_t a, std::uint32_t b) {
    const std::int32_t raw = static_cast<std::int32_t>(a - b);
    float f = static_cast<float>(raw);
    if (raw < 0)
        f += 4294967296.0f;
    return f;
}

} // namespace

// ---- VA 0x00401150: array constructor --------------------------------------
// Calls `ctor` over each of `count` elements of `elementSize` bytes, walking
// from the last element down to the first (the original's counting loop).
// The ctor receives the element address (original passes it in ECX; the
// ported no-op 0x4C46F0 takes it as a plain pointer argument).
//
void* ConstructArrayElements(void* block, std::uint32_t elementSize, std::uint32_t count,
                             void* ctor) {
    const auto fn = reinterpret_cast<void (*)(void*)>(ctor);
    unsigned char* p = static_cast<unsigned char*>(block);
    for (std::int32_t i = static_cast<std::int32_t>(count) - 1; i >= 0; --i) {
        fn(p + static_cast<std::size_t>(i) * elementSize);
    }
    return block;
}

// ---- VA 0x00413120: accessory track seek / object state push ---------------
void ApplyAccessoryTrack(MMDApp* app, int slot) { // VA 0x00413120
    if (app == nullptr || slot < 0 || slot >= 255)
        return;
    const std::uint32_t cur = static_cast<std::uint32_t>(app->CurrentFrame());
    mdl::AccessoryKey* keys = app->AccessoryKeys(slot);
    auto* obj = app->AccessorySlot(slot);
    // The original dereferences the slot pointer directly; callers keep the
    // null-object slots out of this path, so the plain null check below
    // matches the reachable behaviour.
    if (keys == nullptr || obj == nullptr)
        return;

    // Walk the frame-sorted chain (0x413131..0x41316B).
    std::uint32_t index = 0;
    bool terminal = false;
    if (keys[0].frame < cur) {
        for (;;) {
            const std::uint32_t next = keys[index].next;
            if (next == 0) {
                terminal = true; // last record stays current
                break;
            }
            index = next;
            if (keys[index].frame >= cur)
                break;
        }
    }

    // Verbatim state push (0x41316C..0x4131A0 / 0x413189.. terminal).
    // withFloats: the two interpolated channels (+0x34/+0x38) are copied
    // verbatim on exact hit and on the terminal record.
    const auto pushState = [&](const mdl::AccessoryKey& rec, bool withFloats) {
        obj->visible = rec.visible;
        obj->shadowEnabled = rec.shadowEnabled;
        obj->parentModel = rec.parentModel;
        obj->parentBone = rec.parentBone;
        std::memcpy(obj->rotation, rec.rotation, sizeof obj->rotation);
        std::memcpy(obj->position, rec.position, sizeof obj->position);
        if (withFloats) {
            obj->scale = rec.scale;
            obj->opacity = rec.opacity;
        }
    };

    if (terminal) {
        pushState(keys[index], true);
        SeekPortableSceneAccessory(app, slot, static_cast<int>(cur));
        return;
    }

    mdl::AccessoryKey& rec = keys[index];
    if (rec.frame != cur) {
        // Between prev and current: linear blend of the two float
        // channels, everything else verbatim from the PREVIOUS record
        // (0x4131AC..0x413224).
        const mdl::AccessoryKey& prec = keys[rec.previous];
        const float t =
            UnsignedFrameDelta(cur, prec.frame) / UnsignedFrameDelta(rec.frame, prec.frame);
        obj->scale = prec.scale + t * (rec.scale - prec.scale);
        obj->opacity = prec.opacity + t * (rec.opacity - prec.opacity);
        pushState(prec, false);
        SeekPortableSceneAccessory(app, slot, static_cast<int>(cur));
        return;
    }

    pushState(rec, true); // exact hit (0x413225..0x413297)
    SeekPortableSceneAccessory(app, slot, static_cast<int>(cur));
}

// ---- VA 0x00414110: paste one accessory key record --------------------------
// `rec` points at the 0x34-byte clipboard record; `useSelectedSlot` != 0
// targets the currently selected accessory slot (app+0x9E170) instead of
// the record's own slot byte.  Returns 0 when the table is full (the
// replay loop in the 0xFA handler stops); 1 otherwise.
//
int PasteAccessoryKeyRecord(MMDApp* app, const mdl::AccessoryClipboardKey& src,
                            int useSelectedSlot) {
    const std::uint8_t slot = useSelectedSlot != 0 ? app->SelectedAccessorySlot() : src.slot;

    // No accessory loaded: the original returns through its epilogue
    // without touching anything (0x41411E..0x414164).
    if (app->AccessorySlot(slot) == nullptr) {
        return 1;
    }

    mdl::AccessoryKey* table = app->AccessoryKeys(slot);
    const std::uint32_t frame = static_cast<std::uint32_t>(app->CurrentFrame()) + src.frameOffset;

    // Walk to the first record with frame >= target (0x41416D..0x4141AF).
    std::uint32_t index = 0;
    bool offEnd = false;
    if (table[0].frame < frame) {
        for (;;) {
            const std::uint32_t next = table[index].next;
            if (next == 0) {
                offEnd = true; // append after `index`
                break;
            }
            index = next;
            if (table[index].frame >= frame)
                break;
        }
    }

    mdl::AccessoryKey& current = table[index];

    if (!offEnd && current.frame == frame) {
        // Exact hit: overwrite in place, no frame-number store, no
        // max-frame watermark update (0x4141B4..0x414210).
        FillAccKeyRecord(current, src, frame);
        return 1;
    }

    const std::uint32_t freeIndex = FindFreeAccSlot(app, table);
    if (freeIndex == 0)
        return 0; // overflow box already shown

    mdl::AccessoryKey& added = table[freeIndex];
    if (offEnd) {
        // Append after the last record (0x4142B7..0x414321).
        current.next = freeIndex;
        added.previous = index;
    } else {
        // Splice immediately before `current` (0x41423E..0x4142A3).
        const std::uint32_t prev = current.previous;
        table[prev].next = freeIndex;
        added.previous = prev;
        current.previous = freeIndex;
        added.next = index;
    }
    FillAccKeyRecord(added, src, frame);

    if (frame > app->state.lastRegisteredFrame) {
        app->state.lastRegisteredFrame = frame;
    }
    return 1;
}

} // namespace mikudancestudio
