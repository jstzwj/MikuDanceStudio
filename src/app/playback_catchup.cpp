// ===========================================================================
// VA 0x0046EEE0..0x0046F575 - playback / frame-step catch-up section of the
// FrameDriver (original: sub_46B090)
// ===========================================================================
// Field-exact port of the timeline->physics cursor machinery.  State (all
// verified from the live disassembly):
//   app+0x9E64C  physics cursor, seconds (float).  Advanced only here, in
//                1/60 steps (dbl_52EA08 = 1/60), then clamped to the target.
//   app+0x9E654  playback start time (frameA / 30.0, dbl_52BA68 = 30.0);
//                written by the play/seek handlers (CommandDispatch 408 at
//                0x487510/0x487536, FrameDriver section 7 at 0x479CF9, loop
//                restart at 0x46F32A) - NOT advanced during playback.
//   app+0x9E658  playback end time (frameB / 30.0).
//   app+0x9EDA8/AC  T0: wall-clock anchor (64-bit timeGetTime pair) reset at
//                play start / loop restart; the playing target is
//                9E654 + (TimeNow - T0) * MilliToSec.
//   app+0x9ED94  per-pass counter N: incremented at 0x46EFB7 every
//                FrameDriver pass, reset to 0 by section 7 (0x479D5A), so
//                N >= 1 whenever the frame-step target formula runs.
//   app+0x330    "frame advanced" master gate (set by play/seek handlers).
//   app+0x9ED90  frame-step mode flag (1 = paused stepping).
//
// Flow (0x46EEE0..0x46F575):
//   1. frame counter display: " %d" or, when 0xA0B4C (English/recording
//      display) is set, " %dframe recording(until %dframe)"; the shown
//      frame is frameA + (N*30)/measuredFps (0xA0B08).  Skipped when
//      0xA0D61 is set; N += 1 either way (0x46EFB7).
//   2. 0x46EFBE: app+0xA0D6C == 0 -> skip the whole section (jump to the
//      post-physics 0x46FEC4; the same gate also covers the accessory and
//      physics blocks downstream).
//   3. 0x46EFE8: app+0x330 == 0 -> nothing to advance, jump to 0x46F575.
//   4. 0x46EFEE: frame-step mode (0x9ED90 != 0):
//        var_14C8 = 1/fps (dead bookkeeping, never read);
//        target   = (frameA + 30.0/fps) / (N*30.0)          (0x46F011..54)
//        gate     : (uint)(int)(frameA + 30.0/fps) < (uint)(frameB+1)
//                   -> BLOCK 1 (0x46F12C), else the recording wait (0x46F070).
//   5. playing (0x9ED90 == 0): target = 9E654 + elapsed (0x46F264);
//      end(0x9E658) >= target -> BLOCK 2 (0x46F3D6), else the past-end
//      stop/loop path (0x46F2AF).
//   BLOCK 1 (0x46F12C..0x46F25B): cursor += 1/60; while target > cursor:
//     sub_4175A0(app, 1) [timeline advance] - ordered per-model pass
//     [morph 0x4970B0 + SetPhysicsMode(0, models, count)] - kinematic sync
//     0x4B22F0 in REVERSE slot order - stepSimulation(1/60, 10, 1/60) -
//     cursor += 1/60.  No selection skip.  Then cursor = target.
//   BLOCK 2 (0x46F3D6..0x46F562): same, but the ordered pass and the
//     FORWARD-order kinematic sync SKIP the selected model
//     (var_14A1 && app+0x2F8 == 0 && app+0x910 == slot).  Then
//     cursor = target and sub_4175A0(app, 0) (0x46F56C).
//   Past end (0x46F2AF): cursor = target (beyond end, verbatim); audio stop
//     pair 0x4C2680/0x4C2760 when 0xA06CC; loop (0x341): reread the frame
//     edit 0x199 -> atol -> 9E654 = frame/30, count = app+0xA0670,
//     cursor = 9E654, 0x433A40, audio reseek, T0 = now, sub_4175A0(0);
//     stop: 0x330 = 0, 0x4341E0, BM_SETCHECK(0) on item 0x198, and the
//     final sub_4175A0(0) is SKIPPED (jump straight to 0x46F575).
//
// FPU condition-code translation (exact):
//   "test ah,41h / jnz skip"  = skip unless target > t1 (NaN skips too) ->
//                               plain float > in C++.
//   "test ah,5 / jp  block2"  = end >= target (NaN also jumps) ->
//                               !(end < target).
//   "test ah,5 / jnp loop"    = continue while cursor < target (NaN exits)
//                               -> plain float < in C++.
//   1/60 additions: x86 runs them in double and rounds to float on store
//   (fstp dword); the x64 twin (sub_7FF7CB4474F0 region 0x7FF7CB44BA81
//   etc.) is a plain addss against the .rdata float 0x3C888889 - the
//   port follows the x64 single-precision form (behavior basis).
//
// The var_14A1 local ("selection active") is threaded in from FrameDriver
// where the original computes it (0x46DCA6..0x46DCCD): zeroed, then filled
// by the app callback at 0xA03D4 when 0x9ED90 == 0 && byte 0xA03B8 != 0.
//
// Port scope notes (docs/ARCHITECTURE.md section 8):
//   - sub_4175A0 (PlaybackPoseAdvance) is fully ported in
//     src/app/timeline_advance.cpp (per-model AdvanceModelKeyframes + camera/clip/
//     shadow/light/accessory key tracks); verified equivalent by the
//     AllStar play/stop A/B (post-stop kinematic-pose hashes identical
//     across every model slot).
//     Its second parameter is an integer flag (push 1 / push 0, compared
//     as a byte at 0x4175D7 - IDA's float prototype is wrong: the stack
//     slot is merely reused as a double scratch later in the function).
//   - The phase-15 null guards were reclaimed in phase 19: the Bullet
//     world and the app+0xA06C0 capture object are created by the ported
//     init path (SceneConstruct at WM_CREATE / InitMainWindowAndD3D's
//     0x408EA0 zeroing of the 0x6C object) before the message loop can
//     run the FrameDriver, matching the original's direct dereferences.
//     The recording wait itself stays unreachable until the frame-range
//     dialog (0x40F2F0) arms frame-step mode.
//   - flt_52B9F0 = 4294967296.0f (2^32) used as the unsigned-int-to-double
//     fixup; dbl_52BA68 = 30.0; dbl_52EA08 = 1/60; flt_52EA00 = 1/60f.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "btBulletDynamicsCommon.h"

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

