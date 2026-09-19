// ===========================================================================
// Gap ports: wave-context reset, physics-pose key registration and the
// standard-skeleton base-quaternion initializer
//   VA 0x004C2430 - ClearWaveContextFields
//                              wave/timeline 0x25C-context field clear
//   VA 0x004A4A50 - RegisterPhysicsPoseChain
//                              physics-pose key registrar (per-bone chain,
//                              model+14584 consecutive frames)
//   VA 0x004A5690 - RegisterPhysicsPose
//                              "register physics pose" driver (undo snapshot
//                              + standard-bone probes -> RegisterPhysicsPoseChain)
//   VA 0x004A60F0 - BuildLookAtQuaternion
//                              look-at quaternion builder (__stdcall)
//   VA 0x004A6520 - InitStandardSkeletonQuats
//                              standard-skeleton base quaternion setup
// ===========================================================================
// Scope note: the other VAs handed out with this batch were adjudicated as
// already covered and are NOT re-ported here:
//   0x4A48D0 / 0x4A48E0  inlined in src/exports/effect_api.cpp
//     (ExpGetPmdMatNum ~line 267 reads model+0x1C; ExpGetPmdMaterial
//     ~line 275 copies 17 floats from the 0x8F4-stride records with the
//     same inclusive `<=` bound).
//   0x4C2470  ported as InitTimelineAudio in src/window/ui_init.cpp:58
//     (DirectSoundCreate + SetCooperativeLevel(hwnd, DSSCL_PRIORITY) +
//     352800-byte 0x81E0 test buffer 44.1kHz/16bit/2ch, Play/Sleep(1)/
//     Stop/Release, hdc store).
//   0x4C26F0 / 0x4C2C90 ported in src/media/wave_audio.cpp (WaveFindDataChunk
//     line 138, WaveStreamFeed line 192).
//
// Model-object offsets used by the 0x4A4A50/0x4A5690/0x4A6520 family
// (literal hex; effect_api.cpp and key_registrars.cpp agree on the
// shared ones):
//   model+0x0000    HWND owner (message box parent)
//   model+0x0040    (+64)  upper-body base quaternion          (0x4A6520)
//   model+0x0050    (+80)  neck quaternion                     (0x4A6520)
//   model+0x0060..+0x00B4 (+96..180) arm-chain quats           (0x4A6520)
//   model+0x00C0    (+192) lower-body base quaternion          (0x4A6520)
//   model+0x00D0..+0x012C (+208..300) leg-chain quats          (0x4A6520)
//   model+0x0130/+0x0140 (+304/+320) shoulder mid quats        (0x4A6520)
//   model+0x21AC    (+8620) physics-pose source array ptr
//                   (308-byte/77-float stride slots, 3456 B per undo slot)
//   model+0x26EC    (+9952) bone key array base (60-byte records:
//                   frame +0, prev +4, next +8, ctrl bytes +12..27,
//                   pos +28..39, quat +40..55, mark +56, ik-mode +57)
//   model+0x26F0    (+9956) morph key array (20-byte records)
//   model+0x26F4    (+9960) master/display key array (28-byte records)
//   model+0x2700    (+9964) undo slot records, 28 bytes each:
//                   +0 type (2 = pose registration), +4 dirty dword,
//                   +12 frame, +16 bone-pose ptr (36 B/bone),
//                   +20 pose-source ptr (3456 B/slot)
//   model+0x2D84    (+11652) bone count
//   model+0x2D8C    (+11672) per-bone byte array (undo pose copy)
//   model+0x31B4    (+12724) undo ring index (wraps at 30)
//   model+0x31B8    (+12728) undo ring mirror
//   model+0x31C0    (+12720) max registered frame
//   model+0x31C4    (+12740) English-UI flag byte
//   model+0x31BC    (+12732)/(+12733) pose-registration flags
//   model+0x38FD    (+14589) openniVersion - model-spec gate (>=14 / >=15)
//   model+0x38F8    (+14584) physics-pose slot count
//   model+0x3908    (+14596) bone-key marker array, one byte per record
//                   (memset 0 over kBoneKeyCapacity bytes; x64 twin at
//                   +0x3CAC per the model+0x2790 pool doubling)
//   model+0x38E4    (+14568) computed leg length (cache, -1 = unset)
//   model+0x37CC..0x38F0 (+14284..14568) standard-bone probe positions
//                   (float triples, -999.0 = bone not found)
//
// All receivers are __thiscall(model) in the original; ported as free
// functions taking the model base pointer.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/wave_audio_context.hpp"
#include "mikudancestudio/panel_controls.hpp"

#include "../model/keyframe_common.hpp"

namespace mikudancestudio {
namespace {

using kfa::Rd32;
using kfa::RdI32;
using kfa::RdF32;
using kfa::Wr32;
using kfa::WrF32;

// D3DXQuaternionRotationAxis / D3DXQuaternionInverse are not bound in the
// shared d3dx_dyn.hpp Api (which must not be edited this session); resolve
// them locally from the same module handle.
using FnQuatInverseLocal = float*(WINAPI*)(float out[4], const float q[4]);
using FnQuatRotationAxisLocal =
    float*(WINAPI*)(float out[4], const float axis[3], float angle);

FnQuatInverseLocal LocalQuatInverse() {
    static FnQuatInverseLocal fn = reinterpret_cast<FnQuatInverseLocal>(
        GetProcAddress(d3dx::Get().module, "D3DXQuaternionInverse"));
    return fn;
}
FnQuatRotationAxisLocal LocalQuatRotationAxis() {
    static FnQuatRotationAxisLocal fn =
        reinterpret_cast<FnQuatRotationAxisLocal>(GetProcAddress(
            d3dx::Get().module, "D3DXQuaternionRotationAxis"));
    return fn;
}

// .rdata float constants (bit-exact; Hex-Rays decimal renderings do not
// round-trip, e.g. flt_52B738 = 0x40490FD8 prints as "3.141592" but the
// float nearest 3.141592 is 0x40490FD7):
constexpr float kPiFloat = 3.141592f;      // 0x40490FD8 (0x52B738)
// C++17 has no std::bit_cast; MSVC's __builtin_bit_cast is accepted in
// constant expressions, so the static_assert pins the literal above to
// the original .rdata bit pattern.
static_assert(__builtin_bit_cast(std::uint32_t, kPiFloat) == 0x40490FD8u,
              "flt_52B738 bit-exact");
constexpr float k35deg5311A4 = 0.61086512f; // 0x3F1C61A8 (0x5311A4)
constexpr float k30deg53119C = 0.52359867f; // 0x3F060A90 (0x53119C)
constexpr float kNeg0d7 = -0.69999999f;     // 0xBF333333 (0x531190)
constexpr float kNeg0d3 = -0.30000001f;     // 0xBE99999A (0x531198)
constexpr float k75f = 75.0f;               // 0x42960000 (0x531194)
constexpr float kSentinel = -999.0;         // dbl 0x5311B0 (x87 compares
                                             // float mem against the double)

// JP overflow strings 0x52B918 / 0x52B908 (SJIS byte-exact, shared with
// key_registrars.cpp's OverflowBox).
const char kJpOverflow[] =
    "\x93\x6f\x98\x5e\x83\x7c\x83\x43\x83\x93\x83\x67\x90\x94\x82\xaa%d"
    "\x8c\xc2\x82\xf0\x89\x7a\x82\xa6\x82\xdc\x82\xb5\x82\xbd\n"
    "\x82\xb1\x82\xea\x88\xc8\x8f\xe3\x82\xcc\x93\x6f\x98\x5e\x82\xcd\x8d"
    "\x73\x82\xa6\x82\xdc\x82\xb9\x82\xf1\n"
    "\x81\x75\xcc\xda\xb0\xd1\x95\xd2\x8f\x57\x81\x76\x82\xcc\x81\x75\x95"
    "\x73\x97\x70\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c\x81\x76\x82\xf0\x8e\xc0"
    "\x8d\x73\x82\xb5\x82\xc4\x89\xba\x82\xb3\x82\xa2";
const char kJpFrameRegTitle[] = "\xcc\xda\xb0\xd1\x93\x6f\x98\x5e";

// 0x4A4C22: overflow box; the limit is the bone-key capacity (the x64
// E build's sprintf arg is 0x927C0 = 600000, the x86 original's 300000).
void OverflowBoxBoneKey(unsigned char* m) {
    char text[256];
    if (mikudancestudio::mdl::Mdl(m)->physicsFlags != 0)
        sprintf_s(text, 0x100,
                  "You cannot regist over %d point\n"
                  "Please execute 'delete unused frame'",
                  static_cast<int>(mdl::kBoneKeyCapacity));
    else
        sprintf_s(text, 0x100, kJpOverflow,
                  static_cast<int>(mdl::kBoneKeyCapacity));
    // Caption 0x4A4C22: JP 0x52B908 when the JP flag is clear, "register
    // frame" when set (the JP constant was previously unused).
    MessageBoxA(*reinterpret_cast<HWND*>(m), text,
                mikudancestudio::mdl::Mdl(m)->physicsFlags != 0
                    ? "register frame"
                    : kJpFrameRegTitle,
                0);
}

// 60-byte bone-key record body write shared by all insert/overwrite paths
// of 0x4A4A50.  `p` points at the current 7-float source window inside the
// model+8620 pose array (window = p[0..6], 77-float stride per frame).
//   mode 0: position only   (quat = identity)
//   mode 1: rotation only   (pos = 0)
//   mode 2: full pose
//   other : nothing but the frame/links (original falls through the switch
//           without field writes or the mark)
void WriteBoneKeyRecord(mdl::BoneKey& rec, const float* p, int mode) {
    // 0x4A4BA9 / 0x4A4CAC / 0x4A527C
    if (mode == 0) {
        rec.position[0] = p[0]; rec.position[1] = p[1];
        rec.position[2] = p[2];
        rec.rotation[0] = rec.rotation[1] = rec.rotation[2] = 0.0f;
        rec.rotation[3] = 1.0f;
    } else if (mode == 1) {
        rec.position[0] = rec.position[1] = rec.position[2] = 0.0f;
        rec.rotation[3] = p[0]; rec.rotation[0] = p[1];
        rec.rotation[1] = p[2]; rec.rotation[2] = p[3];
    } else if (mode == 2) {
        rec.position[0] = p[0]; rec.position[1] = p[1];
        rec.position[2] = p[2];
        rec.rotation[3] = p[3]; rec.rotation[0] = p[4];
        rec.rotation[1] = p[5]; rec.rotation[2] = p[6];
    } else {
        return;
    }
    // linear interpolation control bytes {20,20,107,107} x4 (0x4A4CF1..)
    for (int g = 0; g < 4; ++g) {
        rec.interpolation[4 * g + 0] = 20;
        rec.interpolation[4 * g + 1] = 20;
        rec.interpolation[4 * g + 2] = 107;
        rec.interpolation[4 * g + 3] = 107;
    }
    rec.allocated = 1;
}

// Standard-bone SJIS name probes of 0x4A5690 (byte-exact, memcmp lengths).
const unsigned char kNameCenter[] =     // 0x531184 "センター" (9)
    {0x83, 0x5A, 0x83, 0x93, 0x83, 0x5E, 0x81, 0x5B, 0x00};
const unsigned char kNameUpper[] =      // 0x53117C "上半身" (7)
    {0x8F, 0xE3, 0x94, 0xBC, 0x90, 0x67, 0x00};
const unsigned char kNameNeck[] =      // 0x531178 "首" (3)
    {0x8E, 0xF1, 0x00};
const unsigned char kNameLArm[] =  // 0x52B81C "左腕" (5)
    {0x8D, 0xB6, 0x98, 0x72, 0x00};
const unsigned char kNameLElbow[] =     // 0x52B814 "左ひじ" (7)
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB6, 0x00};
const unsigned char kNameRArm[] =  // 0x52B804 "右腕" (5)
    {0x89, 0x45, 0x98, 0x72, 0x00};
const unsigned char kNameRElbow[] =     // 0x52B7FC "右ひじ" (7)
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB6, 0x00};
const unsigned char kNameLower[] =      // 0x531170 "下半身" (7)
    {0x89, 0xBA, 0x94, 0xBC, 0x90, 0x67, 0x00};
