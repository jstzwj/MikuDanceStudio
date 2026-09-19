// ===========================================================================
// VA 0x00434B60 - VMD motion file loader  (0x147F bytes)
// ===========================================================================
// __thiscall on the APP block (not the model):
//
//   LoadVmdMotion(app, FileName)   FileName = wide path, opened directly
//                               via _wsopen_s (x64 original 0x7FF7CB48D1B7;
//                               the x86 original's ANSI path + _sopen_s was
//                               dropped in favor of the x64 behavior)
//
// Two file flavors share the entry: "Vocaloid Motion Data file" (old,
// 10-byte model name, ver=1) and "Vocaloid Motion Data 0002" (20-byte
// name, ver=2).  app+760 selects the target:
//
//   != 0  CAMERA/LIGHT path - the motion goes to the app's global tracks:
//         bone/morph sections are parsed and discarded, then camera keys
//         (old format expands its single shared 4-byte interpolation
//         curve into all 24 control bytes), light keys (sub_411900) and,
//         for ver 2, self-shadow keys (sub_4120B0).  Tail: ReloadModels +
//         three list refreshers.
//   == 0  MODEL path - model name check against ModelRecord::name (20-byte SJIS;
//         "カメラ・照明" is rejected with its own message, anything else
//         prompts OK/CANCEL), then:
//           * all three key arrays' allocation marks are cleared
//             (bone +56 of kBoneKeyCapacity x60B records, morph +16 of
//             20000x20B, IK master +20 of 1000x28B),
//           * the 30-slot undo/motion ring at model+9964 (28B slots
//             {+0 type=2, +4 zeroed, +12 current frame, +16 ptr to a
//             36B-per-bone initial-pose snapshot, +20 ptr to 192B-per-key
//             raw storage}) advances model+12724 (wraps at 30) and refills
//             the slot; the snapshot copies each bone's +320 position,
//             +332 quaternion and a byte from the model+11672 array,
//           * keyVisitMap (model+14596 x86 / +0x3CAC x64, one byte per
//             bone-key record) is zeroed, sub_4A4940 resets the
//             key allocator,
//           * bone keys: 15-byte name (LF stripped), frame, position,
//             quaternion, then a 4-channel interpolation block - channel
//             0 arrives as two u16s (the second compared against 3939)
//             and channels 1..3 as u32s, each followed by three u32s -
//             registered via sub_49D880 while the "still ok" flag holds,
//           * sub_4A49A0 links/sorts, morph keys via sub_49F190
//             (non-finite values clamped to 0), app+647532 max-frame bump,
//           * three discarded counts, then the model-registration/IK
//             section (frame, visible byte, count <= 10000, 21B records:
//             20 bytes + one flag byte) via sub_49F8C0, freed immediately,
//             max-frame bump again.
//
// Tail for the model path: _close, PanelPaint (0x414610), SelectionReeval
// (0x430510), then SeekModelFrame(model, app+0x980 current frame, app+0xA0CC4
// physics mode) - the frame-seek ported in model_frame_seek.cpp.
//
// The register chain now calls the real (app, rec) overloads of
// 0x410AA0 / 0x411900 / 0x4120B0 defined in src/window/command_frame_edit.cpp
// (the by-value stack records of the original are passed as pointers -
// documented stub-era deviation absorbed by those ports).  The remaining
// registrar family (0x49D880, 0x49F190, 0x49F8C0, 0x4A49A0, 0x4A4A00) is
// ported in src/model/key_registrars.cpp.
//
// Reference: IDA live disassembly of MikuMikuDance.exe v932 (sole source of
// truth; ../translated/ reference files deviate).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <float.h>
#include <io.h>
#include <sys/stat.h>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
// loader-tail helpers defined in src/app/late_ports.cpp (window-session
// convention: local declarations for stubbed targets)
void SelectionReeval(MMDApp* app);                   // 0x430510
void RefreshLightPanel(MMDApp* app);                         // 0x411070
void RefreshSelfShadowPanel(MMDApp* app);                         // 0x411B90
void ApplyGravityTrack(MMDApp* app);                        // 0x412330 (void in port)