// fild dword + "test/jge/fadd flt_52B9F0" (x86) / zero-extended
// cvtsi2ss r64 (x64, e.g. 0x7FF7CB44B96B): signed dword reinterpreted as
// unsigned before the float conversion.
float UnsigFlt(std::int32_t v) {
    return static_cast<float>(static_cast<std::uint32_t>(v));
}

inline float& Cursor(MMDApp* app) {
    return app->PlaybackCursorSeconds();
}

btDiscreteDynamicsWorld* PlayWorld(MMDApp* app) {
    PhysicsScene* scene = app->Physics();
    if (scene == nullptr)
        return nullptr;
    return scene->world; // slot 64 / 0x40
}

void StepWorld(btDiscreteDynamicsWorld* world) {
#ifdef MIKUDANCESTUDIO_DIAG
    if (getenv("MIKUDANCESTUDIO_TRACE_STEPS") != nullptr)
        fprintf(stderr, "CATCHUP step (cursor behind target)\n");
#endif
    world->stepSimulation(1.0f / 60.0f, 10, 1.0f / 60.0f); // 0x46F206
}

// Cursor += 1/60.  x64: addss against the .rdata float 0x3C888889
// (xmm10 slot 0x7FF7CB552D34) - one single-precision rounding.
void BumpCursor(MMDApp* app) {
    Cursor(app) += 1.0f / 60.0f;
}