const unsigned char kNameLLeg[] =      // 0x531168 "左足" (5)
    {0x8D, 0xB6, 0x91, 0xAB, 0x00};
const unsigned char kNameRLeg[] =      // 0x531160 "右足" (5)
    {0x89, 0x45, 0x91, 0xAB, 0x00};
const unsigned char kNameLLegIk[] =     // 0x531154 "左足ＩＫ" (9)
    {0x8D, 0xB6, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00};
const unsigned char kNameRLegIk[] =     // 0x531148 "右足ＩＫ" (9)
    {0x89, 0x45, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00};
const unsigned char kNameLKnee[] =      // 0x530F60 "左ひざ" (7)
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB4, 0x00};
const unsigned char kNameRKnee[] =      // 0x530F58 "右ひざ" (7)
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB4, 0x00};
const unsigned char kNameLWrist[] =  // 0x52B80C "左手首" (7)
    {0x8D, 0xB6, 0x8E, 0xE8, 0x8E, 0xF1, 0x00};
const unsigned char kNameRWrist[] =  // 0x52B7F4 "右手首" (7)
    {0x89, 0x45, 0x8E, 0xE8, 0x8E, 0xF1, 0x00};
const unsigned char kNameLShoulder[] =      // 0x531140 "左肩" (5)
    {0x8D, 0xB6, 0x8C, 0xA8, 0x00};
const unsigned char kNameRShoulder[] =      // 0x531138 "右肩" (5)
    {0x89, 0x45, 0x8C, 0xA8, 0x00};

int FindBoneByName(unsigned char* m, const void* name, std::size_t cb) {
    const int cnt = mikudancestudio::mdl::Mdl(m)->boneCount;
    if (cnt <= 0) return -1;
    mikudancestudio::mdl::BoneRecord* const bones = mikudancestudio::mdl::Bones(m);
    for (int i = 0; i < cnt; ++i)
        if (std::memcmp(name, bones[i].name, cb) == 0) return i;
    return -1;  // original leaves the counter at cnt and skips the call
}

}  // namespace

// ---------------------------------------------------------------------------
// VA 0x004C2430 - ClearWaveContextFields(this): clear five fields of the 0x25C wave/
// timeline context object.  Called from the WinMain init path 0x47A5B0
// (0x47A665) right after the zeroing ctor 0x4C2450; the array pointers are
// nulled WITHOUT freeing (leak-preserving, matching the original).
// __thiscall(ctx) -> returns ctx.
// ---------------------------------------------------------------------------
void* ClearWaveContextFields(void* obj) {
    auto* audio = static_cast<WaveAudioContext*>(obj);
    audio->path[0] = L'\0';                              // 0x4C2434
    audio->englishUI = 0;                                // 0x4C2438
    audio->waveformMax = nullptr;                        // 0x4C243E
    audio->waveformMin = nullptr;                        // 0x4C2440
    audio->volume = 0;                                   // 0x4C2443
    return audio;                                        // 0x4C2449
}

// ---------------------------------------------------------------------------
// VA 0x004A4A50 - RegisterPhysicsPoseChain(model, boneIdx, srcIdx, mode,
// startFrame).
// Physics-pose key registrar: walks the per-bone sorted chain of 60-byte
// records from record `boneIdx` and registers model+14584 consecutive
// frames (startFrame, startFrame+1, ...), sourcing each frame's pose from
// the model+8620 array (77-float stride, 7-float window per frame at float
// index srcIdx + 77*k).  Returns 0 on record exhaustion (box shown), 1 on
// success.  Uses the same AppendBoneKeyToUndo selection-unlink hook as the VMD
// registrars.  Sole caller: 0x4A5690.
// ---------------------------------------------------------------------------
bool RegisterPhysicsPoseChain(unsigned char* m, int boneIdx, int srcIdx,
                              int mode, std::uint32_t startFrame) {
    mdl::BoneKey* const keys = mdl::BoneKeys(m);

    // chain walk: advance while frame < startFrame (unsigned, 0x4A4A8F..)
    int cur = boneIdx;                                            // v18
    if (keys[cur].frame < startFrame) {
        for (;;) {
            const int nxt = static_cast<int>(keys[cur].next);
            if (nxt == 0) break;                                  // 0x4A4A97
            if (keys[nxt].frame >= startFrame) break;
            cur = nxt;                                            // 0x4A4AAE
        }
    }

    int next = static_cast<int>(keys[cur].next);
    int scan = mikudancestudio::mdl::Mdl(m)->boneCount;                  // v9
    int cand = scan;                                              // v20
    if (keys[scan].frame != 0) {
        do {
            ++scan;
            if (scan >= static_cast<int>(mdl::kBoneKeyCapacity)) {  // 0x4A4AFC
                OverflowBoxBoneKey(m);                            // 0x4A4C22
                return false;
            }
        } while (keys[scan].frame != 0);
        cand = scan;                                              // 0x4A4B0E
    }

    if (mikudancestudio::mdl::Mdl(m)->matMisc == 0) return true;          // 0x4A4B22

    std::uint32_t frame = startFrame;                             // v19
    const float* p = static_cast<const float*>(
                          mikudancestudio::mdl::PoseTraceBuffer(m)) +
                     srcIdx;                                      // i = 4*srcIdx+8
    unsigned int iter = 0;                                        // v22
    for (;;) {
        AppendBoneKeyToUndo(m, cur);                                        // 0x4A4B47
        AppendBoneKeyToUndo(m, next);                                       // 0x4A4B53
        AppendBoneKeyToUndo(m, scan);                                       // 0x4A4B5B

        if (keys[cur].frame == frame) {
            // exact-frame overwrite at cur (unlink, rewrite, relink below)
            keys[cur].next = 0;
            WriteBoneKeyRecord(keys[cur], p, mode);
        } else {
            // insert path (0x4A4F00)
            if (keys[next].frame == frame) {
                // overwrite the successor record instead (0x4A4F05..)
                keys[cur].next = static_cast<std::uint32_t>(next);
                keys[next].previous = static_cast<std::uint32_t>(cur);
                WriteBoneKeyRecord(keys[next], p, mode);
                cur = next;                                       // 0x4A520A
                scan = cand;                                      // 0x4A5218
                next = static_cast<int>(keys[cur].next);
            } else {
                // fresh record at cand (0x4A5229..)
                keys[cur].next = static_cast<std::uint32_t>(cand);
                keys[cand].previous = static_cast<std::uint32_t>(cur);
                keys[cand].frame = frame;
                keys[cand].next = 0;
                WriteBoneKeyRecord(keys[cand], p, mode);
                cur = cand;                                       // 0x4A5550
                scan = cand + 1;                                  // 0x4A5554
                cand = scan;
                if (keys[scan].frame != 0) {
                    for (;;) {
                        ++scan;
                        cand = scan;                              // 0x4A5578
                        if (scan >=
                            static_cast<int>(mdl::kBoneKeyCapacity))
                            break;                   // 0x4A557C
                        if (keys[scan].frame == 0) break;
                    }
                    if (scan >= static_cast<int>(mdl::kBoneKeyCapacity)) {
                        OverflowBoxBoneKey(m);                   // 0x4A5636
                        return false;                             // 0x4A55FB
                    }
                }
            }
        }
        // LABEL_42 (0x4A5598)
        if (frame > mikudancestudio::mdl::Mdl(m)->maxFrame)
            mikudancestudio::mdl::Mdl(m)->maxFrame = frame;
        if (next != 0) {                                           // 0x4A55A6
            keys[cur].next = static_cast<std::uint32_t>(next);
            keys[next].previous = static_cast<std::uint32_t>(cur);
        }
        ++frame;                                                   // 0x4A55D6
        ++iter;
        p += 77;                                                   // i += 308
        if (iter >= static_cast<unsigned int>(
                        mikudancestudio::mdl::Mdl(m)->matMisc))
            return true;                                          // 0x4A55EE
    }
}

