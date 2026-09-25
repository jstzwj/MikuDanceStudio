// ===========================================================================
// VA 0x00419370 - SaveVmdFile  (0xE23 bytes)
// ===========================================================================
// __thiscall(app, path); port keeps the free-function shape of the dialog
// layer (g_Block).  Two flavours keyed by the edit-mode byte app+0x2F8:
//
//   0x2F8 == 0  MODEL motion - active model (this+0x780[slot @0x910]):
//     counts + min-frame over the three model key arrays (all marks are the
//     occupancy byte at +56 of the 60B bone record, +16 of the 20B morph
//     record, +20 of the 28B IK/display master record; the original counts
//     5 records per 300B/100B/140B group walk - flat scan is the same set).
//   0x2F8 != 0  CAMERA/LIGHT/SELF-SHADOW - the same walk over the global
//     app tables (+0x374 camera 84B / +0x378 light 40B / +0x37C shadow 24B;
//     mark +0x48/+0x24/+0x14).
//
// File layout written (every frame rebased by the min frame of all keys):
//   _wsopen_s(fd, path, 0x8301(_O_BINARY|_O_WRONLY|_O_CREAT|_O_TRUNC),
//             0x40 _SH_DENYNO, 0x80 _S_IREAD)
//   sprintf_s(buf,0x100,"Vocaloid Motion Data 0002"); write(fd,buf,30)
//   name: model mode -> write(model+0x2248, 20); camera mode ->
//         strcpy_s(buf, "カメラ・照明"@0x52BCB8) + write(buf,20)
//   boneCount(4) [0 in camera mode]
//   per bone key: name(15) frame(4) pos(12) quat(16) interp 4x16B
//     (ch0 = u16 x1 + u16 3939-marker + 3 dwords, ch1..3 = 4 dwords; the
//      dword writes overlap the transposed control-byte table exactly as
//      the original - the loader takes the low byte of each)
//   morphCount(4); per morph key: name(15) frame(4) value(4)
//   cameraCount(4); per camera key: frame(4) +0xC(4) +0x10..+0x24(6x4)
//     interp 6x(1B@+0x28+i, 1B@+0x34+i, 1B@+0x2E+i, 1B@+0x3A+i) +0x44(4)
//     view byte (+0x40 != 0)
//   lightCount(4); per light key: frame(4) +0x18/+0x1C/+0x20/+0xC/+0x10/
//     +0x14 (6x4)
//   shadowCount(4); per shadow key: frame(4) +0xC(1) +0x10(4)
//   ikCount(4) [0 in camera mode]; per display key: frame(4), visible(1),
//     model.ikChainCount(4), then each IkChain's bone name(20) and the
//     corresponding DisplayKey::ikStates enabled byte(1)
//   _close.  Open failure -> JP/EN "Cannot save file:%d" box (title "").
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mmd_app.hpp"