// 0x46F171..0x46F1DD (block 1) / 0x46F41B..0x46F4BE (block 2): per-model
// morph application + pose source, ordered by model+0x2D7D.  Block 2 adds
// the selected-model skip (var_14A1 && 2F8==0 && 910==slot).
void OrderedMorphPhysicsPass(MMDApp* app, int count, bool selSkip) {
    auto& s = *app;
    mdl::ModelRecord** models = s.ModelSlots();
    // x64 pump sub_7FF7CB4474F0: both ordering passes run order and slot
    // walks to 255 (block 1 cmp edi/rbx, 0FFh at 0x7FF7CB44BB0E/0x7FF7CB44BADA;
    // block 2 at 0x7FF7CB44BDDA/0x7FF7CB44BD7D; comboSelIndex2 = model+0x3109).
    for (int order = 0; order < kModelSlotCount; ++order) {
        for (int j = 0; j < kModelSlotCount; ++j) {
            mdl::ModelRecord* model = models[j];
            if (model == nullptr || model->comboSelIndex2 != order)
                continue;
            if (selSkip && s.CameraMode() == 0 && s.SelectedModelSlot() == j)
                continue;            // 0x46F471
            ModelApplyMorphs(model); // 0x46F1AE
            SetPhysicsMode(model, 0, models, count);
        }
    }
}

// 0x46F1DF..0x46F204 (block 1, forward) / 0x46F4C4..0x46F50B (block 2,
// forward, with the selected-model skip).
void KinematicSyncPass(MMDApp* app, bool reverse, bool selSkip) {
    auto& s = *app;
    mdl::ModelRecord** models = s.ModelSlots();
    // x64: reverse walk is a 255 count-down from slot 254
    // (mov edi, 0FFh ... dec rdi at 0x7FF7CB44BB16); forward-with-skip walk
    // counts down 255 from slot 0 (mov esi, 0FFh ... dec rsi at 0x7FF7CB44BDF1).
    const int begin = reverse ? kModelSlotCount - 1 : 0;
    const int end = reverse ? -1 : kModelSlotCount;
    const int step = reverse ? -1 : 1;
    for (int j = begin; j != end; j += step) {
        mdl::ModelRecord* model = models[j];
        if (model == nullptr)
            continue;
        if (selSkip && s.CameraMode() == 0 && s.SelectedModelSlot() == j)
            continue;              // 0x46F4F1
        ModelKinematicSync(model); // 0x46F1F7
    }
}

// Recording wait (0x46F070..0x46F127): capture one frame through the
// app+0xA06C0 +0x68 interface (vtable slot 4, E_FAIL aborts), pump messages,
// sleep 1ms, until the byte behind app+0x9EDD4 turns non-zero; then sleep
// 500ms and finish with 0x464A00.  Returns true when the section should be
// treated as finished via the 0x46F575 exit (always the case here).
void FrameStepBeyondEnd(MMDApp* app) {
    auto& s = *app;
    DShowRecorder* recorder = s.Recorder();
    IPushSource* push =
        recorder != nullptr ? static_cast<IPushSource*>(recorder->framePush) : nullptr;
    unsigned char* flagPtr = s.RecordingCompletionFlag();

    // The original guards BOTH capture calls with a null test on the
    // +0x68 interface (0x46F079 test/jz and 0x46F0A4 test/jz): with no
    // AVI-dump dialog opened the interface stays null and the calls are
    // skipped entirely.  Omitting the guards null-dereferences on the
    // first frame-step click past the end.
    auto capture = [&]() -> long { // 0x46F070/9B
#ifdef MIKUDANCESTUDIO_DIAG
        static int capCount = 0;
#endif
        if (push == nullptr)
            return 0;
        // IPushSource methods are STDMETHODCALLTYPE: `this` is the first
        // stack argument (NOT a cdecl call - the 8-byte cleanup mismatch
        // corrupted the caller's stack).
#ifdef MIKUDANCESTUDIO_DIAG
        if (getenv("MIKUDANCESTUDIO_TRACE_REC")) {
            if (++capCount <= 5 || capCount % 60 == 0) {
                FILE* tf = fopen(getenv("MIKUDANCESTUDIO_TRACE_REC"), "a");
                if (tf) {
                    fprintf(tf, "capture#%d push=%p flag=%d\n", capCount, static_cast<void*>(push),
                            (int)*flagPtr);
                    fclose(tf);
                }
            }
        }
#endif
        return push->GetStreamingState(flagPtr);
    };

    capture();
    while (*flagPtr == 0) {  // 0x46F08C
        if (push == nullptr) // 0x46F0A4 jz
            break;
        if (capture() == static_cast<long>(0x80004005)) // E_FAIL
            break;                                      // 0x46F0B1
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); // 0x46F0D8
            DispatchMessageA(&msg);
        }
        Sleep(1); // 0x46F102
    }
    Sleep(500);           // 0x46F115
    FinishAviRecord(app); // 0x46F122
}