// ---------------------------------------------------------------------------
// VA 0x004A5690 - RegisterPhysicsPose(model, frame): the
// "register physics-driven
// pose" driver.  Clears every mark byte in the three key arrays, snapshots
// the current pose into the undo ring (slot = model+12724, 30-deep), then
// probes the standard skeleton by SJIS bone name and runs RegisterPhysicsPoseChain for
// each found chain.  On any registrar failure it returns EARLY, skipping
// the model+8620 free and the model+14584 reset (original leak/quirk).
// __thiscall(model).  Sole caller: 0x46B090.
// ---------------------------------------------------------------------------
void RegisterPhysicsPose(unsigned char* m, std::uint32_t frame) {
    mdl::ModelRecord& model = *mdl::Mdl(m);
    mdl::BoneKey* const boneKeys = mdl::BoneKeys(m);
    for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i)
        boneKeys[i].allocated = 0;
    mdl::MorphKey* const morphKeys = mdl::MorphKeys(m);
    for (int i = 0; i < 20000; ++i) morphKeys[i].allocated = 0;
    mdl::DisplayKey* const masterKeys = mdl::DisplayKeys(m);
    for (int i = 0; i < 1000; ++i) masterKeys[i].allocated = 0;

    const auto cleanup = [&]() {                                   // 0x4A60BF
        if (mikudancestudio::mdl::PoseTraceBuffer(m) != nullptr) {
            ::operator delete(mikudancestudio::mdl::PoseTraceBuffer(m));
            mikudancestudio::mdl::PoseTraceBuffer(m) = nullptr;
        }
        mdl::Mdl(m)->matMisc = 0;
    };

    if (model.matMisc == 0) {                                    // 0x4A56FC
        cleanup();
        return;
    }

    HWND hwnd = static_cast<HWND>(model.hwnd);
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), 1);                        // 0x4A5713
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), 0);                        // 0x4A572A

    if (++model.undoState[0] >= 30)
        model.undoState[0] = 0;                                   // 0x4A574D
    const int slot = model.undoState[0];
    model.undoState[1] = slot;                                    // 0x4A5762
    model.undoDirty = 1;                                          // 0x4A573D
    model.redoDirty = 0;                                          // 0x4A5744

    auto& undo = mikudancestudio::mdl::Mdl(m)->undoRings[0].slots[slot];
    undo.operation = 2;                                            // 0x4A5768
    undo.frame = frame;                                             // 0x4A5786

    const int boneCnt = model.boneCount;
    auto*& pose = undo.bonePose;
    if (pose != nullptr) {                                         // 0x4A579C
        ::operator delete(pose);
        pose = nullptr;
    }
    pose = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
        ::operator new(sizeof(*pose) * boneCnt));                    // 0x4A57F1
    std::memset(pose, 0, sizeof(*pose) * static_cast<std::size_t>(boneCnt));
    if (boneCnt > 0) {                                             // 0x4A582C
        mikudancestudio::mdl::BoneRecord* const bones = model.boneTable;
        const unsigned char* flags = model.bonePhysicsState;
        for (int b = 0; b < boneCnt; ++b) {
            auto& rec = pose[b];
            const mikudancestudio::mdl::BoneRecord* bone = &bones[b];
            rec.boneIndex = b;                                    // 0x4A5856
            std::memcpy(rec.position, bone->trans, sizeof(rec.position));
            std::memcpy(rec.rotation, bone->rotQuat, sizeof(rec.rotation));
            rec.physicsDisabled = flags[b];                        // 0x4A58F2
        }
    }
    undo.dirty = 0;                                                 // 0x4A5922

    auto*& poseSrc = undo.auxiliaryPose;
    if (poseSrc != nullptr) {                                      // 0x4A5939
        ::operator delete(poseSrc);
        poseSrc = nullptr;
    }
    poseSrc = static_cast<unsigned char*>(
        ::operator new(3456 * static_cast<std::size_t>(model.matMisc))); // 0x4A5995
    std::memset(poseSrc, 0,
                3456 * static_cast<std::size_t>(model.matMisc));   // 0x4A59C2
    std::memset(mdl::Mdl(m)->keyVisitMap, 0,
                sizeof(mdl::Mdl(m)->keyVisitMap));               // 0x4A59D5

    // standard-bone probes (order and srcIdx/mode pairs from the original)
    int i;
    i = FindBoneByName(m, kNameCenter, 9);                         // 0x4A5A00
    if (i != -1 && !RegisterPhysicsPoseChain(m, i, 0, 0, frame)) return;          // 0x4A5A2D
    i = FindBoneByName(m, kNameUpper, 7);                          // 0x4A5A53
    if (i != -1 && !RegisterPhysicsPoseChain(m, i, 3, 1, frame)) return;          // 0x4A5A80
    if (model.openniVersion >= 14) {                               // 0x4A5A8D
        i = FindBoneByName(m, kNameNeck, 3);                      // 0x4A5AB0
        if (i != -1 && !RegisterPhysicsPoseChain(m, i, 7, 1, frame)) return;      // 0x4A5ADD
    }
    i = FindBoneByName(m, kNameLArm, 5);                      // 0x4A5B03
    if (i != -1 && !RegisterPhysicsPoseChain(m, i, 11, 1, frame)) return;         // 0x4A5B30
    i = FindBoneByName(m, kNameLElbow, 7);                         // 0x4A5B60
    if (i != -1 && !RegisterPhysicsPoseChain(m, i, 15, 1, frame)) return;         // 0x4A5B8D
    i = FindBoneByName(m, kNameRArm, 5);                      // 0x4A5BB3
    if (i != -1 && !RegisterPhysicsPoseChain(m, i, 19, 1, frame)) return;         // 0x4A5BE0
    i = FindBoneByName(m, kNameRElbow, 7);                         // 0x4A5C10
    if (i != -1 && !RegisterPhysicsPoseChain(m, i, 23, 1, frame)) return;         // 0x4A5C3D
    i = FindBoneByName(m, kNameLower, 7);                          // 0x4A5C63
    if (i != -1 && !RegisterPhysicsPoseChain(m, i, 27, 1, frame)) return;         // 0x4A5C90
    i = FindBoneByName(m, kNameLLeg, 5);                          // 0x4A5CC0
    if (i != -1 && !RegisterPhysicsPoseChain(m, i, 31, 1, frame)) return;         // 0x4A5CED
    i = FindBoneByName(m, kNameRLeg, 5);                          // 0x4A5D13
    if (i != -1 && !RegisterPhysicsPoseChain(m, i, 35, 1, frame)) return;         // 0x4A5D40

    // wrist IK chains (model+9920, 24-byte structs: +0 root bone idx,
    // +18 enabled flag): enabled -> wrist bone pose, else the knee
    {
        const int ikCnt = model.ikChainCount;
        const mdl::IkChain* const chains = model.ikChains;
        mikudancestudio::mdl::BoneRecord* const bones = model.boneTable;
        int chain = -1;
        for (int c = 0; c < ikCnt; ++c) {                          // 0x4A5D80
            const mikudancestudio::mdl::BoneRecord* root = &bones[chains[c].boneIndex];
            if (std::memcmp(kNameLLegIk, root->name, 9) == 0) { chain = c; break; }
        }
        if (chain >= 0) {
            if (chains[chain].enabled != 0) {                      // 0x4A5A9D
                i = FindBoneByName(m, kNameLLegIk, 9);             // 0x4A5DD0
                if (i != -1 && !RegisterPhysicsPoseChain(m, i, 47, 2, frame)) return; // 0x4A5DF3
            } else {
                i = FindBoneByName(m, kNameLKnee, 7);              // 0x4A5E14
                if (i != -1 && !RegisterPhysicsPoseChain(m, i, 39, 1, frame)) return; // 0x4A5E3A
            }
        }
    }
    {
        const int ikCnt = model.ikChainCount;
        const mdl::IkChain* const chains = model.ikChains;
        mikudancestudio::mdl::BoneRecord* const bones = model.boneTable;
        int chain = -1;
        for (int c = 0; c < ikCnt; ++c) {                          // 0x4A5E80
            const mikudancestudio::mdl::BoneRecord* root = &bones[chains[c].boneIndex];
            if (std::memcmp(kNameRLegIk, root->name, 9) == 0) { chain = c; break; }
        }
        if (chain >= 0) {
            if (chains[chain].enabled != 0) {                      // 0x4A5E9D
                i = FindBoneByName(m, kNameRLegIk, 9);             // 0x4A5ED0
                if (i != -1 && !RegisterPhysicsPoseChain(m, i, 54, 2, frame)) return; // 0x4A5EF3
            } else {
                i = FindBoneByName(m, kNameRKnee, 7);              // 0x4A5F14
                if (i != -1 && !RegisterPhysicsPoseChain(m, i, 43, 1, frame)) return; // 0x4A5F3A
            }
        }
    }

    if (model.openniVersion >= 14) {                               // 0x4A5F4E
        i = FindBoneByName(m, kNameLWrist, 7);                  // 0x4A5F74
        if (i != -1 && !RegisterPhysicsPoseChain(m, i, 61, 1, frame)) return;     // 0x4A5FA1
        // 0x4A5FAE is a REAL re-test of the spec gate in the binary (cmp
        // byte ptr [ebp+38FDh], 0Eh / jl), not decompiler noise.  Nothing
        // in FindBoneByName/RegisterPhysicsPoseChain writes model.openniVersion, so the re-test
        // always re-enters here; kept verbatim for structural fidelity.
        if (model.openniVersion >= 14) {                           // 0x4A5FAE
            i = FindBoneByName(m, kNameRWrist, 7);              // 0x4A5FD0
            if (i != -1 && !RegisterPhysicsPoseChain(m, i, 65, 1, frame)) return; // 0x4A5FFD
        }
    }
    if (model.openniVersion >= 15) {                               // 0x4A600A
        i = FindBoneByName(m, kNameLShoulder, 5);                      // 0x4A6030
        // 0x4A6056/0x4A60B6: like every other probe, a LFoot/RFoot
        // failure is a PLAIN return in the original - no cleanup.
        if (i != -1 && !RegisterPhysicsPoseChain(m, i, 69, 1, frame))             // 0x4A6056
            return;
        // 0x4A606A: same real re-test in the binary (cmp byte ptr
        // [ebp+38FDh], 0Fh / jl); always true here, kept verbatim.
        if (model.openniVersion >= 15) {                           // 0x4A606A
            i = FindBoneByName(m, kNameRShoulder, 5);                  // 0x4A6090
            if (i != -1 && !RegisterPhysicsPoseChain(m, i, 73, 1, frame))         // 0x4A60B6
                return;
        }
    }
    cleanup();                                                     // 0x4A60BF
}

