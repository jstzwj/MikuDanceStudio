// ===========================================================================
// VA 0x0041B080 - SaveSceneFile  (original: sub_41B080, 0x3746 bytes)
// ===========================================================================
// Serialises the whole scene to the .pmm at app+0xA0900 (the caller fills
// that field from the save dialog first; the original ignores any argument
// and reads the path from the app object - it is a __thiscall member).
//
// Layout (write order is the original's, block VAs from the return-address
// landmarks left at every __write thunk call):
//   0x41B1B3  sprintf_s "Polygon Movie maker 0002" -> W(30)  [the 4 bytes
//             past the NUL are stack garbage in the original too]
//   0x41B1C4+ W4 render w/h (A08D4/A08D8), edit flag (A06C8 or A0D3C,
//             selected by the alt-dialog hwnd at A0D38), W4 fov (9E1E8),
//             W1 x6 option flags 0x2F8..0x2FD (bool), W1 slot index 0x910,
//             W1 (CB_GETCOUNT(item 0x1B4) - 1)
//   0x41B381  per existing model slot (0..99, byte = 0-based slot index):
//             name1 (len+bytes, model+0x2248), name2 (model+0x227A),
//             0x100-byte Shift-JIS path (WideToSjis of model+0x24BC),
//             W1 bone count byte (0x26D4), display-frame name table
//             (count 0x2D84, 0x25C-stride names at *0x26BC), morph name
//             table (0x2D80, 0x88 stride at *0x26C4), IK frame table
//             (0x2D88, W4 per 0x18-stride record at *0x26C0), rigid frame
//             table (0x4CCE8, W4 per 0x14-stride record at *0x4CCE4),
//             W1 0x2D7C, W1 bool 0x2D8D, W4 0x2D90, W4x5 0x2D9C..0x2DAC,
//             W1 bone-count byte again + per-bone bools (0x65-stride
//             records at *0x26D0, flag byte at +100), W4 0x31AC/0x31B0,
//             dense display-frame keys [0, count) (0x3C-stride records at
//             *0x26E0: W4 +0/+4/+8, W1 x4 (+0xC..+0x18), W4 x7
//             (+0x1C..+0x34), W1 bool +0x38/+0x39 - the dense loop counter
//             is 16-bit in the original and its record offset wraps with
//             it), then the sparse half: signed (count*0x3C <= off <
//             60*kBoneKeyCapacity bytes) scan counting frame!=0 records,
//             W4 count, then the
//             same records prefixed by W4(frame index); morph keys
//             (0x14-stride at *0x26E4, dense + sparse to 20000), rigid/IK
//             keys (0x1C-stride at *0x26E8: W4 +0/+4/+8 + W1 bool +0xC
//             base record; per record IK bool table at +0x10, float pairs
//             at +0x18, W1 bool +0x14; sparse scan 1..1000 re-emits the
//             per-record sub tables), then the current pose dump: per
//             display frame (0x25C records at *0x26BC) W4x7 +0x140..+0x158,
//             W1 bool +0x1ED, W1 bool *0x2D98[i], W1 bool *0x2D94[i]; per
//             morph W4 (+0x30 of the 0x88 records); per IK W1 bool +0x12;
//             per rigid W4 +4/+8/+0xC/+0x10 of the 0x14 records; finally
//             W1 bool 0x31BE, W4 0x31C0, W1 bool 0x37C0, W1 0x2D7D.
//   0x41C9AE  camera track (*0x374): W4x12 record 0 (+0..+0x24,+0x4C,+0x50),
//             W1 x24 (+0x28/+0x2E/+0x34/+0x3A + j, j=0..5), W1 bool +0x40,
//             W4 +0x44, W1 bool +0x48; sparse scan 1..9999 (0x54 stride)
//             each non-empty record prefixed by W4(frame index).
//   0x41CE85  camera misc W4s (0x334/0x338/0x33C/0x308/0x30C/0xA08DC/
//             0x310/0x314/0x318) + W1 bool 0x31C.
//   0x41CF51  light track (*0x378): W4 +0/+4/+8/+0x18/+0x1C/+0x20/+0xC/
//             +0x10/+0x14, W1 bool +0x24; sparse scan 1..9999 (0x28 stride).
//   0x41D207  light misc W4s (9E1A4/9E1A8/9E1AC/9E174/9E178/9E17C/9E170/
//             9DA48).
//   0x41D2AF  shadow list: W1 (u8)CB_GETCOUNT(item 0x1D7); per entry
//             CB_GETLBTEXT into the scratch buffer -> W(100).
//   0x41D310  accessory slots 0..254 (byte = slot index): W(acc+0x238,100),
//             0x100-byte SJIS path of acc+0x29C, W1 acc+0x49D, track
//             (*0x384[slot]) W4 +0/+4/+8, transparency byte
//             ((-0x1C - (int)(float(at +0x38) * 100.0)) * 2, +1 if the
//             byte at +0xC is set), W4 x9 (+0x10..+0x34 skipping +0x18),
//             W1 bool +0xD, W1 bool +0x18, sparse scan 1..9999 (0x3C
//             stride, transparency byte recomputed per record), then the
//             accessory-scale byte from acc+0x4A0 (+1 if acc+0x210),
//             W4 acc+0x230/+0x234/+0x220/+0x224/+0x228/+0x22C/+0x214/
//             +0x218/+0x21C, W1 bool acc+0x49C, W1 bool acc+0x49E.
//   0x41DB42  config: W4 0x980/0x97C/0x9E16C/0x914, W1 0x340 (raw),
//             W1 bool 0x341/0x342/0x9ED99, W4 atol(GetWindowText(item
//             0x199,8)) then W4 atol(item 0x19A,8), W1 bool 0xA06CC,
//             0x100-byte SJIS of 0xD0, if 0x9E400==0 swprintf_s(0x9E1EC,
//             0x100, L"%s") [see deviation note], W4 0x9E414/18/1C, 0x100
//             SJIS of 0x9E1EC, W4 0x91C, same quirk for 0x9E448 when
//             0x9E42C==0, W4 0x9E434/38/3C, 0x100 SJIS of 0x9E448,
//             W1 bool 0x9E428/0x31E/0x31D/0x918, W4 0xA08E0/0x9EB84/
//             0xA0B20/0xA0CF0, W1 bool 0x9ED9A, W1 0xA0CC4 (raw), W4
//             gravity 0x9EDC4/C8/B8/BC/C0, W1 bool 0xA0CD4.
//   0x41DF7C  selection/self-shadow track (*0x380): W4 +0/+4/+8, W1 bool
//             +0x20, W4 +0x1C/+0xC/+0x10/+0x14/+0x18, W1 bool +0x21;
//             sparse scan 1..9999 (0x24 stride).
//   0x41E25E  W1 bool 0xA0188, W4 0xA0D2C.
//   0x41E284  self-shadow track (*0x37C): W4 +0/+4/+8, W1 raw +0xC, W4
//             +0x10, W1 bool +0x14; sparse scan 1..9999 (0x18 stride).
//   0x41E46F  config2: W4 0xA0198/0xA019C/0xA01A0, W1 bool 0xA0194, W4
//             0xA0430..0xA0474 (18 dwords), W1 bool 0x9ED98/0xA0478/
//             0xA0197, W4 max(0, atol(GetWindowText(GetDlgItem(alt-or-
//             main, 0x22A), 10))), W1 constant 1.
//   0x41E70D  per existing model slot: W1 slot index, W4 model+0x4CCF0.
//   0x41E747  success tail: _close, swprintf_s title "MikuMikuDance [%s]"
//             with app+0xA0900, SetWindowTextW, MessageBeep(0x40),
//             app+0xA442C = 1.
// Error paths: path[0]=='\\' -> MessageBoxA JP/EN "cannot open save file"
// caption "save"; _wsopen_s errno -> sprintf_s JP/EN "%d" message with
// JP/EN caption.  0xA0B4C (EnglishUI) selects the branch; the JP texts are
// embedded here as raw Shift-JIS bytes from .rdata 0x52BCE4/0x52BD64/
// 0x52BD80.
//
// Undefined-byte compatibility boundaries (see docs/X64_RECONSTRUCTION.md):
//   * swprintf_s(buf, 0x100, L"%s") is called with NO vararg in the
//     original (0x41DCC1/0x41DD7B - the CRT then reads a stack-garbage
//     pointer).  We pass buf itself so the call stays a no-op on the
//     already-valid string; the original's garbage cannot be reproduced.
//   * The 4 stack-garbage bytes after the header NUL and the garbage tails
//     of the 0x100-byte path writes are unspecified stack contents in the
//     original as well.  Our scratch buffer is zero-initialized once at
//     declaration (see below), so our tails are deterministic zeros on the
//     first conversion and previous-path residue afterwards - the
//     original's own garbage values are unreproducible and not attempted.
//   * Saving requires the keyframe tracks allocated during model loading.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <io.h>