// Past-end path while playing (0x46F2AF..0x46F3D1).
void PlayingBeyondEnd(MMDApp* app) {
    auto& s = *app;
    WaveAudioContext* audio = s.Audio(); // app+0xCC
    if (s.WaveEnabled() != 0) {
        CloseDataFile(audio);     // 0x46F2C4
        WaveStartPlayback(audio); // 0x4C2760
    }

    if (s.PlaybackLoopEnabled() != 0) {
        char text[8];
        GetWindowTextA(GetDlgItem(static_cast<HWND>(s.Hwnd()), panel::kPlayStartFrameEdit), text,
                       8);
        const std::int32_t frame = std::atol(text);
        // x64 0x7FF7CB44BC3E..0x7FF7CB44BC4B: cvtsi2ss of the
        // zero-extended frame, then divss by the 30.0f slot.
        s.PlaybackStartSeconds() = UnsigFlt(frame) / 30.0f;
        s.PlaybackPhysicsMode() = s.SavedPlaybackPhysicsMode();
        Cursor(app) = s.PlaybackStartSeconds();
        UpdateBoneFrames(app); // 0x433A40
        if (s.WaveEnabled() != 0 && s.AutomaticFrameAdvanceEnabled() == 0) {
            SetFrameNormalized(s.FrameNormalization());
            WaveSeekAndFeed(audio, static_cast<double>(s.PlaybackStartSeconds()));
        }
        s.PlaybackClockAnchorLow() = s.TimeNowLow();
        s.PlaybackClockAnchorHigh() = s.TimeNowHigh();
        PlaybackPoseAdvance(app, 0); // 0x46F56C
        return;
    }

    // stop playback (0x46F3A5..0x46F3D1); the final 0x4175A0(0) is skipped.
    s.PlaybackActive() = 0;
    StopPlayback(app); // 0x4341E0
    SendMessageA(GetDlgItem(static_cast<HWND>(s.Hwnd()), panel::kPlayButton), BM_SETCHECK, 0,
                 0); // 0x46F3CB
}

} // namespace