// ---------------------------------------------------------------------------
// VA 0x004A60F0 - BuildLookAtQuaternion(out, quat, ax, ay, az, bx, by, bz,
// mode):
// look-at quaternion builder.  Rotates the (b - a) direction by
// inverse(quat) * RotY(pi), normalizes it (mode 5 zeroes Y first), then
// constructs a yaw/roll rotation for the chosen axis mode (0..5) and
// returns the quaternion with its Y component negated.
// __stdcall; `out` is the return value (4 floats).  Sole caller: 0x4A6520
// (10 call sites).
// ---------------------------------------------------------------------------
float* BuildLookAtQuaternion(float out[4], const float quat[4], float ax,
                             float ay, float az, float bx, float by, float bz,
                             unsigned char mode) {
    auto* d3 = &d3dx::Get();
    FnQuatInverseLocal quatInverse = LocalQuatInverse();
    if (d3->module == nullptr || quatInverse == nullptr ||
        d3->matrixRotationQuaternion == nullptr || d3->rotY == nullptr ||
        d3->multiply == nullptr || d3->vec3Transform == nullptr ||
        d3->vec3Normalize == nullptr || d3->quatFromMatrix == nullptr)
        return out;

    float qInv[4];
    quatInverse(qInv, quat);                                       // 0x4A6109
    d3dx::D3DXMATRIXF m1;
    d3->matrixRotationQuaternion(&m1, qInv);                       // 0x4A611B
    d3dx::D3DXMATRIXF m2;
    d3->rotY(&m2, kPiFloat);                                      // 0x4A612F
    d3dx::D3DXMATRIXF m3;
    d3->multiply(&m3, &m1, &m2);                                   // 0x4A6146

    float dir[3] = {bx - ax, by - ay, bz - az};                    // 0x4A616C..
    float t[4];
    d3->vec3Transform(t, dir, &m3);                                // 0x4A61C3

    float n[3] = {t[0], mode == 5 ? 0.0f : t[1], t[2]};            // 0x4A61D9..
    d3->vec3Normalize(n, n);                                       // 0x4A6201

    const float len = sqrtf(n[2] * n[2] + n[0] * n[0]);            // 0x4A6216 (v46)
    float c;   // v40
    float s;   // v44
    float ang = 0.0f;  // v42

    if (mode == 5) {
        c = -n[2] / len;                                           // 0x4A6244
        s = n[0] / len;                                            // 0x4A624C
    } else {
        c = n[0] / len;                                            // 0x4A6266
        s = n[2] / len;                                            // 0x4A6270
    }
    if (mode == 1) {                                           // 0x4A6274
        const float a = acosf(c);                              // 0x4A6285
        ang = s < 0.0f ? -a : a;   // v15 = 0, v14 = v46 kept
    } else if (mode >= 2) {        // mode 0: v15 = 0         // 0x4A6316
        // modes 2/3/4/5: two-term angle decomposition with the x87
        // flag-sensitive sign selection (fnstsw parity + C0/C3 tests at
        // 0x4A633F..0x4A6349 map to ordered "< 0" tests; NaN falls to the
        // ">= 0" arm).
        const float ang0 = acosf(c);                               // 0x4A6323
        float r;
        if (s < 0.0f) {
            r = !(c < 0.0f) ? -acosf(-s) : acosf(-s);              // 0x4A634D/5C
            ang = -ang0 - r * len;                                 // 0x4A6385
        } else {
            r = !(c < 0.0f) ? -acosf(s) : acosf(s);                // 0x4A6395/A4
            ang = ang0 - r * len;                                  // 0x4A63C9
        }
    }
    {
        // first 2D rotation from (c, s) (0x4A62AE..)
        d3dx::D3DXMATRIXF& a = m1;  // reused v53
        std::memset(&a, 0, sizeof(a));
        a.m[0][0] = c;                                             // 0x4A62E6
        a.m[0][2] = -s;                                            // 0x4A62F2
        a.m[2][0] = s;                                             // 0x4A62F6
        a.m[2][2] = c;                                             // 0x4A62FA
        a.m[1][1] = 1.0f;                                          // 0x4A62DE
        a.m[3][3] = 1.0f;                                          // 0x4A62DA
    }
    float e1, e2;   // v41, v45
    if (mode != 0) {                                               // 0x4A62FE
        e1 = -n[1];                                                // 0x4A63DA
        e2 = len;                                                  // 0x4A63DE
    } else {
        e1 = len;                                                  // 0x4A6304
        e2 = n[1];                                                 // 0x4A630A
    }
    {
        d3dx::D3DXMATRIXF& b = m2;  // reused v54
        std::memset(&b, 0, sizeof(b));
        b.m[0][0] = e1;                                            // 0x4A643F
        b.m[0][1] = -e2;                                           // 0x4A644B
        b.m[1][0] = e2;                                            // 0x4A644F
        b.m[1][1] = e1;                                            // 0x4A6456
        b.m[2][2] = 1.0f;                                          // 0x4A6434
        b.m[3][3] = 1.0f;                                          // 0x4A642D
    }
    d3dx::D3DXMATRIXF r;   // v57
    if (mode < 4) {                                                // 0x4A645D
        d3->multiply(&m3, &m1, &m2);                               // 0x4A6477
        std::memcpy(&r, &m3, sizeof(r));                           // 0x4A648F
    } else {
        std::memcpy(&r, &m1, sizeof(r));                           // 0x4A645F
    }
    if (mode == 1 || mode == 2 || mode == 3) {                     // 0x4A6493
        d3->rotY(&m1, -ang);                                       // 0x4A64BE
        d3->multiply(&m3, &r, &m1);                                // 0x4A64D8
        std::memcpy(&r, &m3, sizeof(r));                           // 0x4A64F0
    }
    d3->quatFromMatrix(out, &r);                                   // 0x4A6502
    out[1] = -out[1];                                              // 0x4A650D
    return out;                                                    // 0x4A6512
}