#include "mikudancestudio/charset_conv.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

// Shared PMM IO constants (window-title format; the loader record readers
// live there too).
#include "pmm_io_common.hpp"

namespace mikudancestudio {

namespace {

// fcn.005089ba thunk shape: _write(fd, buf, count), cdecl, result ignored.
inline void W(int fd, const void* buf, unsigned int count) {
    _write(fd, buf, static_cast<unsigned int>(count));
}

void WritePmmBoneKey(int fd, const mdl::BoneKey& key) {
    W(fd, &key.frame, 4); W(fd, &key.previous, 4); W(fd, &key.next, 4);
    for (int lane = 0; lane < 4; ++lane) {
        W(fd, &key.interpolation[lane], 1);
        W(fd, &key.interpolation[lane + 4], 1);
        W(fd, &key.interpolation[lane + 8], 1);
        W(fd, &key.interpolation[lane + 12], 1);
    }
    W(fd, key.position, sizeof key.position);
    W(fd, key.rotation, sizeof key.rotation);
    const unsigned char allocated = key.allocated != 0;
    const unsigned char physicsDisabled = key.physicsDisabled != 0;
    W(fd, &allocated, 1); W(fd, &physicsDisabled, 1);
}

void WritePmmMorphKey(int fd, const mdl::MorphKey& key) {
    W(fd, &key.frame, 4); W(fd, &key.previous, 4); W(fd, &key.next, 4);
    W(fd, &key.value, 4);
    const unsigned char allocated = key.allocated != 0;
    W(fd, &allocated, 1);
}

void WritePmmDisplayKey(int fd, const mdl::DisplayKey& key,
                        int ikCount, int rigidCount) {
    W(fd, &key.frame, 4); W(fd, &key.previous, 4); W(fd, &key.next, 4);
    const unsigned char visible = key.visible != 0;
    W(fd, &visible, 1);
    const auto* flags = mdl::IkStates(key);
    for (int i = 0; i < ikCount; ++i) {
        const unsigned char enabled = flags[i] != 0;
        W(fd, &enabled, 1);
    }
    const auto* pairs = mdl::SelectorStates(key);
    for (int i = 0; i < rigidCount; ++i)
        W(fd, &pairs[i], sizeof(pairs[i]));
    const unsigned char allocated = key.allocated != 0;
    W(fd, &allocated, 1);
}

// strlen walked to the NUL as the original does, then truncated to the
// byte that the length slot holds (asm 0x41B3A8: mov [Buf], al / movzx).
inline unsigned char NameLen(const unsigned char* s) {
    const unsigned char* p = s;
    while (*p != 0) ++p;
    return static_cast<unsigned char>(p - s);
}

// Transparency/scale byte idiom (0x41D44E etc.): FPU control word | 0xC00
// (truncate), fistp of (float * 100.0 double), then
// byte = (char)(2 * (-0x1C - v)), +1 when the paired flag byte is set.
inline unsigned char ScaleQuirkByte(float scale01, unsigned char additive) {
    const int v = static_cast<int>(static_cast<double>(scale01) * 100.0);
    unsigned char b = static_cast<unsigned char>((-0x1C - v) * 2);
    if (additive != 0) b = static_cast<unsigned char>(b + 1);
    return b;
}

void WritePmmAccessoryKey(int fd, const mdl::AccessoryKey& key) {
    W(fd, &key.frame, 4);
    W(fd, &key.previous, 4);
    W(fd, &key.next, 4);
    const unsigned char visibleAndOpacity =
        ScaleQuirkByte(key.opacity, key.visible);
    W(fd, &visibleAndOpacity, 1);
    W(fd, &key.parentModel, 4);
    W(fd, &key.parentBone, 4);
    W(fd, key.position, sizeof key.position);
    W(fd, key.rotation, sizeof key.rotation);
    W(fd, &key.scale, 4);
    const unsigned char shadowEnabled = key.shadowEnabled != 0;
    const unsigned char selected = key.selected != 0;
    W(fd, &shadowEnabled, 1);
    W(fd, &selected, 1);
}

// ---- typed writers for the four global tracks -----------------------------
// Field order and widths are frozen against the original's raw-offset
// writes; each write below carries the old track+offset form it replaces,
// and the offsets are pinned by static_assert in global_key_layout.hpp.
// The byte streams match the loader readers in pmm_io_common.hpp exactly.

void WritePmmCameraKey(int fd, const mdl::CameraKey& key) {
    W(fd, &key.frame, 4);            // was W(fd, cam + 0x00, 4)
    W(fd, &key.previous, 4);         // was W(fd, cam + 0x04, 4)
    W(fd, &key.next, 4);             // was W(fd, cam + 0x08, 4)
    W(fd, &key.distance, 4);         // was W(fd, cam + 0x0C, 4)
    W(fd, &key.eye[0], 4);           // was W(fd, cam + 0x10, 4)
    W(fd, &key.eye[1], 4);           // was W(fd, cam + 0x14, 4)
    W(fd, &key.eye[2], 4);           // was W(fd, cam + 0x18, 4)
    W(fd, &key.target[0], 4);        // was W(fd, cam + 0x1C, 4)
    W(fd, &key.target[1], 4);        // was W(fd, cam + 0x20, 4)
    W(fd, &key.target[2], 4);        // was W(fd, cam + 0x24, 4)
    W(fd, &key.parentModel, 4);      // was W(fd, cam + 0x4C, 4)
    W(fd, &key.parentBone, 4);       // was W(fd, cam + 0x50, 4)
    for (int j = 0; j < 6; ++j) {    // was W(fd, cam + 0x28/0x2E/0x34/0x3A + j, 1)
        W(fd, &key.interpolation[0][j], 1);
        W(fd, &key.interpolation[1][j], 1);
        W(fd, &key.interpolation[2][j], 1);
        W(fd, &key.interpolation[3][j], 1);
    }
    {
        const unsigned char b = key.perspective != 0;    // was cam[0x40] != 0
        W(fd, &b, 1);
    }
    W(fd, &key.fov, 4);              // was W(fd, cam + 0x44, 4)
    {
        const unsigned char b = key.selected != 0;       // was cam[0x48] != 0
        W(fd, &b, 1);
    }
}

void WritePmmLightKey(int fd, const mdl::LightKey& key) {
    // File order: links, then color (+0x18..+0x20), then direction
    // (+0x0C..+0x14) - the memory order of the two float triplets differs.
    W(fd, &key.frame, 4);            // was W(fd, light + 0x00, 4)
    W(fd, &key.previous, 4);         // was W(fd, light + 0x04, 4)
    W(fd, &key.next, 4);             // was W(fd, light + 0x08, 4)
    W(fd, &key.color[0], 4);         // was W(fd, light + 0x18, 4)
    W(fd, &key.color[1], 4);         // was W(fd, light + 0x1C, 4)
    W(fd, &key.color[2], 4);         // was W(fd, light + 0x20, 4)
    W(fd, &key.direction[0], 4);     // was W(fd, light + 0x0C, 4)
    W(fd, &key.direction[1], 4);     // was W(fd, light + 0x10, 4)
    W(fd, &key.direction[2], 4);     // was W(fd, light + 0x14, 4)
    {
        const unsigned char b = key.selected != 0;       // was light[0x24] != 0
        W(fd, &b, 1);
    }
}

void WritePmmGravityKey(int fd, const mdl::GravityKey& key) {
    // Selection/gravity track (app+0x380, 36-byte records).  File order:
    // links, noiseEnabled (+0x20), then noise/acceleration/direction.
    W(fd, &key.frame, 4);            // was W(fd, sel + 0x00, 4)
    W(fd, &key.previous, 4);         // was W(fd, sel + 0x04, 4)
    W(fd, &key.next, 4);             // was W(fd, sel + 0x08, 4)
    {
        const unsigned char b = key.noiseEnabled != 0;   // was sel[0x20] != 0
        W(fd, &b, 1);
    }
    W(fd, &key.noise, 4);            // was W(fd, sel + 0x1C, 4)
    W(fd, &key.acceleration, 4);     // was W(fd, sel + 0x0C, 4)
    W(fd, &key.direction[0], 4);     // was W(fd, sel + 0x10, 4)
    W(fd, &key.direction[1], 4);     // was W(fd, sel + 0x14, 4)
    W(fd, &key.direction[2], 4);     // was W(fd, sel + 0x18, 4)
    {
        const unsigned char b = key.selected != 0;       // was sel[0x21] != 0
        W(fd, &b, 1);
    }
}

void WritePmmSelfShadowKey(int fd, const mdl::SelfShadowKey& key) {
    // Self-shadow track (app+0x37C, 24-byte records).  mode is written RAW
    // (the original copies the byte; it is not bool-ified).
    W(fd, &key.frame, 4);            // was W(fd, shadow + 0x00, 4)
    W(fd, &key.previous, 4);         // was W(fd, shadow + 0x04, 4)
    W(fd, &key.next, 4);             // was W(fd, shadow + 0x08, 4)
    W(fd, &key.mode, 1);             // was W(fd, shadow + 0x0C, 1) raw byte
    W(fd, &key.distance, 4);         // was W(fd, shadow + 0x10, 4)
    {
        const unsigned char b = key.selected != 0;       // was shadow[0x14] != 0
        W(fd, &b, 1);
    }
}

// Sparse-occupancy count over frames 1..9999 (record 0 is written densely
// above; the frame dword at record offset +0 is the occupancy test).  The
// original walks a dword pointer three records per iteration - camera
// p += 0x3F / light p += 0x1E / selection p += 0x1B / shadow p += 0x12
// dwords, i.e. 3 * sizeof(record) bytes each - reading the frame dword of
// records 3k+1/3k+2/3k+3.  Typed as a record walk of the same 9999 records.
template <typename TKey>
std::int32_t CountSparseTrackFrames(const TKey* track) {
    std::int32_t cnt = 0;
    const TKey* rec = track + 1;
    for (int k = 0; k < 3333; ++k, rec += 3) {
        if (rec[0].frame != 0) ++cnt;
        if (rec[1].frame != 0) ++cnt;
        if (rec[2].frame != 0) ++cnt;
    }
    return cnt;
}

// Shift-JIS texts from the original .rdata (embedded byte-exact).
const char kJpSaveFailFmt[] =         // 0x52BCE4
    "\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xaa\x95\xdb\x91\xb6\x82\xc5"
    "\x82\xab\x82\xdc\x82\xb9\x82\xf1:%d";
const char kJpSaveFailCaption[] =     // 0x52BD64
    "\x83\x74\x83\x40\x83\x43\x83\x8b\x95\xdb\x91\xb6";
const char kJpCannotOpenText[] =      // 0x52BD80
    "\x82\xbb\x82\xcc\x83\x74\x83\x48\x83\x8b\x83\x5f\x82\xc9\x82\xcd"
    "\x83\x5a\x81\x5b\x83\x75\x82\xc5\x82\xab\x82\xdc\x82\xb9\x82\xf1";


// ---- SaveSceneFile segment functions -------------------------------------
// Split along the original VA map in the file header: each function below is
// the verbatim statement run of one numbered section (the section banner
// comments travel with the code).  The shared scratch buffer stays in
// SaveSceneFile's frame and is passed down, mirroring the original's single
// stack frame.

// 1. file header and global block (0x41B1B3..0x41B355); hdr is the caller's
// error-path sprintf buffer (stack -0x34 in the original), shared down.
void WritePmmFileHeader(int fd, MMDApp* s, HWND main, char* hdr) {
    // ---- 1. file header and global block (0x41B1B3..0x41B355) -----------
    s->state.windowLayoutReady = 0;
    sprintf_s(hdr, 0x100, "Polygon Movie maker 0002");
    // Bytes 25..29 after the 24-char magic + NUL are unspecified stack
    // residue in the original (its sprintf leaves the buffer's prior bytes
    // there); observed original saves carry zeros, so pin them to zero for
    // deterministic output (same deviation class as the text buffer below).
    hdr[25] = hdr[26] = hdr[27] = hdr[28] = hdr[29] = 0;
    W(fd, hdr, 0x1E);                                          // 0x41B1C4

    W(fd, &s->state.renderW, 4);      // 0x41B1D7
    W(fd, &s->state.renderH, 4);      // 0x41B1EA
    {
        const std::int32_t v = reinterpret_cast<const std::int32_t&>(
            s->state.floatingWindow);
        if (v == 0)
            W(fd, &s->state.sidebarWidth, 4);
        else
            W(fd, &s->state.separateWindowSidebarWidth, 4);
    }
    W(fd, &s->state.cameraFov, 4);        // 0x41B22E

    for (int f = 0; f < 7; ++f) {                              // 0x41B22E..0x41B303
        const unsigned char b =
            s->state.optflag[f] != 0;               // 0x2F8..0x2FE
        W(fd, &b, 1);
    }
    W(fd, &s->SelectedModelSlot(), 1);                         // 0x41B31A

    {                                                          // 0x41B33F
        const unsigned char b = static_cast<unsigned char>(
            SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_GETCOUNT, 0, 0) - 1);
        W(fd, &b, 1);
    }
}


// 2. per-model block (0x41B355..0x41C981)
void WritePmmModelBlocks(int fd, const D3DRenderer* renderer, unsigned char** const slots,
                         char* text) {
    // ---- 2. per-model block (0x41B355..0x41C981) ------------------------
    // Not a file-format cap: the x64 save twin sub_7FF7CB4950A0 writes the
    // slot id byte for every occupied slot of the full array (inc dl /
    // cmp dl, 0FFh / jb at 0x7FF7CB496C11..0x7FF7CB496C1A), so the on-disk
    // bound is the slot capacity (0..254 on x64).
    for (unsigned char slot = 0; slot < kModelSlotCount; ++slot) {  // loc_41B360
        if (slots[slot] == 0) continue;
        W(fd, &slot, 1);                                       // 0x41B381
        unsigned char* const model = slots[slot];

        {  // name1 at model+0x2248
            const unsigned char len = NameLen(
                reinterpret_cast<const unsigned char*>(mdl::Mdl(model)->name));
            W(fd, &len, 1);                                    // 0x41B3B7
            W(fd, mdl::Mdl(model)->name, len);                 // 0x41B3D9
        }
        {  // name2 at model+0x227A
            const unsigned char len = NameLen(
                reinterpret_cast<const unsigned char*>(
                    mdl::Mdl(model)->nameEn));
            W(fd, &len, 1);                                    // 0x41B40F
            W(fd, mdl::Mdl(model)->nameEn, len);               // 0x41B432
        }

        WideToSjis(renderer, text,                                  // 0x41B45D
                       mdl::Mdl(model)->path, 0x100);
        W(fd, text, 0x100);                                    // 0x41B471

        const mdl::ModelRecord* const record = mdl::Mdl(model);
        const std::int32_t dispCount =
            static_cast<std::int32_t>(record->boneCount);
        const std::int32_t morphCount =
            static_cast<std::int32_t>(record->morphCount);
        const std::int32_t ikCount =
            static_cast<std::int32_t>(record->ikChainCount);
        const std::int32_t rigidCount =
            static_cast<std::int32_t>(record->boneOrderCount);
        mdl::BoneRecord* const bones = mdl::Bones(model);
        mdl::MorphRecord* const morphs = mdl::Morphs(model);
        mdl::IkChain* const ikChains = mdl::IkChains(model);
        mdl::BoneOrderEntry* const rigidFrames =
            mdl::BoneOrder(model);
        mdl::BoneKey* const dispKeys = mdl::BoneKeys(model);
        mdl::MorphKey* const morphKeys = mdl::MorphKeys(model);
        mdl::DisplayKey* const physKeys = mdl::DisplayKeys(model);

        W(fd, &record->groupCount, 1);                         // 0x41B48E
        W(fd, &record->boneCount, sizeof(record->boneCount));  // 0x41B4AC
        if (dispCount != 0 && dispCount > 0) {
            for (std::int32_t i = 0; i < dispCount; ++i) {
                const unsigned char len = NameLen(
                    reinterpret_cast<const unsigned char*>(bones[i].name));
                W(fd, &len, 1);                                // 0x41B4FF
                W(fd, bones[i].name, len);                     // 0x41B524
            }
        }
        W(fd, &record->morphCount, sizeof(record->morphCount)); // 0x41B562
        if (morphCount != 0 && morphCount > 0) {
            for (std::int32_t i = 0; i < morphCount; ++i) {
                const unsigned char len = NameLen(
                    reinterpret_cast<const unsigned char*>(morphs[i].name));
                W(fd, &len, 1);                                // 0x41B5AF
                W(fd, morphs[i].name, len);                    // 0x41B5D4
            }
        }
        W(fd, &record->ikChainCount, sizeof(record->ikChainCount)); // 0x41B612
        if (ikCount != 0 && ikCount > 0) {
            for (std::int32_t i = 0; i < ikCount; ++i)
                W(fd, &ikChains[i].boneIndex, sizeof(ikChains[i].boneIndex));
        }
        W(fd, &record->boneOrderCount,
          sizeof(record->boneOrderCount));                     // 0x41B680
        if (rigidCount != 0 && rigidCount > 0) {
            for (std::int32_t i = 0; i < rigidCount; ++i)
                W(fd, &rigidFrames[i].boneIndex,
                  sizeof(rigidFrames[i].boneIndex));           // 0x41B6B5
        }
        W(fd, &record->comboSelIndex, 1);                      // 0x41B6EF
        {
            const unsigned char b = record->loadComplete != 0;
            W(fd, &b, 1);                                      // 0x41B719
        }
        W(fd, &record->selectedBone, sizeof(record->selectedBone)); // 0x41B737
        for (const std::int32_t selectedMorph :
             mdl::Mdl(model)->selectedMorphs)
            W(fd, &selectedMorph, sizeof(selectedMorph));      // 0x41B75B

        const unsigned char boneCount = record->groupCount;
        W(fd, &record->groupCount, 1);                         // 0x41B790
        if (boneCount != 0) {
            const mdl::DisplayGroup* const groups =
                mdl::DisplayGroups(model);
            for (unsigned char i = 0; i < boneCount; ++i) {    // 0x41B7D5
                const unsigned char b =
                    groups[i].nameEn[0] != 0;
                W(fd, &b, 1);
            }
        }
        W(fd, &record->boneListPos, sizeof(record->boneListPos)); // 0x41B800
        W(fd, &record->maxFrame, sizeof(record->maxFrame));    // 0x41B81F

        // dense display-frame key records (0x41B860..0x41BAA4); the
        // original walks both the counter and the record offset through
        // a 16-bit register (movzx eax, bx at 0x41BABA), so the pair
        // wraps together past 65535.
        if (dispCount != 0 && dispCount > 0) {
            for (std::uint16_t i = 0;
                 static_cast<std::int32_t>(i) < dispCount; ++i) {
                WritePmmBoneKey(fd, dispKeys[i]);
            }
        }
        // sparse half: signed scan count*0x3C <= off < 60*kBoneKeyCapacity
        {                                                      // 0x41BACC
            std::int32_t cnt = 0;
            if (dispCount < static_cast<std::int32_t>(mdl::kBoneKeyCapacity)) {
                for (std::int32_t i = dispCount;
                     i < static_cast<std::int32_t>(mdl::kBoneKeyCapacity); ++i)
                    if (dispKeys[i].frame != 0)
                        ++cnt;
            }
            W(fd, &cnt, 4);                                    // 0x41BB2D
            if (dispCount < static_cast<std::int32_t>(mdl::kBoneKeyCapacity)) {                          // 0x41BB44
                for (std::int32_t i = dispCount;
                     i < static_cast<std::int32_t>(mdl::kBoneKeyCapacity); ++i) {
                    if (dispKeys[i].frame == 0)
                        continue;
                    W(fd, &i, 4);                              // 0x41BB92
                    WritePmmBoneKey(fd, dispKeys[i]);
                }
            }
        }

        // morph keys: dense then sparse (0x41BEE8..0x41C13A)
        if (morphCount != 0 && morphCount > 0) {
            for (std::int32_t i = 0; i < morphCount; ++i)
                WritePmmMorphKey(fd, morphKeys[i]);
        }
        {                                                      // 0x41BFFD
            std::int32_t cnt = 0;
            if (morphCount < 20000) {
                for (std::int32_t i = morphCount; i < 20000; ++i)
                    if (morphKeys[i].frame != 0)
                        ++cnt;
            }
            W(fd, &cnt, 4);
            if (morphCount < 20000) {
                for (std::int32_t i = morphCount; i < 20000; ++i) {
                    if (morphKeys[i].frame == 0)
                        continue;
                    W(fd, &i, 4);                              // 0x41C05E
                    WritePmmMorphKey(fd, morphKeys[i]);
                }
            }
        }

        // rigid/IK base record (0x41C173..)
        WritePmmDisplayKey(fd, physKeys[0], ikCount, rigidCount);
        {  // sparse rigid/IK scan, 1000 records of 0x1C (0x41C353..)
            std::int32_t cnt = 0;
            for (std::int32_t i = 1; i < 1000; ++i)
                if (physKeys[i].frame != 0) ++cnt;
            W(fd, &cnt, 4);
            for (std::int32_t i = 1; i < 1000; ++i) {          // 0x41C394
                if (physKeys[i].frame == 0)
                    continue;
                W(fd, &i, 4);
                WritePmmDisplayKey(fd, physKeys[i], ikCount, rigidCount);
            }
        }

        // current pose dump (0x41C5EB..0x41C8AD)
        if (dispCount != 0 && dispCount > 0) {
            unsigned char* const physicsState = record->bonePhysicsState;
            unsigned char* const selection = record->boneSelection;
            for (std::int32_t i = 0; i < dispCount; ++i) {
                W(fd, &bones[i].trans[0], 4);                   // 0x41C5EB
                W(fd, &bones[i].trans[1], 4);                   // 0x41C611
                W(fd, &bones[i].trans[2], 4);                   // 0x41C637
                W(fd, &bones[i].rotQuat[0], 4);                 // 0x41C65D
                W(fd, &bones[i].rotQuat[1], 4);                 // 0x41C683
                W(fd, &bones[i].rotQuat[2], 4);                 // 0x41C6A9
                W(fd, &bones[i].rotQuat[3], 4);                 // 0x41C6D2
                {
                    const unsigned char b = bones[i].physicsDisabled != 0;
                    W(fd, &b, 1);                              // 0x41C703
                }
                {
                    const unsigned char b = physicsState[i] != 0; // 0x41C730
                    W(fd, &b, 1);
                }
                {
                    const unsigned char b = selection[i] != 0; // 0x41C75D
                    W(fd, &b, 1);
                }
            }
        }
        if (morphCount != 0 && morphCount > 0) {
            for (std::int32_t i = 0; i < morphCount; ++i)
                W(fd, &morphs[i].value, sizeof(morphs[i].value));
        }
        if (ikCount != 0 && ikCount > 0) {
            for (std::int32_t i = 0; i < ikCount; ++i) {
                const unsigned char b = ikChains[i].enabled != 0;
                W(fd, &b, 1);                                  // 0x41C812
            }
        }
        if (rigidCount != 0 && rigidCount > 0) {
            for (std::int32_t i = 0; i < rigidCount; ++i) {
                W(fd, &rigidFrames[i].windowStart, 4);         // 0x41C867
                W(fd, &rigidFrames[i].windowEnd, 4);           // 0x41C88A
                W(fd, &rigidFrames[i].linkedModel, 4);         // 0x41C8AD
                W(fd, &rigidFrames[i].linkedBone, 4);          // 0x41C8D0
            }
        }
        {
            const unsigned char b =                            // 0x41C91A
                record->postLoadFlag2 != 0;
            W(fd, &b, 1);
        }
        W(fd, &record->edgeScale, sizeof(record->edgeScale));  // 0x41C939
        {
            const unsigned char b =                            // 0x41C963
                record->toonFlag != 0;
            W(fd, &b, 1);
        }
        const unsigned char displayOrder = mdl::Mdl(model)->comboSelIndex2;
        W(fd, &displayOrder, sizeof(displayOrder));             // 0x41C981
    }
}


// 3. camera track + camera misc (0x41C9AE..0x41CF3E)
void WritePmmCameraSection(int fd, MMDApp* s) {
    // ---- 3. camera track (0x41C9AE..0x41CE85) ---------------------------
    const mdl::CameraKey* const cam = s->CameraKeys();  // app+0x374
    WritePmmCameraKey(fd, cam[0]);                              // 0x41C9AE
    {  // sparse scan over camera keys 1..9999 (sizeof(mdl::CameraKey) stride)
        const std::int32_t cnt = CountSparseTrackFrames(cam);   // 0x41CBB8..
        W(fd, &cnt, 4);
        for (std::int32_t i = 1; i < 10000; ++i) {             // 0x41CBE6
            if (cam[i].frame == 0)
                continue;
            W(fd, &i, 4);
            WritePmmCameraKey(fd, cam[i]);                      // 0x41CC00..
        }
    }

    // camera misc (0x41CE85..0x41CF3E)
    W(fd, &s->state.cameraPosition[0], 4);         // 0x334
    W(fd, &s->state.cameraPosition[1], 4);         // 0x338
    W(fd, &s->state.cameraPosition[2], 4);         // 0x33C
    W(fd, &s->state.viewOffsetX, 4);         // 0x308
    W(fd, &s->state.viewOffsetY, 4);         // 0x30C
    W(fd, &s->state.cameraDistance, 4);     // 0xA08DC
    W(fd, &s->state.cameraPitch, 4);         // 0x310
    W(fd, &s->state.cameraYaw, 4);         // 0x314
    W(fd, &s->state.cameraRoll, 4);         // 0x318
    {
        const unsigned char b =                                // 0x31C
            s->state.cameraPerspective != 0;
        W(fd, &b, 1);
    }
}


// 4. light track + light misc (0x41CF51..0x41D28F)
void WritePmmLightSection(int fd, MMDApp* s) {
    // ---- 4. light track (0x41CF51..0x41D207) ----------------------------
    const mdl::LightKey* const light = s->LightKeys();  // app+0x378
    WritePmmLightKey(fd, light[0]);                             // 0x41CF51
    {  // sparse scan over light keys 1..9999 (sizeof(mdl::LightKey) stride)
        const std::int32_t cnt = CountSparseTrackFrames(light); // 0x41D072..
        W(fd, &cnt, 4);
        for (std::int32_t i = 1; i < 10000; ++i) {             // 0x41D0A3
            if (light[i].frame == 0)
                continue;
            W(fd, &i, 4);
            WritePmmLightKey(fd, light[i]);                     // 0x41D0C0..
        }
    }

    // light misc (0x41D207..0x41D28F)
    W(fd, s->LightColor() + 0, sizeof(float));
    W(fd, s->LightColor() + 1, sizeof(float));
    W(fd, s->LightColor() + 2, sizeof(float));
    W(fd, s->LightDirection() + 0, sizeof(float));
    W(fd, s->LightDirection() + 1, sizeof(float));
    W(fd, s->LightDirection() + 2, sizeof(float));
    W(fd, &s->SelectedAccessorySlot(), 1);                      // 0x41D26D 1 byte
    W(fd, &s->DisplayObjectListScrollPosition(), 4);
}


// 5. accessory-shadow list (0x41D2AF..0x41D310)
void WritePmmAccessoryShadowList(int fd, HWND main, char* text) {
    // ---- 5. accessory-shadow list (0x41D2AF..0x41D310) ------------------
    {
        const unsigned char cnt = static_cast<unsigned char>(
            SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_GETCOUNT, 0, 0));
        W(fd, &cnt, 1);                                        // 0x41D2CD
        for (unsigned int i = 0; i < cnt; ++i) {               // 0x41D2FC
            SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_GETLBTEXT, i,
                         reinterpret_cast<LPARAM>(text));
            W(fd, text, 100);                                  // 0x41D310
        }
    }
}


