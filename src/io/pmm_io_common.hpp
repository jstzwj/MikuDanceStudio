// ===========================================================================
// pmm_io_common.hpp - shared record readers / strings for the PMM IO bodies
// ===========================================================================
// LoadSceneV1 (pmm_load_v1.cpp, VA 0x0045916D) and LoadSceneV2
// (pmm_load_v2.cpp, VA 0x00450000) parse the same record family through the
// same _read helper; this header holds the byte-identical halves once.  The
// two stream dialects differ in exactly two record shapes, expressed as
// template parameters (no duplicated readers):
//   * BoneKey - the v1 (".pmm 0001") stream stops at the allocated flag byte;
//     the v2 (".pmm 0002") stream appends one physics-disabled byte.
//   * CameraKey - the v1 stream stops after the 0x28-byte head (no
//     parent-model/bone dwords); v2 reads the two parent dwords between the
//     head and the interpolation table.
// Morph / light / self-shadow / gravity / accessory records and the whole
// wide UI string table are byte-identical between the dialects.
//
// Wave5-C IDA verdict (x86 original): the four SJIS texts once believed to
// differ per dialect (the chain formats' "and later", the "save data" caption, the
// "substitution allowed" accessory note) are SINGLE .rdata copies referenced by both loader
// bodies; byte-per-body duplicates live in pmm_load_v1/v2.cpp (merge
// candidates once the loader split lands).
//
// Layout locks: every field the readers touch is pinned by static_assert in
// its layout header (model.hpp / global_key_layout.hpp /
// accessory_layout.hpp), all included here; the v2 workspace locks
// (NameMapping / IndexMapping, pmm_load_v2.cpp) are unaffected.
// ===========================================================================
#pragma once

#include <cstdint>
#include <io.h>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/model.hpp"