// ---------------------------------------------------------------------------
// VA 0x004A6520 - InitStandardSkeletonQuats(model, flag):
// standard-skeleton base-quaternion
// initializer.  Using the standard-bone probe positions cached at
// model+14284..14568 (-999.0 = absent), builds the rest orientation
// quaternions for the upper body (model+64), neck (+80), arm chains
// (+96..+188), lower body (+192) and leg chains (+208..+300), plus the two
// shoulder mid rotations (+304/+320) and the leg length cache (+14568).
// `flag` (a2): when a probe group is absent, initialize that quaternion to
// identity instead of leaving it (some blocks do this unconditionally -
// preserved).  Original returns an undefined eax; ported as void.
// __thiscall(model).  Sole caller: 0x4B5760.
// ---------------------------------------------------------------------------
void InitStandardSkeletonQuats(unsigned char* m, unsigned char flag) {
    auto* d3 = &d3dx::Get();
    FnQuatRotationAxisLocal quatRotationAxis = LocalQuatRotationAxis();
    if (d3->module == nullptr || quatRotationAxis == nullptr ||
        d3->vec3Normalize == nullptr || d3->quatMultiply == nullptr)
        return;

    auto& state = *mdl::Mdl(m);
    auto& joints = state.currentJoints;
    auto& pose = state.standardPose;
    const auto normalizeComponents = [d3](float& x, float& y, float& z) {
        float value[3] = {x, y, z};
        d3->vec3Normalize(value, value);
        x = value[0]; y = value[1]; z = value[2];
    };
    float qByVal[4];  // by-value quaternion reads (original passes 4 dwords)
    // The arm twist blocks rotate about (0, 0, 1) - the v139/v140 register
    // trio always carries (0, 0, 1) at those call sites.
    const float kAxisZ[3] = {0.0f, 0.0f, 1.0f};

    // scratch quats (v144/v148/v132-style stack temps)
    float qA[4], qB[4], t1[4], t2[4];

    // q150/q155/q159 persist across blocks; the original leaves them as
    // uninitialized stack when producer blocks are skipped - zeroed here
    // (deterministic deviation, consumed only on data-dependent paths).
    float q150[4] = {}, q155[4] = {}, q159[4] = {};

    // ---- leg length cache (0x4A6543..0x4A6814) ---------------------------
    if (state.lightDir[0] == -1.0f) {
        if (joints[mdl::TrackedJoint::RightKnee][1] != kSentinel && joints[mdl::TrackedJoint::RightAnkle][1] != kSentinel &&
            joints[mdl::TrackedJoint::RightHip][1] != kSentinel) {
            const float d1x = joints[mdl::TrackedJoint::RightAnkle][0] - joints[mdl::TrackedJoint::RightKnee][0];
            const float d1y = joints[mdl::TrackedJoint::RightAnkle][1] - joints[mdl::TrackedJoint::RightKnee][1];
            const float d1z = joints[mdl::TrackedJoint::RightAnkle][2] - joints[mdl::TrackedJoint::RightKnee][2];
            const float d2x = joints[mdl::TrackedJoint::RightKnee][0] - joints[mdl::TrackedJoint::RightHip][0];
            const float d2y = joints[mdl::TrackedJoint::RightKnee][1] - joints[mdl::TrackedJoint::RightHip][1];
            const float d2z = joints[mdl::TrackedJoint::RightKnee][2] - joints[mdl::TrackedJoint::RightHip][2];
            const float len1 = sqrtf(d1y * d1y + d1x * d1x + d1z * d1z); // 0x4A6661
            const float len2 = sqrtf(d2x * d2x + d2y * d2y + d2z * d2z); // 0x4A6690
            state.lightDir[0] = len2 + len1;                         // 0x4A680E
        } else if (joints[mdl::TrackedJoint::RightKnee][1] != kSentinel && joints[mdl::TrackedJoint::LeftAnkle][1] != kSentinel &&
                   joints[mdl::TrackedJoint::LeftHip][1] != kSentinel) {
            const float d1x = joints[mdl::TrackedJoint::LeftAnkle][0] - joints[mdl::TrackedJoint::LeftKnee][0];
            const float d1y = joints[mdl::TrackedJoint::LeftAnkle][1] - joints[mdl::TrackedJoint::LeftKnee][1];
            const float d1z = joints[mdl::TrackedJoint::LeftAnkle][2] - joints[mdl::TrackedJoint::LeftKnee][2];
            const float d2x = joints[mdl::TrackedJoint::LeftKnee][0] - joints[mdl::TrackedJoint::LeftHip][0];
            const float d2y = joints[mdl::TrackedJoint::LeftKnee][1] - joints[mdl::TrackedJoint::LeftHip][1];
            const float d2z = joints[mdl::TrackedJoint::LeftKnee][2] - joints[mdl::TrackedJoint::LeftHip][2];
            const float lenA = sqrtf(d1y * d1y + d1x * d1x + d1z * d1z); // 0x4A67C6
            const float lenB = sqrtf(d2x * d2x + d2y * d2y + d2z * d2z); // 0x4A67F5
            state.lightDir[0] = lenB + lenA;                         // 0x4A680E
        }
    }

    // ---- head / upper-body base quat, model+64 (0x4A6827..0x4A6A5E) ------
    if (joints[mdl::TrackedJoint::Torso][1] != kSentinel && joints[mdl::TrackedJoint::RightShoulder][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftShoulder][1] != kSentinel && joints[mdl::TrackedJoint::Neck][1] != kSentinel) {
        float dx = joints[mdl::TrackedJoint::Neck][0] - joints[mdl::TrackedJoint::Torso][0];                            // 0x4A68AB
        float dy = joints[mdl::TrackedJoint::Neck][1] - joints[mdl::TrackedJoint::Torso][1];                            // 0x4A68C8
        float dz = joints[mdl::TrackedJoint::Neck][2] - joints[mdl::TrackedJoint::Torso][2];                            // 0x4A68E3
        normalizeComponents(dx, dy, dz);                               // 0x4A68EF
        float cx = dz * 1.0f - dy * 0.0f;                          // 0x4A6914
        float cy = dx * 0.0f - dz * 0.0f;                          // 0x4A6935
        float cz = dy * 0.0f - dx * 1.0f;                          // 0x4A6950
        const float dot =
            1.0f * dy + dx * 0.0f + 0.0f * dz;                     // 0x4A696C
        float axis[3] = {cx, cy, cz};
        d3->vec3Normalize(axis, axis);                             // 0x4A6970
        const float ang = acosf(dot);                              // 0x4A697E
        quatRotationAxis(qA, axis, ang);                           // 0x4A6994
        std::memcpy(qByVal, pose.upperBody, sizeof qByVal);
        BuildLookAtQuaternion(q150, qByVal, joints[mdl::TrackedJoint::LeftShoulder][0], joints[mdl::TrackedJoint::LeftShoulder][1], joints[mdl::TrackedJoint::LeftShoulder][2],
                  joints[mdl::TrackedJoint::RightShoulder][0], joints[mdl::TrackedJoint::RightShoulder][1], joints[mdl::TrackedJoint::RightShoulder][2], 4);                // 0x4A6A06
        std::memcpy(pose.upperBody, q150, 16);                           // 0x4A6A0D..
        d3->quatMultiply(t1, pose.upperBody, qA);                        // 0x4A6A30
        std::memcpy(pose.upperBody, t1, 16);                             // 0x4A6A4B..
    } else if (flag != 0) {
        pose.upperBody[3] = 1.0f;                                       // 0x4A6A60
        pose.upperBody[0] = 0.0f;                                       // 0x4A6A65
        pose.upperBody[1] = 0.0f;                                       // 0x4A6A68
        pose.upperBody[2] = 0.0f;                                       // 0x4A6A6B
    }

    // ---- neck quat, model+80 (0x4A6A7F..0x4A6C60) ------------------------
    if (joints[mdl::TrackedJoint::Center][1] != kSentinel && joints[mdl::TrackedJoint::Head][1] != kSentinel &&
        joints[mdl::TrackedJoint::Neck][1] != kSentinel) {
        std::memcpy(qByVal, pose.upperBody, sizeof qByVal);
        BuildLookAtQuaternion(t1, qByVal, joints[mdl::TrackedJoint::Head][0], joints[mdl::TrackedJoint::Head][1], joints[mdl::TrackedJoint::Head][2],
                  joints[mdl::TrackedJoint::Neck][0], joints[mdl::TrackedJoint::Neck][1], joints[mdl::TrackedJoint::Neck][2], 1);                // 0x4A6B33
        std::memcpy(pose.neck, t1, 16);                             // 0x4A6B4B..
        if (joints[mdl::TrackedJoint::HeadDirection][1] != kSentinel) {                               // 0x4A6B49
            d3->quatMultiply(qB, pose.neck, pose.upperBody);              // 0x4A6B75
            BuildLookAtQuaternion(qA, qB, joints[mdl::TrackedJoint::HeadDirection][0], joints[mdl::TrackedJoint::HeadDirection][1], joints[mdl::TrackedJoint::HeadDirection][2],
                      joints[mdl::TrackedJoint::Head][0], joints[mdl::TrackedJoint::Head][1], joints[mdl::TrackedJoint::Head][2], 5);            // 0x4A6BE4
            d3->quatMultiply(t1, qA, pose.neck);                    // 0x4A6C0F
            std::memcpy(pose.neck, t1, 16);                         // 0x4A6C2C..
        }
    } else {
        // unconditional identity (v32/v36 are constant 0/1 here)
        pose.neck[3] = 1.0f;                                       // 0x4A6C55
        pose.neck[0] = 0.0f;                                       // 0x4A6C5A
        pose.neck[1] = 0.0f;                                       // 0x4A6C5D
        pose.neck[2] = 0.0f;                                       // 0x4A6C60
    }

    // ---- shoulder mid quat #1, model+320 (0x4A6C4A..0x4A6E2C) -----------
    if (joints[mdl::TrackedJoint::LeftShoulder][1] != kSentinel && joints[mdl::TrackedJoint::RightClavicle][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightShoulder][1] != kSentinel) {
        const float mx = (joints[mdl::TrackedJoint::RightShoulder][0] + joints[mdl::TrackedJoint::LeftShoulder][0]) * 0.5f;             // 0x4A6CE9
        const float my = (joints[mdl::TrackedJoint::LeftShoulder][1] + joints[mdl::TrackedJoint::RightShoulder][1]) * 0.5f;             // 0x4A6CCF..
        const float mz = (joints[mdl::TrackedJoint::LeftShoulder][2] + joints[mdl::TrackedJoint::RightShoulder][2]) * 0.5f;             // 0x4A6CFB
        float ax = joints[mdl::TrackedJoint::RightShoulder][0] - mx, ay = joints[mdl::TrackedJoint::RightShoulder][1] - my,
              az = joints[mdl::TrackedJoint::RightShoulder][2] - mz;                                  // 0x4A6D0F..
        float bx = joints[mdl::TrackedJoint::RightClavicle][0] - mx, by = joints[mdl::TrackedJoint::RightClavicle][1] - my,
              bz = joints[mdl::TrackedJoint::RightClavicle][2] - mz;                                  // 0x4A6D5D..
        normalizeComponents(ax, ay, az);                               // 0x4A6D95
        normalizeComponents(bx, by, bz);                               // 0x4A6DA2
        float cx = bz * ay - by * az;                              // 0x4A6DC7
        float cy = bx * az - bz * ax;                              // 0x4A6DE8
        float cz = by * ax - bx * ay;                              // 0x4A6E03
        const float dot = ay * by + bx * ax + az * bz;             // 0x4A6E1F
        float axis[3] = {cx, cy, cz};
        d3->vec3Normalize(axis, axis);                             // 0x4A6E23
        quatRotationAxis(pose.rightShoulder, axis, acosf(dot));             // 0x4A6E49
    } else {
        pose.rightShoulder[3] = 1.0f;                                      // 0x4A6E64
        pose.rightShoulder[0] = 0.0f;                                      // 0x4A6E6C
        pose.rightShoulder[1] = 0.0f;                                      // 0x4A6E6F
        pose.rightShoulder[2] = 0.0f;                                      // 0x4A6E75
    }

    // ---- upper arm quat, model+144 (0x4A6E9C..0x4A7079) ------------------
    if (joints[mdl::TrackedJoint::RightShoulder][1] != kSentinel && joints[mdl::TrackedJoint::RightElbow][1] != kSentinel &&
        joints[mdl::TrackedJoint::Neck][1] != kSentinel) {
        quatRotationAxis(qA, kAxisZ, -k35deg5311A4);               // 0x4A6EFB
        if (joints[mdl::TrackedJoint::RightClavicle][1] == kSentinel) {                               // 0x4A6F11
            std::memcpy(qByVal, pose.upperBody, sizeof qByVal);
            BuildLookAtQuaternion(q150, qByVal, joints[mdl::TrackedJoint::RightShoulder][0], joints[mdl::TrackedJoint::RightShoulder][1], joints[mdl::TrackedJoint::RightShoulder][2],
                      joints[mdl::TrackedJoint::RightElbow][0], joints[mdl::TrackedJoint::RightElbow][1], joints[mdl::TrackedJoint::RightElbow][2], 0);            // 0x4A6FF2
        } else {
            d3->quatMultiply(qB, pose.rightShoulder, pose.upperBody);             // 0x4A6F1D
            BuildLookAtQuaternion(q150, qB, joints[mdl::TrackedJoint::RightClavicle][0], joints[mdl::TrackedJoint::RightClavicle][1], joints[mdl::TrackedJoint::RightClavicle][2],
                      joints[mdl::TrackedJoint::RightElbow][0], joints[mdl::TrackedJoint::RightElbow][1], joints[mdl::TrackedJoint::RightElbow][2], 0);            // 0x4A6F8A
        }
        d3->quatMultiply(pose.rightArm, qA, q150);                     // 0x4A7023
    } else if (flag != 0) {                                        // 0x4A702C
        quatRotationAxis(qA, kAxisZ, k35deg5311A4);                // 0x4A7048
        std::memcpy(pose.rightArm, qA, 16);                            // 0x4A7059..
    }

    // ---- forearm quat, model+176 (0x4A7090..0x4A7211) --------------------
    if (joints[mdl::TrackedJoint::RightElbow][1] != kSentinel && joints[mdl::TrackedJoint::RightWrist][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightShoulder][1] != kSentinel) {
        quatRotationAxis(qA, kAxisZ, -k30deg53119C);               // 0x4A70EF
        d3->quatMultiply(t1, q150, pose.rightShoulder);                     // 0x4A70FF
        d3->quatMultiply(t2, t1, pose.upperBody);                        // 0x4A7112
        BuildLookAtQuaternion(q155, t2, joints[mdl::TrackedJoint::RightElbow][0], joints[mdl::TrackedJoint::RightElbow][1], joints[mdl::TrackedJoint::RightElbow][2],
                  joints[mdl::TrackedJoint::RightWrist][0], joints[mdl::TrackedJoint::RightWrist][1], joints[mdl::TrackedJoint::RightWrist][2], 0);                // 0x4A7181
        d3->quatMultiply(pose.rightElbow, qA, q155);                     // 0x4A71C1
        quatRotationAxis(qA, kAxisZ, k30deg53119C);                // 0x4A71DA
        d3->quatMultiply(pose.rightElbow, pose.rightElbow, qA);                // 0x4A71E6
    } else if (flag != 0) {                                        // 0x4A7211
        pose.rightElbow[3] = 1.0f;                                      // 0x4A7213
        pose.rightElbow[0] = 0.0f;                                      // 0x4A721B
        pose.rightElbow[1] = 0.0f;                                      // 0x4A7221
        pose.rightElbow[2] = 0.0f;                                      // 0x4A7227
    }

    // ---- wrist quat, model+160 (0x4A7206..0x4A743E) ----------------------
    if (joints[mdl::TrackedJoint::RightWrist][1] != kSentinel && joints[mdl::TrackedJoint::RightHand][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightElbow][1] != kSentinel) {
        if (joints[mdl::TrackedJoint::RightHand][0] == joints[mdl::TrackedJoint::RightWrist][0] && joints[mdl::TrackedJoint::RightHand][1] == joints[mdl::TrackedJoint::RightWrist][1] &&
            joints[mdl::TrackedJoint::RightHand][2] == joints[mdl::TrackedJoint::RightWrist][2]) {                                // 0x4A72A8
            pose.rightWrist[3] = 1.0f;                                  // 0x4A72AA
            pose.rightWrist[0] = 0.0f;                                  // 0x4A72B2
            pose.rightWrist[1] = 0.0f;                                  // 0x4A72B8
            pose.rightWrist[2] = 0.0f;                                  // 0x4A72BE
        } else {
            quatRotationAxis(qA, kAxisZ, -k30deg53119C);           // 0x4A72E3
            d3->quatMultiply(t1, q155, q150);                      // 0x4A72FA
            d3->quatMultiply(t2, t1, pose.rightShoulder);                   // 0x4A730D
            d3->quatMultiply(qB, t2, pose.upperBody);                    // 0x4A7323
            BuildLookAtQuaternion(q159, qB, joints[mdl::TrackedJoint::RightWrist][0], joints[mdl::TrackedJoint::RightWrist][1], joints[mdl::TrackedJoint::RightWrist][2],
                      joints[mdl::TrackedJoint::RightHand][0], joints[mdl::TrackedJoint::RightHand][1], joints[mdl::TrackedJoint::RightHand][2], 0);            // 0x4A7392
            d3->quatMultiply(pose.rightWrist, qA, q159);                 // 0x4A73D2
            quatRotationAxis(qA, kAxisZ, k30deg53119C);            // 0x4A73EB
            d3->quatMultiply(pose.rightWrist, pose.rightWrist, qA);            // 0x4A73F7
        }
    } else if (flag != 0) {                                        // 0x4A7420
        pose.rightWrist[3] = 1.0f;                                      // 0x4A7422
        pose.rightWrist[0] = 0.0f;                                      // 0x4A742A
        pose.rightWrist[1] = 0.0f;                                      // 0x4A7430
        pose.rightWrist[2] = 0.0f;                                      // 0x4A7436
    }

    // ---- shoulder mid quat #2, model+304 (0x4A7415..0x4A7668) -----------
    if (joints[mdl::TrackedJoint::LeftShoulder][1] != kSentinel && joints[mdl::TrackedJoint::LeftClavicle][1] != kSentinel &&
        joints[mdl::TrackedJoint::RightShoulder][1] != kSentinel) {
        const float mx = (joints[mdl::TrackedJoint::RightShoulder][0] + joints[mdl::TrackedJoint::LeftShoulder][0]) * 0.5f;             // 0x4A7490
        const float my = (joints[mdl::TrackedJoint::LeftShoulder][1] + joints[mdl::TrackedJoint::RightShoulder][1]) * 0.5f;             // 0x4A74A0
        const float mz = (joints[mdl::TrackedJoint::LeftShoulder][2] + joints[mdl::TrackedJoint::RightShoulder][2]) * 0.5f;             // 0x4A74B0
        float ax = joints[mdl::TrackedJoint::LeftShoulder][0] - mx, ay = joints[mdl::TrackedJoint::LeftShoulder][1] - my,
              az = joints[mdl::TrackedJoint::LeftShoulder][2] - mz;                                  // 0x4A74E8..
        float bx = joints[mdl::TrackedJoint::LeftClavicle][0] - mx, by = joints[mdl::TrackedJoint::LeftClavicle][1] - my,
              bz = joints[mdl::TrackedJoint::LeftClavicle][2] - mz;                                  // 0x4A7536..
        normalizeComponents(ax, ay, az);                               // 0x4A756E
        normalizeComponents(bx, by, bz);                               // 0x4A757B
        float cx = bz * ay - by * az;                              // 0x4A75A0
        float cy = bx * az - bz * ax;                              // 0x4A75C1
        float cz = by * ax - bx * ay;                              // 0x4A75DC
        const float dot = ay * by + bx * ax + az * bz;             // 0x4A75F8
        float axis[3] = {cx, cy, cz};
        d3->vec3Normalize(axis, axis);                             // 0x4A75FC
        quatRotationAxis(pose.leftShoulder, axis, acosf(dot));             // 0x4A7622
    } else {
        pose.leftShoulder[3] = 1.0f;                                      // 0x4A765F
        pose.leftShoulder[0] = 0.0f;                                      // 0x4A7667
        pose.leftShoulder[1] = 0.0f;                                      // 0x4A766A
        pose.leftShoulder[2] = 0.0f;                                      // 0x4A7670
    }

    // ---- lower arm quat, model+96 (0x4A764E..0x4A7835) -------------------
    if (joints[mdl::TrackedJoint::LeftShoulder][1] != kSentinel && joints[mdl::TrackedJoint::LeftElbow][1] != kSentinel &&
        joints[mdl::TrackedJoint::Neck][1] != kSentinel) {
        quatRotationAxis(qA, kAxisZ, k35deg5311A4);                // 0x4A76CE
        if (joints[mdl::TrackedJoint::LeftClavicle][1] == kSentinel) {                               // 0x4A76E4
            std::memcpy(qByVal, pose.upperBody, sizeof qByVal);
            BuildLookAtQuaternion(q150, qByVal, joints[mdl::TrackedJoint::LeftElbow][0], joints[mdl::TrackedJoint::LeftElbow][1], joints[mdl::TrackedJoint::LeftElbow][2],
                      joints[mdl::TrackedJoint::LeftShoulder][0], joints[mdl::TrackedJoint::LeftShoulder][1], joints[mdl::TrackedJoint::LeftShoulder][2], 0);            // 0x4A77B7
        } else {
            d3->quatMultiply(qB, pose.leftShoulder, pose.upperBody);             // 0x4A76F0
            BuildLookAtQuaternion(q150, qB, joints[mdl::TrackedJoint::LeftElbow][0], joints[mdl::TrackedJoint::LeftElbow][1], joints[mdl::TrackedJoint::LeftElbow][2],
                      joints[mdl::TrackedJoint::LeftClavicle][0], joints[mdl::TrackedJoint::LeftClavicle][1], joints[mdl::TrackedJoint::LeftClavicle][2], 0);            // 0x4A774F
        }
        d3->quatMultiply(pose.leftArm, qA, q150);                      // 0x4A77E5
    } else if (flag != 0) {                                        // 0x4A77EE
        quatRotationAxis(qA, kAxisZ, -k35deg5311A4);               // 0x4A780A
        std::memcpy(pose.leftArm, qA, 16);                             // 0x4A781B..
    }

    // ---- lower forearm quat, model+128 (0x4A7846..0x4A79CD) --------------
    if (joints[mdl::TrackedJoint::LeftElbow][1] != kSentinel && joints[mdl::TrackedJoint::LeftWrist][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftShoulder][1] != kSentinel) {
        quatRotationAxis(qA, kAxisZ, k30deg53119C);                // 0x4A78A5
        d3->quatMultiply(t1, q150, pose.leftShoulder);                     // 0x4A78B8
        d3->quatMultiply(t2, t1, pose.upperBody);                        // 0x4A78CE
        BuildLookAtQuaternion(q155, t2, joints[mdl::TrackedJoint::LeftWrist][0], joints[mdl::TrackedJoint::LeftWrist][1], joints[mdl::TrackedJoint::LeftWrist][2],
                  joints[mdl::TrackedJoint::LeftElbow][0], joints[mdl::TrackedJoint::LeftElbow][1], joints[mdl::TrackedJoint::LeftElbow][2], 0);                // 0x4A793D
        d3->quatMultiply(pose.leftElbow, qA, q155);                     // 0x4A797D
        quatRotationAxis(qA, kAxisZ, -k30deg53119C);               // 0x4A7996
        d3->quatMultiply(pose.leftElbow, pose.leftElbow, qA);                // 0x4A79A2
    } else if (flag != 0) {                                        // 0x4A79CD
        pose.leftElbow[3] = 1.0f;                                      // 0x4A79CF
        pose.leftElbow[0] = 0.0f;                                      // 0x4A79D7
        pose.leftElbow[1] = 0.0f;                                      // 0x4A79DD
        pose.leftElbow[2] = 0.0f;                                      // 0x4A79E3
    }

    // ---- lower wrist quat, model+112 (0x4A79C2..0x4A7C21) ----------------
    if (joints[mdl::TrackedJoint::LeftWrist][1] != kSentinel && joints[mdl::TrackedJoint::LeftHand][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftElbow][1] != kSentinel) {
        if (joints[mdl::TrackedJoint::LeftHand][0] == joints[mdl::TrackedJoint::LeftWrist][0] && joints[mdl::TrackedJoint::LeftHand][1] == joints[mdl::TrackedJoint::LeftWrist][1] &&
            joints[mdl::TrackedJoint::LeftHand][2] == joints[mdl::TrackedJoint::LeftWrist][2]) {                                // 0x4A7A64
            pose.leftWrist[3] = 1.0f;                                  // 0x4A7A66
            pose.leftWrist[0] = 0.0f;                                  // 0x4A7A6B
            pose.leftWrist[1] = 0.0f;                                  // 0x4A7A6E
            pose.leftWrist[2] = 0.0f;                                  // 0x4A7A71
        } else {
            quatRotationAxis(qA, kAxisZ, k30deg53119C);            // 0x4A7A93
            d3->quatMultiply(t1, q155, q150);                      // 0x4A7AAD
            d3->quatMultiply(t2, t1, pose.leftShoulder);                   // 0x4A7AC0
            d3->quatMultiply(qB, t2, pose.upperBody);                    // 0x4A7AD3
            BuildLookAtQuaternion(q159, qB, joints[mdl::TrackedJoint::LeftHand][0], joints[mdl::TrackedJoint::LeftHand][1], joints[mdl::TrackedJoint::LeftHand][2],
                      joints[mdl::TrackedJoint::LeftWrist][0], joints[mdl::TrackedJoint::LeftWrist][1], joints[mdl::TrackedJoint::LeftWrist][2], 0);            // 0x4A7B42
            d3->quatMultiply(pose.leftWrist, qA, q159);                 // 0x4A7B7F
            quatRotationAxis(qA, kAxisZ, -k30deg53119C);           // 0x4A7B98
            d3->quatMultiply(pose.leftWrist, pose.leftWrist, qA);            // 0x4A7BA4
        }
    } else if (flag != 0) {                                        // 0x4A7C21
        pose.leftWrist[3] = 1.0f;                                      // 0x4A7C23
        pose.leftWrist[0] = 0.0f;                                      // 0x4A7C28
        pose.leftWrist[1] = 0.0f;                                      // 0x4A7C2B
        pose.leftWrist[2] = 0.0f;                                      // 0x4A7C2E
    }

    // ---- lower body base quat, model+192 (0x4A7BC1..0x4A7E7B) -----------
    const float lmx = (joints[mdl::TrackedJoint::LeftHip][0] + joints[mdl::TrackedJoint::RightHip][0]) * 0.5f;                // 0x4A7BC1
    const float lmy = (joints[mdl::TrackedJoint::LeftHip][1] + joints[mdl::TrackedJoint::RightHip][1]) * 0.5f;                // 0x4A7BD1
    const float lmz = (joints[mdl::TrackedJoint::LeftHip][2] + joints[mdl::TrackedJoint::RightHip][2]) * 0.5f;                // 0x4A7BE1
    if (joints[mdl::TrackedJoint::Torso][1] != kSentinel && joints[mdl::TrackedJoint::RightHip][1] != kSentinel &&
        joints[mdl::TrackedJoint::LeftHip][1] != kSentinel && lmy != kSentinel) {               // 0x4A7C16
        float ax = joints[mdl::TrackedJoint::Torso][0] - lmx, ay = joints[mdl::TrackedJoint::Torso][1] - lmy,
              az = joints[mdl::TrackedJoint::Torso][2] - lmz;                                 // 0x4A7CAA..
        normalizeComponents(ax, ay, az);                               // 0x4A7CE6
        float cx = az * 1.0f - ay * 0.0f;                          // 0x4A7D0B
        float cy = ax * 0.0f - az * 0.0f;                          // 0x4A7D2C
        float cz = ay * 0.0f - ax * 1.0f;                          // 0x4A7D47
        const float dot = 1.0f * ay + ax * 0.0f + 0.0f * az;       // 0x4A7D63
        float axis[3] = {cx, cy, cz};
        d3->vec3Normalize(axis, axis);                              // 0x4A7D67
        const float ang = acosf(dot);                              // 0x4A7D75
        quatRotationAxis(qA, axis, ang);                           // 0x4A7D8B
        BuildLookAtQuaternion(t1, qA, joints[mdl::TrackedJoint::LeftHip][0], joints[mdl::TrackedJoint::LeftHip][1], joints[mdl::TrackedJoint::LeftHip][2],
                  joints[mdl::TrackedJoint::RightHip][0], joints[mdl::TrackedJoint::RightHip][1], joints[mdl::TrackedJoint::RightHip][2], 4);                // 0x4A7DFD
        std::memcpy(pose.lowerBody, t1, 16);                            // 0x4A7E04..
        d3->quatMultiply(t2, pose.lowerBody, qA);                       // 0x4A7E2D
        std::memcpy(pose.lowerBody, t2, 16);                            // 0x4A7E48..
    } else if (flag != 0) {
        pose.lowerBody[3] = 1.0f;                                      // 0x4A7E61
        pose.lowerBody[0] = 0.0f;                                      // 0x4A7E69
        pose.lowerBody[1] = 0.0f;                                      // 0x4A7E6F
        pose.lowerBody[2] = 0.0f;                                      // 0x4A7E75
    }

    // ---- hip quat, model+256 (0x4A7E8A..0x4A7F7D) ------------------------
    if (joints[mdl::TrackedJoint::RightHip][1] != kSentinel && joints[mdl::TrackedJoint::RightKnee][1] != kSentinel &&
        joints[mdl::TrackedJoint::Center][1] != kSentinel) {
        std::memcpy(qByVal, pose.lowerBody, sizeof qByVal);
        BuildLookAtQuaternion(t1, qByVal, joints[mdl::TrackedJoint::RightHip][0], joints[mdl::TrackedJoint::RightHip][1], joints[mdl::TrackedJoint::RightHip][2],
                  joints[mdl::TrackedJoint::RightKnee][0], joints[mdl::TrackedJoint::RightKnee][1], joints[mdl::TrackedJoint::RightKnee][2], 2);                // 0x4A7F47
        std::memcpy(pose.rightLeg, t1, 16);                            // 0x4A7F58..
    } else if (flag != 0) {                                        // 0x4A7F7D
        pose.rightLeg[3] = 1.0f;                                      // 0x4A7F81
        pose.rightLeg[0] = 0.0f;                                      // 0x4A7F89
        pose.rightLeg[1] = 0.0f;                                      // 0x4A7F8F
        pose.rightLeg[2] = 0.0f;                                      // 0x4A7F95
    }

    // ---- other hip quat, model+208 (0x4A7FAC..0x4A809F) ------------------
    if (joints[mdl::TrackedJoint::LeftHip][1] != kSentinel && joints[mdl::TrackedJoint::LeftKnee][1] != kSentinel &&
        joints[mdl::TrackedJoint::Center][1] != kSentinel) {
        std::memcpy(qByVal, pose.lowerBody, sizeof qByVal);
        BuildLookAtQuaternion(t1, qByVal, joints[mdl::TrackedJoint::LeftHip][0], joints[mdl::TrackedJoint::LeftHip][1], joints[mdl::TrackedJoint::LeftHip][2],
                  joints[mdl::TrackedJoint::LeftKnee][0], joints[mdl::TrackedJoint::LeftKnee][1], joints[mdl::TrackedJoint::LeftKnee][2], 3);                // 0x4A8069
        std::memcpy(pose.leftLeg, t1, 16);                            // 0x4A807A..
    } else if (flag != 0) {                                        // 0x4A809F
        pose.leftLeg[3] = 1.0f;                                      // 0x4A80A3
        pose.leftLeg[0] = 0.0f;                                      // 0x4A80AB
        pose.leftLeg[1] = 0.0f;                                      // 0x4A80B1
        pose.leftLeg[2] = 0.0f;                                      // 0x4A80B7
    }

    // ---- knee quat #1, model+272 (0x4A80CE..0x4A81D1) --------------------
    if (joints[mdl::TrackedJoint::RightKnee][1] != kSentinel && joints[mdl::TrackedJoint::RightAnkle][1] != kSentinel &&
        joints[mdl::TrackedJoint::Center][1] != kSentinel) {
        d3->quatMultiply(qB, pose.rightLeg, pose.lowerBody);                // 0x4A812C
        BuildLookAtQuaternion(t1, qB, joints[mdl::TrackedJoint::RightKnee][0], joints[mdl::TrackedJoint::RightKnee][1], joints[mdl::TrackedJoint::RightKnee][2],
                  joints[mdl::TrackedJoint::RightAnkle][0], joints[mdl::TrackedJoint::RightAnkle][1], joints[mdl::TrackedJoint::RightAnkle][2], 1);                // 0x4A819B
        std::memcpy(pose.rightKnee, t1, 16);                            // 0x4A81AC..
    } else if (flag != 0) {                                        // 0x4A81D1
        pose.rightKnee[3] = 1.0f;                                      // 0x4A81D5
        pose.rightKnee[0] = 0.0f;                                      // 0x4A81DD
        pose.rightKnee[1] = 0.0f;                                      // 0x4A81E3
        pose.rightKnee[2] = 0.0f;                                      // 0x4A81E9
    }

    // ---- knee quat #2, model+224 (0x4A8200..0x4A831F) --------------------
    if (joints[mdl::TrackedJoint::LeftKnee][1] != kSentinel && joints[mdl::TrackedJoint::LeftAnkle][1] != kSentinel &&
        joints[mdl::TrackedJoint::Center][1] != kSentinel) {
        d3->quatMultiply(qB, pose.leftLeg, pose.lowerBody);                // 0x4A825E
        BuildLookAtQuaternion(t1, qB, joints[mdl::TrackedJoint::LeftKnee][0], joints[mdl::TrackedJoint::LeftKnee][1], joints[mdl::TrackedJoint::LeftKnee][2],
                  joints[mdl::TrackedJoint::LeftAnkle][0], joints[mdl::TrackedJoint::LeftAnkle][1], joints[mdl::TrackedJoint::LeftAnkle][2], 1);                // 0x4A82CD
        std::memcpy(pose.leftKnee, t1, 16);                            // 0x4A82DA..
    } else if (flag != 0) {                                        // 0x4A82FF
        pose.leftKnee[3] = 1.0f;                                      // 0x4A8303
        pose.leftKnee[0] = 0.0f;                                      // 0x4A8309
        pose.leftKnee[1] = 0.0f;                                      // 0x4A830F
        pose.leftKnee[2] = 0.0f;                                      // 0x4A8315
    }

    // ---- ankle/foot quat #1, model+288 (0x4A832E..0x4A864E) --------------
    if (joints[mdl::TrackedJoint::RightFoot][1] != kSentinel && joints[mdl::TrackedJoint::RightAnkle][1] != kSentinel) {
        float ax = joints[mdl::TrackedJoint::RightAnkle][0] - joints[mdl::TrackedJoint::RightKnee][0], ay = joints[mdl::TrackedJoint::RightAnkle][1] - joints[mdl::TrackedJoint::RightKnee][1],
              az = joints[mdl::TrackedJoint::RightAnkle][2] - joints[mdl::TrackedJoint::RightKnee][2];                            // 0x4A8365..
        normalizeComponents(ax, ay, az);                               // 0x4A83A9
        float bx = joints[mdl::TrackedJoint::RightFoot][0] - joints[mdl::TrackedJoint::RightAnkle][0], by = joints[mdl::TrackedJoint::RightFoot][1] - joints[mdl::TrackedJoint::RightAnkle][1],
              bz = joints[mdl::TrackedJoint::RightFoot][2] - joints[mdl::TrackedJoint::RightAnkle][2];                            // 0x4A83BA..
        normalizeComponents(bx, by, bz);                               // 0x4A83FE
        const float dot = by * ay + bx * ax + bz * az;             // 0x4A841F
        bool straight = false;                                     // 0x4A849B
        if (joints[mdl::TrackedJoint::RightFoot][2] - joints[mdl::TrackedJoint::RightAnkle][2] > 0.0f) {                          // 0x4A8458
            const float w = fabsf(joints[mdl::TrackedJoint::RightFoot][0] - joints[mdl::TrackedJoint::RightAnkle][0]);            // 0x4A8470
            if (w < k75f && dot > 0.5f) straight = true;           // 0x4A8490
        }
        if (dot < kNeg0d3 || straight) {                           // 0x4A849D
            d3->quatMultiply(t1, pose.rightKnee, pose.rightLeg);            // 0x4A84B7
            d3->quatMultiply(t2, t1, pose.lowerBody);                   // 0x4A84D0
            std::memcpy(pose.rightFoot, t2, 16);                        // 0x4A84E1..
        } else {
            float ex = 0.0f, ey = kNeg0d7, ez = -1.0f;             // 0x4A8508..
            normalizeComponents(ex, ey, ez);                           // 0x4A8524
            float cx = bz * ey - by * ez;                          // 0x4A8549
            float cy = bx * ez - bz * ex;                          // 0x4A856A
            float cz = by * ex - bx * ey;                          // 0x4A8585
            const float dot2 = ey * by + bx * ex + ez * bz;        // 0x4A85A1
            float axis[3] = {cx, cy, cz};
            d3->vec3Normalize(axis, axis);                         // 0x4A85A5
            quatRotationAxis(pose.rightFoot, axis, acosf(dot2));        // 0x4A85C9
        }
    } else {
        d3->quatMultiply(t1, pose.rightKnee, pose.rightLeg);                // 0x4A860E
        d3->quatMultiply(t2, t1, pose.lowerBody);                       // 0x4A8627
        std::memcpy(pose.rightFoot, t2, 16);                            // 0x4A8638..
    }

    // ---- ankle/foot quat #2, model+240 (0x4A8669..0x4A899D) --------------
    if (joints[mdl::TrackedJoint::LeftFoot][1] != kSentinel && joints[mdl::TrackedJoint::LeftAnkle][1] != kSentinel) {
        float ax = joints[mdl::TrackedJoint::LeftAnkle][0] - joints[mdl::TrackedJoint::LeftKnee][0], ay = joints[mdl::TrackedJoint::LeftAnkle][1] - joints[mdl::TrackedJoint::LeftKnee][1],
              az = joints[mdl::TrackedJoint::LeftAnkle][2] - joints[mdl::TrackedJoint::LeftKnee][2];                            // 0x4A86A0..
        normalizeComponents(ax, ay, az);                               // 0x4A86E4
        float bx = joints[mdl::TrackedJoint::LeftFoot][0] - joints[mdl::TrackedJoint::LeftAnkle][0], by = joints[mdl::TrackedJoint::LeftFoot][1] - joints[mdl::TrackedJoint::LeftAnkle][1],
              bz = joints[mdl::TrackedJoint::LeftFoot][2] - joints[mdl::TrackedJoint::LeftAnkle][2];                            // 0x4A86F5..
        normalizeComponents(bx, by, bz);                               // 0x4A8739
        const float dot = by * ay + bx * ax + bz * az;             // 0x4A875A
        bool straight = false;                                     // 0x4A87D6
        if (joints[mdl::TrackedJoint::LeftFoot][2] - joints[mdl::TrackedJoint::LeftAnkle][2] > 0.0f) {                          // 0x4A8793
            const float w = fabsf(joints[mdl::TrackedJoint::LeftFoot][0] - joints[mdl::TrackedJoint::LeftAnkle][0]);            // 0x4A87AB
            if (w < k75f && dot > 0.5f) straight = true;           // 0x4A87CB
        }
        if (dot < kNeg0d3 || straight) {                           // 0x4A87D8
            d3->quatMultiply(t1, pose.leftKnee, pose.leftLeg);            // 0x4A87F2
            d3->quatMultiply(t2, t1, pose.lowerBody);                   // 0x4A880B
            std::memcpy(pose.leftFoot, t2, 16);                        // 0x4A881C..
        } else {
            float ex = 0.0f, ey = kNeg0d7, ez = -1.0f;             // 0x4A884B..
            normalizeComponents(ex, ey, ez);                           // 0x4A8867
            float cx = bz * ey - by * ez;                          // 0x4A888C
            float cy = bx * ez - bz * ex;                          // 0x4A88AD
            float cz = by * ex - bx * ey;                          // 0x4A88C8
            const float dot2 = ey * by + bx * ex + ez * bz;        // 0x4A88E4
            float axis[3] = {cx, cy, cz};
            d3->vec3Normalize(axis, axis);                         // 0x4A88E8
            quatRotationAxis(pose.leftFoot, axis, acosf(dot2));        // 0x4A890C
        }
    } else {
        d3->quatMultiply(t1, pose.leftKnee, pose.leftLeg);                // 0x4A895C
        d3->quatMultiply(t2, t1, pose.lowerBody);                       // 0x4A8975
        std::memcpy(pose.leftFoot, t2, 16);                            // 0x4A8986..
    }
}

}  // namespace mikudancestudio