// 6. accessory block (0x41D310..0x41DB42)
void WritePmmAccessoryBlocks(
    int fd, const D3DRenderer* renderer, mdl::AccessoryRecord** const accessories,
    mdl::AccessoryKey** const accTracks, char* text) {
    // ---- 6. accessory block (0x41D310..0x41DB42) ------------------------
    for (unsigned char slot = 0; slot != 0xFF; ++slot) {       // 0x41D351
        if (accessories[slot] == 0) continue;
        W(fd, &slot, 1);
        const mdl::AccessoryRecord& accessory =
            *mdl::Accessory(accessories[slot]);
        const auto* const track = reinterpret_cast<const mdl::AccessoryKey*>(
            accTracks[slot]);

        W(fd, accessory.name, sizeof accessory.name);          // 0x41D370
        WideToSjis(renderer, text,                                   // 0x41D39B
                       accessory.sourcePath, 0x100);
        W(fd, text, 0x100);                                    // 0x41D3AF
        W(fd, &accessory.order, 1);                             // 0x41D3CD

        WritePmmAccessoryKey(fd, track[0]);
        {  // sparse scan over accessory keys 1..9999 (0x3C stride)
            std::int32_t cnt = 0;
            for (std::int32_t i = 1; i < 10000; ++i)
                if (track[i].frame != 0) ++cnt;
            W(fd, &cnt, 4);
            for (std::int32_t i = 1; i < 10000; ++i) {         // 0x41D66E
                if (track[i].frame == 0)
                    continue;
                W(fd, &i, 4);
                WritePmmAccessoryKey(fd, track[i]);
            }
        }
        {
            const unsigned char b = ScaleQuirkByte(            // 0x41D9AC
                accessory.opacity, accessory.visible);
            W(fd, &b, 1);
        }
        W(fd, &accessory.parentModel, 4);                       // 0x41D9CB
        W(fd, &accessory.parentBone, 4);                        // 0x41D9EA
        W(fd, &accessory.rotation[0], 4);                       // 0x41DA09
        W(fd, &accessory.rotation[1], 4);                       // 0x41DA28
        W(fd, &accessory.rotation[2], 4);                       // 0x41DA47
        W(fd, &accessory.scale, 4);                             // 0x41DA69
        W(fd, &accessory.position[0], 4);                       // 0x41DA88
        W(fd, &accessory.position[1], 4);                       // 0x41DAA7
        W(fd, &accessory.position[2], 4);                       // 0x41DAC6
        {
            const unsigned char b = accessory.shadowEnabled != 0;
            W(fd, &b, 1);
        }
        {
            const unsigned char b = accessory.additiveBlend != 0;
            W(fd, &b, 1);
        }
    }
}


