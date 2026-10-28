// ===========================================================================
// VA 0x00412330 - ApplyGravityTrack  (0x7F1 bytes)  gravity-track apply + physics
//                                    dialog (hwnd @ app+0xA0CCC) refresh
// VA 0x00413120 - ApplyAccessoryTrack  (0x3B5 bytes)  accessory key-track apply
// ===========================================================================
// ApplyGravityTrack (called after VMD load, frame seek, physics-dialog edits and
// PMM load): walks the physical-gravity track at app+0x380 (36B records
// {+0 frame, +4 prev, +8 next, +0xC gravity magnitude, +0x10/+0x14/+0x18
// gravity dir xyz, +0x1C iterations, +0x20 mode dword}) to the frame at
// app+0x980 and writes app+0xA0CD4/9EDC8/9EDC4/9EDB8/9EDBC/9EDC0.  Exact
// hit / dead end copy the record; between keys the five values lerp with
// the unsigned 2^32 wrap correction (0x52B9F0 = 4294967296.0f) and the
// iteration count rounds (int)(t * delta) + prev.  When the physics dialog
// is open the controls refresh: "%3.2f" texts 709..712 (0x2C5..0x2C8),
// "%d" text 713 (0x2C9), trackbars 637..639 (0x27D..0x27F) at
// (int)(g * 100.0), check 731 (0x2DB) mirrors app+0xA0CD4, and control
// 713 is enabled iff app+0xA0CD4 != 0.
//
// ApplyAccessoryTrack (per accessory slot idx): walks the 60B records of the track
// at app+0x384[idx] to app+0x980 and copies into the accessory object at
// app+0x9DD70[idx]: visible byte +0x210 (rec+0xC), +0x49C (rec+0xD),
// floats +0x230/+0x234 (rec+0x10/+0x14), vec +0x220 (rec+0x28),
// +0x22C (rec+0x34), vec +0x214 (rec+0x1C), +0x4A0 (rec+0x38).  Only
// +0x34/+0x38 lerp between keys (the transparency-quirk pair); the rest
// copy from the previous record.  Port adds null guards the original gets
// from the 0x466D20 allocations.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/scene_timeline_bridge.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

constexpr float kWrap2p32 = 4294967296.0f; // 0x52B9F0

// applies the record fields of the gravity track into the live cluster
void ApplyGravityRecord(MMDApp* app, const mdl::GravityKey& key) {
    std::uint32_t noiseModeWord;
    std::memcpy(&noiseModeWord, &key.noiseEnabled, sizeof noiseModeWord);
    app->GravityNoiseModeWord() = noiseModeWord;
    app->state.gravityNoise = static_cast<std::uint32_t>(key.noise);
    app->state.gravityMagnitude = key.acceleration;
    app->state.gravityX = key.direction[0];
    app->state.gravityY = key.direction[1];
    app->state.gravityZ = key.direction[2];
}