// Physics-cursor catch-up of the FrameDriver (0x46EEE0..0x46F575).
// selActive is the var_14A1 local computed in FrameDriver (0x46DCA6).
void PlaybackCatchup(MMDApp* app, unsigned char selActive) {
    auto& s = *app;
    const bool selSkip = selActive != 0;

    // ---- 1. frame counter display + N++ (0x46EEE0..0x46EFB7) ------------
    // x64 0x7FF7CB44B850..0x7FF7CB44B8A4, all single precision: N*30 as an
    // INTEGER (imul eax, 1Eh) then movd/cvtdq2ss, divss by the fps float,
    // addss the zero-extended frameA, cvttss2si r32.
    if (s.state.aviStereoOutput == 0) {
        const std::int32_t n = s.state.recordedFrameCount;
        const std::int32_t frameA = s.AviRecordStartFrame();
        const float fps = s.AviRecordFps();
        const int shown = static_cast<int>(static_cast<float>(n * 30) / fps + UnsigFlt(frameA));
        wchar_t buf[0x100];
        if (s.state.englishUI != 0) {
            swprintf_s(buf, 0x100, L" %dframe recording(until %dframe)", shown,
                       s.AviRecordEndFrame());
        } else {
            swprintf_s(buf, 0x100, L" %d", shown);
        }
        SetWindowTextW(s.RecordingWindow(), buf);
    }
    s.state.recordedFrameCount += 1; // 0x46EFB7

    // ---- 2./3. gates (0x46EFBE..0x46EFEE) --------------------------------
    if (s.state.messageSeen == 0)
        return; // -> 0x46FEC4 (post)
    // var_14C8 = app+0xA06BC unconditionally (0x46EFDE); overwritten with
    // 1/fps when the frame-advance byte AND the frame-step flag are both
    // set (0x46F00D, x87 double divide, one rounding at the store).  The
    // BLOCK loops below decrement it by 1/60 per substep (0x46F226 and the
    // BLOCK 2 twin) and the settle main step consumes the REMAINDER - see
    // g_CatchupDtBudget in globals.hpp.
    g_CatchupDtBudget = s.state.deltaTime; // 0xA06BC
    if (s.PlaybackActive() == 0)
        return; // -> 0x46F575

    const int count = s.PlaybackPhysicsMode();

    if (s.FrameStepPlayback() != 0) {
        // ---- 4. frame-step mode (0x46EFFB..0x46F25B) --------------------
        // var_14C8 = 1/fps: the frame's FULL dt budget, of which the BLOCK
        // loop substeps below consume their 1/60s and the settle main
        // step steps the remainder.  x64 0x7FF7CB44B940..0x7FF7CB44B956:
        // movss 1.0f then divss by the fps float - single precision.
        const std::int32_t n = s.state.recordedFrameCount;
        const std::int32_t frameA = s.AviRecordStartFrame();
        const float fps = s.AviRecordFps();
        g_CatchupDtBudget = 1.0f / fps; // 0x46F00D
        // x64 0x7FF7CB44B92E..0x7FF7CB44B984, all single precision:
        //   term   = ((float)N * 30.0f) / fps + (float)(u32)frameA
        //            [movd/cvtdq2ss N, mulss 30.0f, divss fps,
        //             cvtsi2ss zero-extended frameA, addss]
        //   gate   = (u32)(int64)term   [cvttss2si r64, low half]
        //   target = term / 30.0f       [divss]
        // I.e. the frame-step target advances N/fps SECONDS per pass (one
        // frame per pass at fps 30) - the port first read it as
        // (frameA+30/fps)/(N*30), which DECREASES with N and pinned the
        // recording cursor at 0.  NOTE: the display formula above
        // multiplies N*30 as INTEGERS first; this path multiplies as
        // float - keep the two apart.
        const float term = static_cast<float>(n) * 30.0f / fps + UnsigFlt(frameA);
        const float target = term / 30.0f; // 0x46F054
        const std::uint32_t gate = static_cast<std::uint32_t>(static_cast<std::int64_t>(term));
        const std::uint32_t limit = static_cast<std::uint32_t>(s.AviRecordEndFrame() + 1);
        if (gate < limit) { // 0x46F06A jb
            // BLOCK 1 (0x46F12C): catch-up loop, no selection skip.  The
            // block is gated on the physics-mode counter exactly like
            // BLOCK 2 (x86 `cmp [this+0xA0CC4],1 / jl` at 0x46F12C; x64
            // `cmp [r12+0xA1D4C],1 / jl` at 0x7FF7CB44BA6A, reached from
            // the frame-step gate's jb at 0x7FF7CB44B986) - the port
            // previously ran the loop unconditionally here.
            if (count >= 1) {
                BumpCursor(app);
                btDiscreteDynamicsWorld* world = PlayWorld(app);
                if (target > Cursor(app)) {
                    do {
                        PlaybackPoseAdvance(app, 1); // 0x46F16C
                        OrderedMorphPhysicsPass(app, count, false);
                        // 0x46F1DF: `lea 0x780(%ebx),%edi; add $0x4` -
                        // FORWARD slot order (block 2 at 0x46F4CA is
                        // forward too).
                        KinematicSyncPass(app, false, false);
                        StepWorld(world);
                        // 0x46F226 / x64 0x7FF7CB44BB64: the dt budget
                        // pays for the substep (subss 1/60f).
                        g_CatchupDtBudget -= 1.0f / 60.0f;
                        BumpCursor(app);
                    } while (Cursor(app) < target); // 0x46F255
                }
            }
            Cursor(app) = target; // 0x46F566
            // 0x46F566 falls THROUGH to 0x46F56C: the final
            // sub_4175A0(app, 0) runs for BLOCK 1 too (the frame-step
            // recording path) - omitting it desynchronised every recorded
            // frame's pose application from the original.
            PlaybackPoseAdvance(app, 0); // 0x46F56C
            return;                      // -> 0x46F575
        }
        FrameStepBeyondEnd(app); // 0x46F070
        return;                  // -> 0x46F575
    }

    // ---- 5. playing (0x46F264..) ------------------------------------------
    // MIKUDANCESTUDIO_TRACE_PLAY: per-substep bone dump for the first model, first
    // 40 catch-up substeps after play starts (diagnostic only; compiled
    // under MIKUDANCESTUDIO_DIAG).
#ifdef MIKUDANCESTUDIO_DIAG
    static int tracePlayLeft = -1;
    static int tracePassLeft = -1;
    if (const char* tp = getenv("MIKUDANCESTUDIO_TRACE_PLAY")) {
        if (tracePlayLeft < 0) {
            FILE* tf = fopen(tp, "a");
            if (tf) {
                fprintf(tf, "play-start end=%f cursor=%f count=%d\n", s.PlaybackEndSeconds(),
                        Cursor(app), count);
                fclose(tf);
            }
            tracePlayLeft = 400;
            tracePassLeft = 100;
            mdl::ModelRecord* model = s.ModelSlot(0);
            FILE* tf2 = fopen(tp, "a");
            if (tf2 && model != nullptr) {
                const std::int32_t boneCnt = model->boneCount;
                unsigned char* const active = model->boneTrackActive;
                std::uint32_t* const cursors = model->boneKeyCursors;
                mikudancestudio::mdl::BoneRecord* const bones = model->boneTable;
                const mdl::BoneKey* const keyList = model->boneKeys;
                fprintf(tf2, "loadstate boneCnt=%d\n", boneCnt);
                for (int b = 0; b < boneCnt && b < 12; ++b)
                    fprintf(tf2,
                            "bone%d active=%d cursor=%d quat=%08X "
                            "keyframe=%d nextframe=%d\n",
                            b, active ? active[b] : -1, cursors ? cursors[b] : -1,
                            *reinterpret_cast<std::uint32_t*>(&bones[b].rotQuat[0]),
                            (keyList && cursors) ? static_cast<int>(keyList[cursors[b]].frame) : -1,
                            (keyList && cursors && keyList[cursors[b]].next != 0)
                                ? static_cast<int>(keyList[keyList[cursors[b]].next].frame)
                                : -1);
                fclose(tf2);
            } else if (tf2) {
                fclose(tf2);
            }
        }
    }
#endif
    const std::uint64_t now = (static_cast<std::uint64_t>(s.TimeNowHigh()) << 32) | s.TimeNowLow();
    const std::uint64_t t0 = (static_cast<std::uint64_t>(s.PlaybackClockAnchorHigh()) << 32) |
                             s.PlaybackClockAnchorLow();
    // x64 0x7FF7CB44BB86..0x7FF7CB44BBB3, all single precision: the 64-bit
    // tick delta goes straight into a float (cvtsi2ss rax), then mulss by
    // MilliToSec and addss the start seconds.  The old port widened
    // everything to double and rounded once at the store.
    const std::int64_t elapsed = static_cast<std::int64_t>(now - t0);
    const float target = static_cast<float>(elapsed) * s.MilliToSec() + s.PlaybackStartSeconds();
    const float end = s.PlaybackEndSeconds();

#ifdef MIKUDANCESTUDIO_DIAG
    if (tracePassLeft > 0) {
        --tracePassLeft;
        if (FILE* tf = fopen(getenv("MIKUDANCESTUDIO_TRACE_PLAY"), "a")) {
            fprintf(tf,
                    "pass cursor=%.5f target=%.5f end=%.5f elapsed=%.3f "
                    "b330=%d sel=%d\n",
                    Cursor(app), target, end, static_cast<double>(elapsed), s.PlaybackActive(),
                    selSkip);
            fclose(tf);
        }
    }
#endif

    if (!(end < target)) { // 0x46F2A9 jp
        // BLOCK 2 (0x46F3D6): playback catch-up with selection skip.
        if (count >= 1) {
            BumpCursor(app);
            btDiscreteDynamicsWorld* world = PlayWorld(app);
            if (target > Cursor(app)) {
                do {
                    PlaybackPoseAdvance(app, 1); // 0x46F416
                    OrderedMorphPhysicsPass(app, count, selSkip);
                    KinematicSyncPass(app, false, selSkip);
                    StepWorld(world);
                    // BLOCK 2 twin of 0x46F226 (x64 0x7FF7CB44BE57):
                    // the substep is paid from the dt budget (subss 1/60f).
                    g_CatchupDtBudget -= 1.0f / 60.0f;
                    BumpCursor(app);
#ifdef MIKUDANCESTUDIO_DIAG
                    if (tracePlayLeft > 0 && getenv("MIKUDANCESTUDIO_TRACE_PLAY")) {
                        --tracePlayLeft;
                        mdl::ModelRecord* model = s.ModelSlot(0);
                        FILE* tf = fopen(getenv("MIKUDANCESTUDIO_TRACE_PLAY"), "a");
                        if (tf) {
                            if (model != nullptr) {
                                mikudancestudio::mdl::BoneRecord* bones = model->boneTable;
                                const std::int32_t boneCount =
                                    static_cast<std::int32_t>(model->boneCount);
                                fprintf(tf, "sub cursor=%.5f target=%.5f bones=", Cursor(app),
                                        target);
                                for (int bi = 0; bi < boneCount && bi < 8; ++bi)
                                    fprintf(
                                        tf, "%08X%08X%08X%08X,",
                                        *reinterpret_cast<std::uint32_t*>(&bones[bi].rotQuat[0]),
                                        *reinterpret_cast<std::uint32_t*>(&bones[bi].rotQuat[1]),
                                        *reinterpret_cast<std::uint32_t*>(&bones[bi].rotQuat[2]),
                                        *reinterpret_cast<std::uint32_t*>(&bones[bi].rotQuat[3]));
                                fputc('\n', tf);
                            }
                            fclose(tf);
                        }
                    }
#endif
                } while (Cursor(app) < target); // 0x46F55C
            }
        }
        Cursor(app) = target;        // 0x46F566
        PlaybackPoseAdvance(app, 0); // 0x46F56C
        return;
    }

    // past end (0x46F2AF)
    Cursor(app) = target;
    PlayingBeyondEnd(app);
}

} // namespace mikudancestudio