// 7. config block (0x41DB42..0x41DF7C)
void WritePmmConfigBlock(int fd, MMDApp* s, HWND main, char* text) {
    // ---- 7. config block (0x41DB42..0x41DF7C) ---------------------------
    W(fd, &s->state.currentFrame, 4);
    W(fd, &s->state.timelineStartFrame, 4);
    W(fd, &s->state.lastRegisteredFrame, 4);
    const std::int32_t savedEditMode = static_cast<std::int32_t>(s->EditMode());
    W(fd, &savedEditMode, 4);
    W(fd, &s->state.cameraReferenceMode, 1);           // raw byte
    {
        const unsigned char b = s->state.playbackLoopEnabled != 0;
        W(fd, &b, 1);
    }
    {
        const unsigned char b = s->state.playbackReturnsToStartFrame != 0;
        W(fd, &b, 1);
    }
    {
        const unsigned char b = s->state.playbackStartsAtCurrentFrame != 0;
        W(fd, &b, 1);
    }
    {  // frame edit readbacks 0x199 then 0x19A (asm 0x41DBEA..0x41DC5F)
        GetWindowTextA(GetDlgItem(main, panel::kPlayStartFrameEdit), text, 8);
        const std::int32_t playStartFrame = atol(text);
        GetWindowTextA(GetDlgItem(main, panel::kPlayStopFrameEdit), text, 8);
        const std::int32_t playStopFrame = atol(text);
        W(fd, &playStartFrame, 4);
        W(fd, &playStopFrame, 4);
    }
    {
        const unsigned char b = s->state.waveEnabled != 0;
        W(fd, &b, 1);
    }
    // WideToSjis leaves the destination untouched for an empty path.  Each
    // fixed-width PMM field must start empty, otherwise the previous field
    // (often the WAV path) is saved as an unset AVI or picture path.
    std::memset(text, 0, 0x100);
    WideToSjis(s->Renderer(), text,                                       // 0x41DC85
                   reinterpret_cast<const wchar_t*>(
                       &s->state.wavPath),
                   0x100);
    W(fd, text, 0x100);                                        // 0x41DCB5
    if (s->AviStream() == nullptr) {                            // 0x41DCC1
        swprintf_s(s->AviBackgroundPath(), 0x100, L"%s",
                   s->AviBackgroundPath());
    }
    W(fd, &s->AviOffsetX(), 4);
    W(fd, &s->AviOffsetY(), 4);
    W(fd, &s->AviScale(), 4);
    std::memset(text, 0, 0x100);
    WideToSjis(s->Renderer(), text, s->AviBackgroundPath(), 0x100);        // 0x41DD32
    W(fd, text, 0x100);                                        // 0x41DD46
    W(fd, &s->AviBackgroundEnabled(), 4);
    if (s->PictureBackgroundTexture() == nullptr) {             // 0x41DD7B
        swprintf_s(s->PictureBackgroundPath(), 0x100, L"%s",
                   s->PictureBackgroundPath());
    }
    W(fd, &s->PictureOffsetX(), 4);
    W(fd, &s->PictureOffsetY(), 4);
    W(fd, &s->PictureScale(), 4);
    std::memset(text, 0, 0x100);
    WideToSjis(s->Renderer(), text, s->PictureBackgroundPath(), 0x100);    // 0x41DDD6
    W(fd, text, 0x100);                                        // 0x41DDEA
    {
        const unsigned char b = s->PictureBackgroundEnabled() != 0;
        W(fd, &b, 1);
    }
    {
        const unsigned char b = s->state.fpsOverlayEnabled != 0;
        W(fd, &b, 1);
    }
    {
        const unsigned char b = s->state.groundGridEnabled != 0;
        W(fd, &b, 1);
    }
    {
        const unsigned char b = s->state.groundShadowEnabled != 0;
        W(fd, &b, 1);
    }
    W(fd, &s->state.fpsLimit, 4);     // 0xA08E0
    W(fd, &reinterpret_cast<ScreenCaptureMode&>(s->state.captureMode), 4);
    W(fd, &s->state.accessoryRenderSplitOrder, 4);
    W(fd, &s->ProjectedShadowAmbientIntensity(), 4);
    {
        const unsigned char b = s->state.projectedShadowBlendEnabled != 0;
        W(fd, &b, 1);
    }
    W(fd, reinterpret_cast<const unsigned char*>(
              &s->PlaybackPhysicsMode()), 1);                  // raw byte
    W(fd, &s->state.gravityMagnitude, 4);      // 0x9EDC4
    W(fd, &s->state.gravityNoise, 4);
    W(fd, &s->state.gravityX, 4);        // 0x9EDB8
    W(fd, &s->state.gravityY, 4);        // 0x9EDBC
    W(fd, &s->state.gravityZ, 4);        // 0x9EDC0
    {
        const unsigned char b = s->state.gravityNoiseEnabled != 0;
        W(fd, &b, 1);
    }
}