// physics-dialog mirror of the live cluster (0x4123F2..0x412820)
void RefreshPhysicsDialog(MMDApp* app) {
    HWND dlg = app->GravitySettingDialog();
    if (dlg == nullptr)
        return;
    char text[0x32];
    sprintf_s(text, 0x32, "%3.2f",
              app->state.gravityMagnitude); // 0x52BA08
    SetWindowTextA(GetDlgItem(dlg, panel::kGravityAccelEdit), text);
    sprintf_s(text, 0x32, "%3.2f", app->state.gravityX);
    SetWindowTextA(GetDlgItem(dlg, panel::kGravityDirXEdit), text);
    sprintf_s(text, 0x32, "%3.2f", app->state.gravityY);
    SetWindowTextA(GetDlgItem(dlg, panel::kGravityDirYEdit), text);
    sprintf_s(text, 0x32, "%3.2f", app->state.gravityZ);
    SetWindowTextA(GetDlgItem(dlg, panel::kGravityDirZEdit), text);
    SendMessageA(GetDlgItem(dlg, panel::kGravityDirXSlider), TBM_SETPOS, 1, // 0x4124E0..
                 static_cast<LPARAM>(static_cast<int>(app->state.gravityX * 100.0)));
    SendMessageA(GetDlgItem(dlg, panel::kGravityDirYSlider), TBM_SETPOS, 1,
                 static_cast<LPARAM>(static_cast<int>(app->state.gravityY * 100.0)));
    SendMessageA(GetDlgItem(dlg, panel::kGravityDirZSlider), TBM_SETPOS, 1,
                 static_cast<LPARAM>(static_cast<int>(app->state.gravityZ * 100.0)));
    sprintf_s(text, 0x32, "%d", static_cast<int>(app->state.gravityNoise));
    SetWindowTextA(GetDlgItem(dlg, panel::kGravityNoiseEdit), text); // 0x52B9F4
    const BOOL mode = app->state.gravityNoiseEnabled != 0;
    SendMessageA(GetDlgItem(dlg, panel::kGravityNoiseCheckbox), BM_SETCHECK, mode, 0);
    EnableWindow(GetDlgItem(dlg, panel::kGravityNoiseEdit), mode); // 0x412B03
}

} // namespace

// ---- VA 0x00412330 --------------------------------------------------------
void ApplyGravityTrack(MMDApp* app) { // VA 0x00412330
    const std::uint32_t frame = app->state.currentFrame;
    mdl::GravityKey* const track = app->GravityKeys();
    if (track == nullptr)
        return; // guard: 0x466D20 allocates the track

    const auto recFrame = [&track](unsigned int idx) -> std::uint32_t& { return track[idx].frame; };

    unsigned int idx = 0;
    if (recFrame(0) < frame) { // 0x412390..
        for (;;) {
            const unsigned int next = track[idx].next;
            if (next == 0) {
                ApplyGravityRecord(app, track[idx]); // dead end
                SeekPortableSceneGravity(app, static_cast<int>(frame));
                RefreshPhysicsDialog(app);
                return;
            }
            idx = next;
            if (recFrame(idx) >= frame)
                break;
        }
    }

    const mdl::GravityKey& rec = track[idx];
    if (recFrame(idx) == frame) { // exact hit
        ApplyGravityRecord(app, rec);
        SeekPortableSceneGravity(app, static_cast<int>(frame));
        RefreshPhysicsDialog(app);
        return;
    }

    // between keys: lerp against the previous record (0x412860..0x4128E0)
    const mdl::GravityKey& prev = track[rec.previous];
    float num = static_cast<float>(static_cast<std::int32_t>(frame - recFrame(rec.previous)));
    if (num < 0)
        num += kWrap2p32;
    float den =
        static_cast<float>(static_cast<std::int32_t>(recFrame(idx) - recFrame(rec.previous)));
    if (den < 0)
        den += kWrap2p32;
    const float t = num / den;

    const int dIter = rec.noise - prev.noise;
    app->state.gravityNoise =
        static_cast<std::uint32_t>(static_cast<int>(t * static_cast<float>(dIter)) + prev.noise);
    app->state.gravityMagnitude = (rec.acceleration - prev.acceleration) * t + prev.acceleration;
    app->state.gravityX = (rec.direction[0] - prev.direction[0]) * t + prev.direction[0];
    app->state.gravityY = (rec.direction[1] - prev.direction[1]) * t + prev.direction[1];
    app->state.gravityZ = (rec.direction[2] - prev.direction[2]) * t + prev.direction[2];
    std::uint32_t noiseModeWord;
    std::memcpy(&noiseModeWord, &rec.noiseEnabled, sizeof noiseModeWord);
    app->GravityNoiseModeWord() = noiseModeWord;
    SeekPortableSceneGravity(app, static_cast<int>(frame));
    RefreshPhysicsDialog(app);
}

// ---- VA 0x00413120 --------------------------------------------------------
// Ported in src/window/accessory_paste.cpp (parallel-port winner: verbatim
// prev-record copy in the between-keys branch, per 0x4131AC..0x413224).

} // namespace mikudancestudio