namespace mikudancestudio {
namespace {

unsigned char* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

const char kCameraLightName[] =  // 0x52BCB8 "カメラ・照明" (byte-exact)
    "\x83\x4a\x83\x81\x83\x89\x81\x45\x8f\xc6\x96\xbe";
const char kJpCannotSave[] =  // 0x52BCE4
    "\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xaa\x95\xdb\x91\xb6\x82\xc5\x82"
    "\xab\x82\xdc\x82\xb9\x82\xf1:%d";

int Wr(int fd, const void* p, unsigned int n) { return _write(fd, p, n); }

}  // namespace

// ---- VA 0x00419370 --------------------------------------------------------
void SaveVmdFile(const wchar_t* path) {
    MMDApp* app = g_Block;
    if (app == nullptr) return;
    auto& s = *app;

    const bool cameraMode = s.state.optflag[0] != 0;
    unsigned char* const model = ActiveModel(app);

    // ---- pass 1: counts + minimum frame ---------------------------------
    std::uint32_t minFrame = 0xFFFFFFFF;
    unsigned int boneCnt = 0, morphCnt = 0, ikCnt = 0;
    unsigned int camCnt = 0, lightCnt = 0, shadowCnt = 0;

    if (!cameraMode) {
        const mdl::BoneKey* keys = mdl::BoneKeys(model);
        for (unsigned int i = 0; i < static_cast<unsigned int>(mdl::kBoneKeyCapacity); ++i) {
            if (keys[i].allocated != 0) {
                ++boneCnt;
                const std::uint32_t f = keys[i].frame;
                if (f < minFrame) minFrame = f;
            }
        }
        const mdl::MorphKey* mkeys = mdl::MorphKeys(model);
        for (unsigned int i = 0; i < 20000; ++i) {
            if (mkeys[i].allocated != 0) {
                ++morphCnt;
                const std::uint32_t f = mkeys[i].frame;
                if (f < minFrame) minFrame = f;
            }
        }
        const mdl::DisplayKey* ikeys = mdl::DisplayKeys(model);
        for (unsigned int i = 0; i < 1000; ++i) {
            if (ikeys[i].allocated != 0) {
                ++ikCnt;
                const std::uint32_t f = ikeys[i].frame;
                if (f < minFrame) minFrame = f;
            }
        }
    } else {
        const mdl::CameraKey* cam = app->CameraKeys();
        for (unsigned int i = 0; i < 10000; ++i) {
            if (cam[i].selected != 0) {
                ++camCnt;
                const std::uint32_t f = cam[i].frame;
                if (f < minFrame) minFrame = f;
            }
        }
        const mdl::LightKey* light = app->LightKeys();
        for (unsigned int i = 0; i < 10000; ++i) {
            if (light[i].selected != 0) {
                ++lightCnt;
                const std::uint32_t f = light[i].frame;
                if (f < minFrame) minFrame = f;
            }
        }
        const mdl::SelfShadowKey* shadow = app->ShadowKeys();
        for (unsigned int i = 0; i < 10000; ++i) {
            if (shadow[i].selected != 0) {
                ++shadowCnt;
                const std::uint32_t f = shadow[i].frame;
                if (f < minFrame) minFrame = f;
            }
        }
    }

    // ---- open ------------------------------------------------------------
    int fd;
    const errno_t openError = _wsopen_s(&fd, path, 0x8301, 0x40, 0x80);
    if (openError != 0) {
        char text[256];
        if (s.state.englishUI != 0)
            sprintf_s(text, 0x100, "Cannot save file:%d", openError);
        else
            sprintf_s(text, 0x100, kJpCannotSave, openError);
        MessageBoxA(static_cast<HWND>(s.Hwnd()), text, "", 0);
        return;
    }

    char buf[256];
    sprintf_s(buf, 0x100, "%s", "Vocaloid Motion Data 0002");
    Wr(fd, buf, 30);                                             // 0x41981E
    if (!cameraMode) {
        Wr(fd, mdl::Mdl(model)->name, 20);                       // 0x419875
    } else {
        strcpy_s(buf, 0x100, kCameraLightName);
        Wr(fd, buf, 20);                                         // 0x41984F
    }

    // x64 sub_7FF7CB48C450：相机/照明模式（app+808 != 0）下完全不碰 model
    // 指针——模型名取常量"カメラ・照明"，bone/morph/IK 三段直接写 0 计数
    // 并跳过循环（计数全来自全局键表 app+976/984/992）。这些 model 侧的
    // 计数与表指针只在模型模式读取；相机模式下 model 可能为空。
    int boneCount = 0, morphCount = 0, ikChainCount = 0;
    mikudancestudio::mdl::BoneRecord* bones = nullptr;
    mikudancestudio::mdl::MorphRecord* morphs = nullptr;
    mikudancestudio::mdl::IkChain* ikChains = nullptr;
    if (!cameraMode) {
        auto* const rec = mdl::Mdl(model);
        boneCount = rec->boneCount;
        morphCount = rec->morphCount;
        ikChainCount = rec->ikChainCount;
        bones = mikudancestudio::mdl::Bones(model);
        morphs = mikudancestudio::mdl::Morphs(model);
        ikChains = mikudancestudio::mdl::IkChains(model);
    }

    std::uint32_t outDword;
    // ---- bone keys --------------------------------------------------------
    outDword = cameraMode ? 0 : boneCnt;
    Wr(fd, &outDword, 4);                                             // 0x419889
    if (!cameraMode) {
        const mdl::BoneKey* keys = mdl::BoneKeys(model);
        for (unsigned int idx = 0; idx < static_cast<unsigned int>(mdl::kBoneKeyCapacity); ++idx) {
            const mdl::BoneKey& rec = keys[idx];
            if (rec.allocated == 0) continue;
            unsigned int boneIdx = idx;                          // 0x4198E0
            while (boneIdx >= static_cast<unsigned int>(boneCount))
                boneIdx = keys[boneIdx].previous;
            Wr(fd, bones[boneIdx].name, 15);                    // 0x419904
            outDword = rec.frame - minFrame;
            Wr(fd, &outDword, 4);
            Wr(fd, rec.position, sizeof rec.position);
            Wr(fd, rec.rotation, sizeof rec.rotation);
            // interpolation: 4 channels, transposed control-byte table
            // (+0xC x1 / +0x10 y1 / +0x14 x2 / +0x18 y2, one byte each per
            // channel); ch0 carries the 3939 VMD2 marker as the second u16
            for (int ch = 0; ch < 4; ++ch) {
                if (ch == 0) {                                   // 0x419AA5..
                    Wr(fd, rec.interpolation, 2);
                    const unsigned short marker =
                        rec.physicsDisabled != 0 ? 0x0F63 : 0;
                    Wr(fd, &marker, 2);
                } else {                                         // 0x419AE3
                    Wr(fd, rec.interpolation + ch, 4);
                }
                Wr(fd, rec.interpolation + 4 + ch, 4);
                Wr(fd, rec.interpolation + 8 + ch, 4);
                Wr(fd, rec.interpolation + 12 + ch, 4);
            }
        }
    }

    // ---- morph keys -------------------------------------------------------
    outDword = cameraMode ? 0 : morphCnt;
    Wr(fd, &outDword, 4);                                             // 0x419B8F
    if (!cameraMode) {
        const mdl::MorphKey* mkeys = mdl::MorphKeys(model);
        for (unsigned int idx = 0; idx < 20000; ++idx) {
            const mdl::MorphKey& rec = mkeys[idx];
            if (rec.allocated == 0) continue;
            unsigned int morphIdx = idx;                         // 0x419B90..
            while (morphIdx >= static_cast<unsigned int>(morphCount))
                morphIdx = mkeys[morphIdx].previous;
            Wr(fd, morphs[morphIdx].name, 15);                  // 0x419C18
            outDword = rec.frame - minFrame;
            Wr(fd, &outDword, 4);
            Wr(fd, &rec.value, sizeof rec.value);
        }
    }

    // ---- camera keys ------------------------------------------------------
    outDword = camCnt;
    Wr(fd, &outDword, 4);                                             // 0x419E60
    if (camCnt != 0) {
        const mdl::CameraKey* cam = app->CameraKeys();
        for (unsigned int i = 0; i < 10000; ++i) {
            const mdl::CameraKey& rec = cam[i];
            if (rec.selected == 0) continue;
            outDword = rec.frame - minFrame;
            Wr(fd, &outDword, 4);
            Wr(fd, &rec.distance, sizeof rec.distance);
            Wr(fd, rec.eye, sizeof rec.eye);
            Wr(fd, rec.target, sizeof rec.target);
            for (int j = 0; j < 6; ++j) {                        // 0x419DAA..
                // was: inner `i`, shadowing the outer record-index i
                Wr(fd, &rec.interpolation[0][j], 1);
                Wr(fd, &rec.interpolation[2][j], 1);
                Wr(fd, &rec.interpolation[1][j], 1);
                Wr(fd, &rec.interpolation[3][j], 1);
            }
            Wr(fd, &rec.fov, sizeof rec.fov);
            const unsigned char view = rec.perspective != 0 ? 1 : 0;
            Wr(fd, &view, 1);
        }
    }

    // ---- light keys -------------------------------------------------------
    outDword = lightCnt;
    Wr(fd, &outDword, 4);                                             // 0x419F4D
    if (lightCnt != 0) {
        const mdl::LightKey* light = app->LightKeys();
        for (unsigned int i = 0; i < 10000; ++i) {
            const mdl::LightKey& rec = light[i];
            if (rec.selected == 0) continue;
            outDword = rec.frame - minFrame;
            Wr(fd, &outDword, 4);
            Wr(fd, rec.color, sizeof rec.color);
            Wr(fd, rec.direction, sizeof rec.direction);
        }
    }

    // ---- self-shadow keys -------------------------------------------------
    outDword = shadowCnt;
    Wr(fd, &outDword, 4);                                             // 0x419FD6
    if (shadowCnt != 0) {
        const mdl::SelfShadowKey* shadow = app->ShadowKeys();
        for (unsigned int i = 0; i < 10000; ++i) {
            const mdl::SelfShadowKey& rec = shadow[i];
            if (rec.selected == 0) continue;
            outDword = rec.frame - minFrame;
            Wr(fd, &outDword, 4);
            Wr(fd, &rec.mode, sizeof rec.mode);
            Wr(fd, &rec.distance, sizeof rec.distance);
        }
    }

    // ---- IK / display master keys ----------------------------------------
    outDword = cameraMode ? 0 : ikCnt;
    Wr(fd, &outDword, 4);                                             // 0x41A17B..
    if (!cameraMode && outDword != 0) {
        const mdl::DisplayKey* ikeys = mdl::DisplayKeys(model);
        for (unsigned int i = 0; i < 1000; ++i) {
            const mdl::DisplayKey& rec = ikeys[i];
            if (rec.allocated == 0) continue;
            outDword = rec.frame - minFrame;
            Wr(fd, &outDword, 4);
            const unsigned char visible = rec.visible != 0 ? 1 : 0;
            Wr(fd, &visible, 1);
            outDword = static_cast<std::uint32_t>(ikChainCount);
            Wr(fd, &outDword, 4);
            for (int j = 0; j < ikChainCount; ++j) {             // 0x41A0F0..
                // was: inner `i`, shadowing the outer record-index i
                const int boneIdx = ikChains[j].boneIndex;
                Wr(fd, bones[boneIdx].name, 20);
                // 0x41A11A: the per-IK display byte array lives in a separate
                // heap buffer pointed to by the record's +0x10 slot (written
                // through the same pointer by the registrar 0x49F8C0) - the
                // on bit is (*(u8**)(rec+0x10))[i], NOT the inline record
                // bytes.  Reading inline made chains >= 4 read the mark byte
                // and the padding/pair-pointer bytes instead (allocation
                // garbage on a fresh load) - toe-IK chains came out 0.
                const unsigned char on =
                    mdl::IkStates(rec)[j] != 0
                        ? 1 : 0;
                Wr(fd, &on, 1);
            }
        }
    }

    _close(fd);                                                  // 0x41A17B
}

}  // namespace mikudancestudio