// 8. selection track + shadow-mode byte (0x41DF7C..0x41E284)
void WritePmmSelectionSection(int fd, MMDApp* s) {
    // ---- 8. selection/self-shadow track (0x41DF7C..0x41E25E) ------------
    const mdl::GravityKey* const sel = s->GravityKeys();  // app+0x380
    WritePmmGravityKey(fd, sel[0]);                             // 0x41DF7C
    {  // sparse scan over selection keys 1..9999
       // (sizeof(mdl::GravityKey) stride)
        const std::int32_t cnt = CountSparseTrackFrames(sel);   // 0x41E0AC..
        W(fd, &cnt, 4);
        for (std::int32_t i = 1; i < 10000; ++i) {             // 0x41E0E4
            if (sel[i].frame == 0)
                continue;
            W(fd, &i, 4);
            WritePmmGravityKey(fd, sel[i]);                     // 0x41E101..
        }
    }

    {
        const unsigned char b = s->state.selfShadowEnabled != 0;
        W(fd, &b, 1);                                          // 0x41E25E
    }
    W(fd, &s->state.physicsInterval, 4);        // 0xA0D2C
}


// 9. self-shadow track + 10. config2 (0x41E284..0x41E6E5); kept as one
// function - the config2 run continues the same write sequence.
void WritePmmShadowAndConfig2(int fd, MMDApp* s, HWND main, char* text) {
    // ---- 9. self-shadow track (0x41E284..0x41E46F) ----------------------
    const mdl::SelfShadowKey* const shadow = s->ShadowKeys();  // app+0x37C
    WritePmmSelfShadowKey(fd, shadow[0]);                       // 0x41E284
    {  // sparse scan over self-shadow keys 1..9999
       // (sizeof(mdl::SelfShadowKey) stride)
        const std::int32_t cnt = CountSparseTrackFrames(shadow);// 0x41E34D..
        W(fd, &cnt, 4);
        for (std::int32_t i = 1; i < 10000; ++i) {             // 0x41E384
            if (shadow[i].frame == 0)
                continue;
            W(fd, &i, 4);
            WritePmmSelfShadowKey(fd, shadow[i]);               // 0x41E3A1..
        }
    }

    // ---- 10. config2 (0x41E46F..0x41E6E5) --------------------------------
    W(fd, &s->state.modelOutlineColorRed, 4);
    W(fd, &s->state.modelOutlineColorGreen, 4);
    W(fd, &s->state.modelOutlineColorBlue, 4);
    {
        const unsigned char b = s->state.blackBackgroundEnabled != 0;
        W(fd, &b, 1);
    }
    W(fd, &s->state.cameraParentModel, 4);
    W(fd, &s->state.cameraParentBone, 4);
    W(fd, &s->state.cameraAttachmentBasis[0], 4);   // 0xA0438
    for (int i = 0; i < 15; ++i)                               // 0xA043C..A0474
        W(fd, &s->state.cameraAttachmentBasis[1 + i], 4);
    {
        const unsigned char b = s->state.followCameraEnabled != 0;
        W(fd, &b, 1);                                          // 0x9ED98
    }
    {
        const unsigned char b = s->state.cameraAttachmentTransformSuppressed != 0;
        W(fd, &b, 1);                                          // 0xA0478
    }
    {
        const unsigned char b = s->state.floorVisible != 0;
        W(fd, &b, 1);
    }
    {  // 0x22A edit readback on the alt dialog when present (0x41E69E)
        const HWND alt = s->state.floatingWindow;
        GetWindowTextA(GetDlgItem(alt != nullptr ? alt : main, panel::kGotoFrameEdit), text, 10);
        std::int32_t v = atol(text);
        if (v < 0) v = 0;
        W(fd, &v, 4);                                          // 0x41E6D1
    }
    {
        const unsigned char one = 1;                           // 0x41E6E5
        W(fd, &one, 1);
    }
}