namespace {

// SJIS constants from .rdata (byte-exact)
const char kCameraLightName[] =  // 0x52BCB8 "カメラ・照明"
    "\x83\x4a\x83\x81\x83\x89\x81\x45\x8f\xc6\x96\xbe";
const char kJpTitleMotionLoad[] =  // 0x52CD58 "モーション読込"
    "\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66\x81\x5b\x83\x5e"
    "\x93\xc7\x8d\x9e";
const char kJpNotVmd[] =  // 0x52CACC
    "\x82\xb1\x82\xcc\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xcd\x20Vocaloid "
    "Motion Data file \x82\xc5\x82\xcd\x82\xa0\x82\xe8\x82\xdc\x82\xb9"
    "\x82\xf1";
const char kJpCannotOpen[] =  // 0x52CDD4 "...:%d"
    "\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xaa\x93\xc7\x82\xdd\x8d\x9e"
    "\x82\xdf\x82\xdc\x82\xb9\x82\xf1:%d";
const char kJpNoCamLight[] =  // 0x52CCA4
    "\x82\xb1\x82\xcc\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66"
    "\x81\x5b\x83\x5e\x82\xc9\x82\xcd\x83\x4a\x83\x81\x83\x89\x81\x45"
    "\x8f\xc6\x96\xbe\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xcd\x82\xa0"
    "\x82\xe8\x82\xdc\x82\xb9\x82\xf1";
const char kJpIsCamLight[] =  // 0x52CC20
    "\x82\xb1\x82\xcc\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66"
    "\x81\x5b\x83\x5e\x82\xcd\x83\x4a\x83\x81\x83\x89\x81\x45\x8f\xc6"
    "\x96\xbe\x97p\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xc5\x82\xb7\n\n"
    "\x83\x4a\x83\x81\x83\x89\x95\xd2\x8f\x57\x83\x82\x81\x5b\x83\x68"
    "\x82\xc5\x93\xc7\x82\xdd\x8d\x9e\x82\xf1\x82\xc5\x89\xba\x82\xb3"
    "\x82\xa2";
const char kJpNoCamLightShadow[] =  // 0x52CD10
    "\x82\xb1\x82\xcc\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66"
    "\x81\x5b\x83\x5e\x82\xc9\x82\xcd\x81\x41\x83\x4a\x83\x81\x83\x89"
    "\x81\x45\x8f\xc6\x96\xbe\x81\x45\x83\x5a\x83\x8b\x83\x74\x89\x65"
    "\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xcd\x8a\xdc\x82\xdc\x82\xea"
    "\x82\xdc\x82\xb9\x82\xf1";
const char kJpModelMismatch[] =  // 0x52CB48 "... %s ..."
    "\x82\xb1\x82\xcc\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xcd\x20%s\x97"
    "p\x82\xcc\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66\x81\x5b"
    "\x83\x5e\x82\xc5\x82\xb7\n\x93\xaf\x82\xb6\x83\x7b\x81\x5b\x83\x93"
    "\x96\xbc\x82\xcc\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x82\xcc"
    "\x82\xdd\x82\xf0\x8e\xe6\x82\xe8\x8d\x9e\x82\xde\x8e\x96\x82\xc9"
    "\x82\xc8\x82\xe8\x82\xdc\x82\xb7\n\x91\xb1\x8d\x73\x82\xb5\x82\xdc"
    "\x82\xb7\x82\xa9\x81\x48";

}  // namespace