namespace mikudancestudio::pmm_io {

// Stream dialect selector for the two parameterized readers.
enum class PmmStream {
    V1,
    V2
};

// _read thunk shape shared by both loader bodies (v1 0x4593F9-region /
// v2 0x450208-region call pattern); the result is only tested by the v1
// read-gated tail and the load shell.
inline int Rd(int fd, void* buf, unsigned int count) {
    return _read(fd, buf, count);
}

// ---- record readers (stream byte order is the original's) -----------------

// v1 body: ReadV1BoneKey (0x459DF4 dense / 0x459F18 sparse helper shape);
// v2 body: ReadPmmBoneKey (0x452C97 / 0x453087).  Only difference: the v2
// tail byte.
template <PmmStream S> inline void ReadPmmBoneKey(int fd, mdl::BoneKey& key) {
    Rd(fd, &key.frame, 4);
    Rd(fd, &key.previous, 4);
    Rd(fd, &key.next, 4);
    for (int lane = 0; lane < 4; ++lane) {
        Rd(fd, &key.interpolation[lane], 1);
        Rd(fd, &key.interpolation[lane + 4], 1);
        Rd(fd, &key.interpolation[lane + 8], 1);
        Rd(fd, &key.interpolation[lane + 12], 1);
    }
    Rd(fd, key.position, sizeof key.position);
    Rd(fd, key.rotation, sizeof key.rotation);
    Rd(fd, &key.allocated, 1);
    if constexpr (S == PmmStream::V2)
        Rd(fd, &key.physicsDisabled, 1);
}

// v1 body: ReadV1MorphKey (0x45A0D7); v2 body: ReadPmmMorphKey (0x453427).
// Byte-identical dialects.
inline void ReadPmmMorphKey(int fd, mdl::MorphKey& key) {
    Rd(fd, &key.frame, 4);
    Rd(fd, &key.previous, 4);
    Rd(fd, &key.next, 4);
    Rd(fd, &key.value, 4);
    Rd(fd, &key.allocated, 1);
}

// Camera track record.  The 0x28-byte head covers frame..target[2] (the
// first ten dwords of the 84-byte record ABI, pinned in
// global_key_layout.hpp; v1 reads it as &key.frame, v2 as &key - same
// address).  v1 (0x45AC10) stops after head + interpolation + perspective +
// fov + selected; v2 (0x454E07) reads the parent-model/bone dwords between
// the head and the interpolation table.
template <PmmStream S> inline void ReadPmmCameraKey(int fd, mdl::CameraKey& key) {
    Rd(fd, &key, 0x28);
    if constexpr (S == PmmStream::V2) {
        Rd(fd, &key.parentModel, 4);
        Rd(fd, &key.parentBone, 4);
    }
    for (int k = 0; k < 6; ++k) {
        Rd(fd, &key.interpolation[0][k], 1);
        Rd(fd, &key.interpolation[1][k], 1);
        Rd(fd, &key.interpolation[2][k], 1);
        Rd(fd, &key.interpolation[3][k], 1);
    }
    unsigned char b = 0;
    Rd(fd, &b, 1);
    key.perspective = (b == 1) ? 1 : 0;
    Rd(fd, &key.fov, 4);
    Rd(fd, &b, 1);
    key.selected = (b == 1) ? 1 : 0;
}

// Light track record, 37 stream bytes (v1 0x45B090 / v2 0x455286; identical).
inline void ReadPmmLightKey(int fd, mdl::LightKey& key) {
    Rd(fd, &key.frame, 4);
    Rd(fd, &key.previous, 4);
    Rd(fd, &key.next, 4);
    Rd(fd, &key.color[0], 4);
    Rd(fd, &key.color[1], 4);
    Rd(fd, &key.color[2], 4);
    Rd(fd, &key.direction[0], 4);
    Rd(fd, &key.direction[1], 4);
    Rd(fd, &key.direction[2], 4);
    unsigned char b = 0;
    Rd(fd, &b, 1);
    key.selected = (b == 1) ? 1 : 0;
}

// Self-shadow track record, 18 stream bytes (v1 readSelRecord 0x45D82D / v2
// readShadowRecord 0x457A4F; identical).  mode is consumed raw.
inline void ReadPmmSelfShadowKey(int fd, mdl::SelfShadowKey& key) {
    Rd(fd, &key.frame, 4);
    Rd(fd, &key.previous, 4);
    Rd(fd, &key.next, 4);
    Rd(fd, &key.mode, 1);
    Rd(fd, &key.distance, 4);
    unsigned char b = 0;
    Rd(fd, &b, 1);
    key.selected = (b == 1) ? 1 : 0;
}

// Gravity/selection track record, 34 stream bytes (v2 only, 0x45777C region).
inline void ReadPmmGravityKey(int fd, mdl::GravityKey& key) {
    Rd(fd, &key.frame, 4);
    Rd(fd, &key.previous, 4);
    Rd(fd, &key.next, 4);
    unsigned char b = 0;
    Rd(fd, &b, 1);
    key.noiseEnabled = (b == 1) ? 1 : 0;
    Rd(fd, &key.noise, 4);
    Rd(fd, &key.acceleration, 4);
    Rd(fd, &key.direction[0], 4);
    Rd(fd, &key.direction[1], 4);
    Rd(fd, &key.direction[2], 4);
    Rd(fd, &b, 1);
    key.selected = (b == 1) ? 1 : 0;
}

// Accessory track record, 55 stream bytes (v1 0x45BBF3 / v2 0x455DCA;
// identical), including the (100 - b/2)/100.0 transparency quirk byte.
inline void ReadPmmAccessoryKey(int fd, mdl::AccessoryKey& key) {
    unsigned char b = 0;
    Rd(fd, &key.frame, 4);
    Rd(fd, &key.previous, 4);
    Rd(fd, &key.next, 4);
    Rd(fd, &b, 1);
    key.visible = b & 1;
    key.opacity = static_cast<float>(100 - (b >> 1)) / 100.0f;
    Rd(fd, &key.parentModel, 4);
    Rd(fd, &key.parentBone, 4);
    Rd(fd, key.position, sizeof key.position);
    Rd(fd, key.rotation, sizeof key.rotation);
    Rd(fd, &key.scale, 4);
    Rd(fd, &b, 1);
    key.shadowEnabled = (b == 1) ? 1 : 0;
    Rd(fd, &b, 1);
    key.selected = (b == 1) ? 1 : 0;
}

// v2 skip-path consumption (a missing model still owns its stream bytes);
// the discard readers keep those sizes tied to the load readers above.
inline void DiscardPmmBoneKey(int fd) {
    mdl::BoneKey ignored{};
    ReadPmmBoneKey<PmmStream::V2>(fd, ignored);
}

inline void DiscardPmmMorphKey(int fd) {
    mdl::MorphKey ignored{};
    ReadPmmMorphKey(fd, ignored);
}

inline void DiscardPmmDisplayKey(int fd, std::int32_t ikCount, std::int32_t rigidBodyCount) {
    std::uint32_t ignoredLink = 0;
    std::uint8_t ignoredFlag = 0;
    Rd(fd, &ignoredLink, 4);
    Rd(fd, &ignoredLink, 4);
    Rd(fd, &ignoredLink, 4);
    Rd(fd, &ignoredFlag, 1);
    for (std::int32_t i = 0; i < ikCount; ++i)
        Rd(fd, &ignoredFlag, 1);
    for (std::int32_t i = 0; i < rigidBodyCount; ++i) {
        Rd(fd, &ignoredLink, 4);
        Rd(fd, &ignoredLink, 4);
    }
    Rd(fd, &ignoredFlag, 1);
}

// AVIFIL32 imports used by both loader teardowns.
extern "C" {
__declspec(dllimport) std::int32_t __stdcall AVIStreamGetFrameClose(void* pg);
__declspec(dllimport) std::int32_t __stdcall AVIStreamRelease(void* pavi);
__declspec(dllimport) std::int32_t __stdcall AVIFileRelease(void* pfile);
}

// ---- UI strings (wide .rdata mirrors; VA recorded) -----------------------
inline const wchar_t kWNashi[] = L"\x306A\x3057";  // 0x52D360
inline const wchar_t kWGround[] = L"\x5730\x9762"; // 0x52D368
inline const wchar_t kWCamLight[] = L"\xFF76\xFF92\xFF97\xFF65\x7167\x660E\xFF65"
                                    L"\xFF71\xFF78\xFF7E\xFF7B\xFF98"; // 0x52D370
inline const wchar_t kWLight[] = L"\x7167\x660E";                      // 0x52D390
inline const wchar_t kWSelfSh[] = L"\x30BB\x30EB\x30D5\x5F71";         // 0x52D398
inline const wchar_t kWGravity[] = L"\x91CD\x529B";                    // 0x52D3A4
inline const wchar_t kWAll[] = L"\x3059\x3079\x3066";                  // 0x52D3AC
inline const wchar_t kWViewAng[] = L"\x8996\x91CE\x89D2";              // 0x52D3B4
inline const wchar_t kWDist[] = L"\x8DDD\x3000\x96E2";                 // 0x52D3BC
inline const wchar_t kWZMove[] = L"\xFF5A\x79FB\x52D5";                // 0x52D3C4
inline const wchar_t kWYMove[] = L"\xFF59\x79FB\x52D5";                // 0x52D3CC
inline const wchar_t kWXMove[] = L"\xFF58\x79FB\x52D5";                // 0x52D3D4
inline const wchar_t kWRot[] = L"\x56DE\x3000\x8EE2";                  // 0x52D470
inline const wchar_t kWCamera[] = L"\x30AB\x30E1\x30E9";               // 0x52B784
inline const wchar_t kWOpenFile[] = L"\x30D5\x30A1\x30A4\x30EB\x3092"
                                    L"\x958B\x304F"; // 0x52DC84
inline const wchar_t kWUserModel[] = L"UserFile\\Model";
inline const wchar_t kWUserAcc[] = L"UserFile\\Accessory";
inline const wchar_t kWFilterModel[] = L"All Model files(*.pmd,*.pmx)\0*.pmd;*.pmx\0"; // 0x52DCE0
inline const wchar_t kWFilterAccJp[] = L"\x8AAD\x8FBC\x53EF\x80FD\x30D5\x30A1\x30A4\x30EB"
                                       L"(*.x,*.vac)\0*.x;*.vac\0"; // 0x52D948
inline const wchar_t kWFilterAccEn[] = L"accessory file(*.x,*.vac)\0*.x;*.vac\0";
inline const char kNon[] = "non"; // 0x52D38C

// ---- SJIS box texts shared verbatim by both loader bodies ----------------
inline const char kJpChainCapPhys[] =
    "\x95\x5C\x8E\xA6\xA5\x49\x4B\xA5\x8A\x4F\x90\x65\x83\x66\x81\x5B"
    "\x83\x5E\x88\xD9\x8F\xED"; // 0x52D82C
inline const char kJpCannotOpenModel[] =
    "\x83\x82\x83\x66\x83\x8B\x83\x74\x83\x40\x83\x43\x83\x8B\x93\xC7"
    "\x82\xDD\x8D\x9E\x82\xDD\x8E\xB8\x94\x73";                                          // 0x52DB64
inline const char kJpOpenCaption[] = "\x83\x74\x83\x40\x83\x43\x83\x8B\x93\xC7\x8D\x9E"; // 0x52DB80

// Main-window title format set after every successful load/save (v1 load
// tail 0x45E2A4 / v2 load tail 0x4582C2 / save tail 0x41E747).
inline const wchar_t kAppTitleFormat[] = L"MikuDanceStudio [%s]";

} // namespace mikudancestudio::pmm_io