// 11. per-model tail (0x41E70D)
void WritePmmModelTail(int fd, unsigned char** const slots) {
    // ---- 11. per-model tail (0x41E70D) -----------------------------------
    // x64 twin walks slots 0..254 (cmp dl, 0FFh / jb at 0x7FF7CB498DC2).
    for (unsigned char slot = 0; slot < kModelSlotCount; ++slot) {  // loc_41E6F0
        if (slots[slot] == 0) continue;
        W(fd, &slot, 1);
        const std::int32_t registration =
            mdl::Mdl(slots[slot])->frameRegistrationSelection;
        W(fd, &registration, 4);                               // 0x41E72C
    }
}


}  // namespace

void SaveSceneFile(MMDApp* app) {
    auto* s = app;

    // The scratch CHAR buffer (stack "Text").  The original leaves this
    // uninitialized, so every 0x100 fixed-width path field it feeds carries
    // whatever stack history preceded the save - deterministic in the
    // original's codegen, run-to-run garbage in ours.  Zero-initialize the
    // buffer once: first conversion gets zero tails (a documented deviation
    // - the original's garbage is unreproducible by design).  The config
    // block clears this buffer for each optional media path, so an empty
    // path cannot inherit the previous field's contents.
    char text[0x100] = {};
    char hdr[0x100];    // sprintf buffer at stack -0x34
    wchar_t title[0x100];

    unsigned char** const slots = s->ModelSlots();
    mdl::AccessoryRecord** const accessories = s->AccessorySlots();
    mdl::AccessoryKey** const accTracks = s->AccessoryKeyTracks();
    HWND const main = reinterpret_cast<HWND>(s->Hwnd());

    if (s->EnvFileName()[0] == L'\\') {                        // 0x41B097
        MessageBoxA(main,
                    s->EnglishUI() != 0 ? "Cannot open save file"
                                        : kJpCannotOpenText,
                    "save", 0);
        return;
    }

    s->SceneModified() = 0;                                    // 0x41B0C6
    int fd = -1;
    const errno_t err =
        _wsopen_s(&fd, s->EnvFileName(), 0x8301, 0x40, 0x80);  // 0x41B125
    if (err != 0) {
        if (s->EnglishUI() == 0)
            sprintf_s(hdr, 0x100, kJpSaveFailFmt, err);
        else
            sprintf_s(hdr, 0x100, "Cannot save file:%d", err);
        MessageBoxA(main, hdr,
                    s->EnglishUI() == 0 ? kJpSaveFailCaption : "save file",
                    0);
        return;
    }

    WritePmmFileHeader(fd, s, main, hdr);                   // 1. 0x41B1B3..0x41B355
    WritePmmModelBlocks(fd, s->Renderer(), slots, text);                   // 2. 0x41B355..0x41C981
    WritePmmCameraSection(fd, s);                           // 3. 0x41C9AE..0x41CF3E
    WritePmmLightSection(fd, s);                            // 4. 0x41CF51..0x41D28F
    WritePmmAccessoryShadowList(fd, main, text);            // 5. 0x41D2AF..0x41D310
    WritePmmAccessoryBlocks(fd, s->Renderer(), accessories, accTracks, text);  // 6. 0x41D310..0x41DB42
    WritePmmConfigBlock(fd, s, main, text);                 // 7. 0x41DB42..0x41DF7C
    WritePmmSelectionSection(fd, s);                        // 8. 0x41DF7C..0x41E25E
    WritePmmShadowAndConfig2(fd, s, main, text);            // 9+10. 0x41E284..0x41E6E5
    WritePmmModelTail(fd, slots);                           // 11. 0x41E70D


    // ---- 12. success tail (0x41E747) --------------------------------------
    _close(fd);
    swprintf_s(title, 0x100, pmm_io::kAppTitleFormat, s->EnvFileName());
    SetWindowTextW(main, title);
    MessageBeep(0x40);
    s->state.windowLayoutReady = 1;

    // 内置 MMEffect：场景保存成功通知（EMM 自动保存）。
    mme::NotifyPmmSaved(s);
}

}  // namespace mikudancestudio