// ---- VA 0x00434B60 --------------------------------------------------------
int LoadVmdMotion(MMDApp* app, const wchar_t* fileName) {  // VA 0x00434B60
    auto& s = *app;
    unsigned char* const model = app->SelectedModel();

    int fileHandle;
    unsigned int count = 0;  // DstBuf - reused for every section count
    char text[256], buffer[256];
    unsigned char rec[96];
    unsigned int scratch;    // k
    unsigned short word;     // v68

    const HWND hwnd = s.state.hwnd;
    const bool english = s.state.englishUI != 0;

    // x64 @0x7FF7CB48D19B..0x7FF7CB48D1B7: wide path straight into
    // _wsopen_s(fd, fileName, _O_BINARY, _SH_DENYNO, _S_IREAD)
    const errno_t openErr =
        _wsopen_s(&fileHandle, fileName,
                  0x8000 /*_O_BINARY*/, 0x40 /*_SH_DENYNO*/, 0x80 /*_S_IREAD*/);
    if (openErr != 0) {
        if (english)
            sprintf_s(text, 0x100, "Cannot open file:%d", openErr);
        else
            sprintf_s(text, 0x100, kJpCannotOpen, openErr);
        // Caption is the global empty Locale string in the reference
        // (x64 0x7FF7CB48D1F1 -> 0x7FF7CB54A2B0), not a title constant.
        MessageBoxA(hwnd, text, "", 0);
        return 0;
    }

    // header + model name (the name read overwrites the header buffer)
    _read(fileHandle, text, 30);
    int ver = 0;
    unsigned int flavor = 0;
    int nameLen;
    if (std::strcmp(text, "Vocaloid Motion Data file") == 0) {
        ver = 1; flavor = 1; nameLen = 10;
    } else if (std::strcmp(text, "Vocaloid Motion Data 0002") == 0) {
        ver = 2; flavor = 2; nameLen = 20;
    } else {
        MessageBoxA(hwnd,
                    english ? "This is not Vocaloid Motion Data file !!"
                            : kJpNotVmd,
                    english ? "load motion file" : kJpTitleMotionLoad, 0);
        _close(fileHandle);
        return 0;
    }
    _read(fileHandle, text, nameLen);

    if (s.state.optflag[0] != 0) {
        // ==== camera / light / self-shadow motion =========================
        if (ver == 2 && std::memcmp(text, kCameraLightName, 13) != 0) {
            MessageBoxA(hwnd,
                        english
                            ? "This motion file has not cammera,light,shadow data."
                            : kJpNoCamLightShadow,
                        english ? "load motion data" : kJpTitleMotionLoad, 0);
            _close(fileHandle);
            return 0;
        }
        // discard the bone keys: 15-byte name + 24 dwords each
        _read(fileHandle, &count, 4);
        for (unsigned int i = 0; i < count; ++i) {
            _read(fileHandle, text, 15);
            for (int w = 0; w < 24; ++w) _read(fileHandle, &scratch, 4);
        }
        // discard the morph keys: 15 + 4 + 4
        _read(fileHandle, &count, 4);
        for (unsigned int j = 0; j < count; ++j) {
            _read(fileHandle, text, 15);
            _read(fileHandle, &scratch, 4);
            _read(fileHandle, &scratch, 4);
        }
        if (_read(fileHandle, &count, 4) == 0) {
            MessageBoxA(hwnd,
                        english
                            ? "This motion data has not camera/light data !!"
                            : kJpNoCamLight,
                        english ? "load motion data" : kJpTitleMotionLoad, 0);
            _close(fileHandle);
            return 0;
        }
        if (ver == 1) {
            unsigned char ok = 1;
            for (unsigned int k = 0; k < count; ++k) {
                std::memset(rec, 0, sizeof rec);
                *reinterpret_cast<int*>(rec + 64) = -1;
                *reinterpret_cast<int*>(rec + 68) = 0;
                _read(fileHandle, rec + 0, 4);    // frame
                _read(fileHandle, rec + 60, 4);   // distance
                _read(fileHandle, rec + 4, 4);    // eye xyz
                _read(fileHandle, rec + 8, 4);
                _read(fileHandle, rec + 12, 4);
                _read(fileHandle, rec + 16, 4);   // target xyz
                _read(fileHandle, rec + 20, 4);
                _read(fileHandle, rec + 24, 4);
                _read(fileHandle, rec + 33, 1);   // shared 4-byte curve
                _read(fileHandle, rec + 45, 1);
                _read(fileHandle, rec + 39, 1);
                _read(fileHandle, rec + 51, 1);
                // x64 @0x7FF7CB48D5A5..0x7FF7CB48D5EF: 每个共享字节只填充
                // 其后的 5 字节（20 次赋值，rec+33/39/45/51 起各成一组
                // 6 字节均匀曲线）；rec[57] 保持清零不被触碰
                for (int c = 1; c <= 5; ++c) {
                    rec[33 + c] = rec[33]; rec[45 + c] = rec[45];
                    rec[39 + c] = rec[39]; rec[51 + c] = rec[51];
                }
                *reinterpret_cast<int*>(rec + 28) = 45;  // fov default
                rec[32] = 0;                             // view flag
                if (ok && !RegisterCameraKey(app, rec)) ok = 0;
            }
        } else {
            unsigned char ok = 1;
            for (unsigned int k = 0; k < count; ++k) {
                std::memset(rec, 0, sizeof rec);
                *reinterpret_cast<int*>(rec + 64) = -1;
                *reinterpret_cast<int*>(rec + 68) = 0;
                _read(fileHandle, rec + 0, 4);
                _read(fileHandle, rec + 60, 4);
                _read(fileHandle, rec + 4, 4);
                _read(fileHandle, rec + 8, 4);
                _read(fileHandle, rec + 12, 4);
                _read(fileHandle, rec + 16, 4);
                _read(fileHandle, rec + 20, 4);
                _read(fileHandle, rec + 24, 4);
                for (int c = 0; c < 6; ++c) {     // interleaved curve bytes
                    _read(fileHandle, rec + c + 33, 1);
                    _read(fileHandle, rec + c + 45, 1);
                    _read(fileHandle, rec + c + 39, 1);
                    _read(fileHandle, rec + c + 51, 1);
                }
                _read(fileHandle, rec + 28, 4);   // fov
                _read(fileHandle, &word, 1);
                rec[32] = static_cast<unsigned char>(word != 0);
                if (ok && !RegisterCameraKey(app, rec)) ok = 0;
            }
        }
        // light keys
        _read(fileHandle, &count, 4);
        unsigned char lightOk = 1;
        for (unsigned int k = 0; k < count; ++k) {
            std::memset(rec, 0, sizeof rec);
            _read(fileHandle, rec + 0, 4);    // frame
            _read(fileHandle, rec + 16, 4);   // color rgb
            _read(fileHandle, rec + 20, 4);
            _read(fileHandle, rec + 24, 4);
            _read(fileHandle, rec + 4, 4);    // position xyz
            _read(fileHandle, rec + 8, 4);
            _read(fileHandle, rec + 12, 4);
            if (lightOk && !RegisterLightKey(app, rec)) lightOk = 0;
        }
        // self-shadow keys (ver 2 only)
        unsigned char shadowOk = 1;
        if (flavor == 2 && _read(fileHandle, &count, 4) > 0) {
            for (unsigned int m = 0; m < count; ++m) {
                std::memset(rec, 0, sizeof rec);
                _read(fileHandle, rec + 0, 4);
                _read(fileHandle, rec + 4, 1);
                _read(fileHandle, rec + 8, 4);
                if (shadowOk && !RegisterSelfShadowKey(app, rec)) shadowOk = 0;
            }
        }
        _close(fileHandle);
        PanelPaint(app);
        SelectionReeval(app);
        ReloadModels(app);                              // 0x42E640
        RefreshLightPanel(app);
        RefreshSelfShadowPanel(app);
        ApplyGravityTrack(app);
        // x64 @0x7FF7CB48E7ED chains: `call sub_7FF7CB47D320` then jumps
        // straight to the common epilogue, returning its eax (key field /
        // trailing EnableWindow BOOL).  The port's ApplyGravityTrack
        // (track_apply.cpp) is void and its signature is linked as void
        // across 15+ files, so the value cannot be taken here yet.
        return 0;
    }

    // ==== model motion =====================================================
    if (std::strcmp(text, mdl::Mdl(model)->name) != 0) {
        if (std::memcmp(text, kCameraLightName, 13) == 0) {
            MessageBoxA(hwnd,
                        english
                            ? "This motion data if camera/light data !!"
                            : kJpIsCamLight,
                        english ? "load motion data" : kJpTitleMotionLoad, 0);
            _close(fileHandle);
            return 0;
        }
        if (english)
            sprintf_s(buffer, 0x100,
                      "This motion file is the data for '%s'.\n"
                      "You can regist the motion only same bone name.\n"
                      "Are you OK?", text);
        else
            sprintf_s(buffer, 0x100, kJpModelMismatch, text);
        if (MessageBoxA(hwnd, buffer,
                        english ? "load motion data" : kJpTitleMotionLoad,
                        1 /*MB_OKCANCEL*/) != 1 /*IDOK*/) {
            _close(fileHandle);
            return 0;
        }
    }
    _read(fileHandle, &count, 4);
    const unsigned int boneKeyCount = count;

    // clear the allocation marks of all three key arrays
    mikudancestudio::mdl::BoneKey* const bkeys = mikudancestudio::mdl::BoneKeys(model);
    mikudancestudio::mdl::MorphKey* const mkeys = mikudancestudio::mdl::MorphKeys(model);
    mikudancestudio::mdl::DisplayKey* const ikeys = mikudancestudio::mdl::DisplayKeys(model);
    for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i) bkeys[i].allocated = 0;
    for (int i = 0; i < 20000; ++i) mkeys[i].allocated = 0;
    for (int i = 0; i < 1000; ++i) ikeys[i].allocated = 0;

    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    mdl::ModelRecord& record = *mdl::Mdl(model);
    record.undoDirty = 1;
    record.redoDirty = 0;
    std::uint32_t& ringIdx = record.undoState[0];
    if (++ringIdx >= 30) ringIdx = 0;
    record.undoState[1] = ringIdx;

    auto& undo = mikudancestudio::mdl::Mdl(model)->undoRings[0].slots[ringIdx];
    undo.operation = 2;
    undo.dirty = 0;
    undo.frame = s.state.currentFrame;
    if (undo.bonePose != nullptr)
        operator delete(undo.bonePose);  // x64 @0x7FF7CB48DE1C ??3
    const int boneCnt = static_cast<int>(record.boneCount);
    // x64 @0x7FF7CB48DE70..0x7FF7CB48E013: 每骨骼一个 36 字节槽位
    // (boneIndex / trans / rotQuat / 物理标志)，源端等价于按
    // BoneRecord 类型化字段读取（x64 骨骼表 624 字节 stride 下
    // trans@+328、rotQuat@+340），物理标志取 model+12584[b]
    auto* const snap = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
        operator new(boneCnt * sizeof(mikudancestudio::mdl::BonePoseSnapshot)));
    undo.bonePose = snap;
    std::memset(snap, 0,
                static_cast<std::size_t>(boneCnt) *
                    sizeof(mikudancestudio::mdl::BonePoseSnapshot));
    if (boneCnt > 0) {
        mikudancestudio::mdl::BoneRecord* const bones =
            mikudancestudio::mdl::Bones(model);
        const unsigned char* const flags =
            mikudancestudio::mdl::Mdl(model)->bonePhysicsState;
        for (int b = 0; b < boneCnt; ++b) {
            mikudancestudio::mdl::BonePoseSnapshot& slot = snap[b];
            slot.boneIndex = b;
            std::memcpy(slot.position, bones[b].trans, sizeof slot.position);
            std::memcpy(slot.rotation, bones[b].rotQuat, sizeof slot.rotation);
            slot.physicsDisabled = flags[b];
        }
    }
    if (undo.auxiliaryPose != nullptr)
        operator delete(undo.auxiliaryPose);  // x64 @0x7FF7CB48E08F ??3
    unsigned char* const rawKeys = static_cast<unsigned char*>(
        operator new(192 * boneKeyCount));
    undo.auxiliaryPose = rawKeys;
    std::memset(rawKeys, 0, 192 * boneKeyCount);
    std::memset(mdl::Mdl(model)->keyVisitMap, 0,
                sizeof(mdl::Mdl(model)->keyVisitMap));
    ResetBoneKeyCursor(model);
    (void)rawKeys;  // consumed by the registrar family (0x49D880 et al.)

    // bone keys
    unsigned char boneOk = 1;
    for (unsigned int k = 0; k < boneKeyCount; ++k) {
        _read(fileHandle, text, 15);
        if (char* nl = std::strchr(text, 10)) *nl = 0;
        strcpy_s(reinterpret_cast<char*>(rec), 30, text);
        _read(fileHandle, rec + 32, 4);   // frame
        _read(fileHandle, rec + 52, 4);   // position
        _read(fileHandle, rec + 56, 4);
        _read(fileHandle, rec + 60, 4);
        _read(fileHandle, rec + 36, 4);   // quaternion
        _read(fileHandle, rec + 40, 4);
        _read(fileHandle, rec + 44, 4);
        _read(fileHandle, rec + 48, 4);
        for (int ch = 0; ch < 4; ++ch) {
            if (ch != 0) {
                _read(fileHandle, &scratch, 4);
                rec[ch + 65] = static_cast<unsigned char>(scratch);
            } else {
                _read(fileHandle, &word, 2);
                rec[65] = static_cast<unsigned char>(word);
                _read(fileHandle, &word, 2);
                rec[64] = static_cast<unsigned char>(word == 3939);
            }
            _read(fileHandle, &scratch, 4);
            rec[ch + 69] = static_cast<unsigned char>(scratch);
            _read(fileHandle, &scratch, 4);
            rec[ch + 73] = static_cast<unsigned char>(scratch);
            _read(fileHandle, &scratch, 4);
            rec[ch + 77] = static_cast<unsigned char>(scratch);
        }
        if (boneOk) {
            if (!RegisterBoneKey(model, rec,
                           s.state.currentFrame, 0))
                boneOk = 0;
        }
    }
    ResetMorphKeyCursor(model);

    // morph keys
    _read(fileHandle, &count, 4);
    unsigned char morphOk = 1;
    for (unsigned int k = 0; k < count; ++k) {
        _read(fileHandle, text, 15);
        text[15] = 0;
        if (char* nl = std::strchr(text, 10)) *nl = 0;
        strcpy_s(reinterpret_cast<char*>(rec), 30, text);
        _read(fileHandle, rec + 32, 4);   // frame
        _read(fileHandle, rec + 36, 4);   // value
        if (!_finite(*reinterpret_cast<float*>(rec + 36)))
            *reinterpret_cast<float*>(rec + 36) = 0.0f;
        if (morphOk) {
            if (!RegisterMorphKeyFromRecord(model, rec,
                           s.state.currentFrame))
                morphOk = 0;
        }
    }
    unsigned int modelMax = record.maxFrame;
    if (s.LastRegisteredFrame() < modelMax)
        s.LastRegisteredFrame() = modelMax;

    // camera/light/self-shadow counts are parsed and discarded
    _read(fileHandle, &count, 4);
    _read(fileHandle, &count, 4);
    _read(fileHandle, &count, 4);
    if (_read(fileHandle, &count, 4) > 0) {
        ResetDisplayKeyCursor(model);
        unsigned char ikOk = 1;
        for (unsigned int k = 0; k < count; ++k) {
            _read(fileHandle, rec + 0, 4);   // frame
            _read(fileHandle, &word, 1);     // visible
            rec[4] = static_cast<unsigned char>(word != 0);
            _read(fileHandle, rec + 8, 4);   // IK entry count
            int n = *reinterpret_cast<int*>(rec + 8);
            if (*reinterpret_cast<unsigned int*>(rec + 8) > 0x2710u) break;
            unsigned char* entries = nullptr;
            if (n > 0)
                entries = static_cast<unsigned char*>(
                    operator new(21 * n));  // x64 @0x7FF7CB48E6AD ??2
            for (int e = 0; e < n; ++e) {
                _read(fileHandle, entries + 21 * e, 20);
                _read(fileHandle, &word, 1);
                entries[21 * e + 20] = static_cast<unsigned char>(word != 0);
            }
            *reinterpret_cast<int*>(rec + 16) = 0;
            if (ikOk) {
                if (!RegisterDisplayKeyFromRecord(model, *reinterpret_cast<int*>(rec), rec[4],
                               n, entries, 0, nullptr,
                               s.state.currentFrame))
                    ikOk = 0;
            }
            // Match the scalar raw-storage allocation above. The binary
            // imports an array-delete thunk here; its allocator convention
            // does not require reproducing mismatched C++ allocation APIs.
            if (entries) operator delete(entries);
        }
        modelMax = record.maxFrame;
        if (s.LastRegisteredFrame() < modelMax)
            s.LastRegisteredFrame() = modelMax;
    }

    _close(fileHandle);
    PanelPaint(app);                                    // 0x414610
    SelectionReeval(app);                               // 0x430510
    return SeekModelFrame(model, s.state.currentFrame,
                     s.PlaybackPhysicsMode());
}

// Wide-path entry used by the ported file dialog (0x461300's GetOpenFileNameW
// buffer) and the drop handler.  The x64 original opens the wide path
// directly via _wsopen_s (0x7FF7CB48D1B7) - no code-page conversion.
void LoadVmdFile(const wchar_t* path) {
    if (g_Block == nullptr) return;
    LoadVmdMotion(g_Block, path);
}

}  // namespace mikudancestudio
