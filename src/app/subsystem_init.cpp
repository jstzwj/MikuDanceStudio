// ===========================================================================
// Residual subsystem-init / wave-seek ports (phase scaffolding cleanup)
// ===========================================================================
// VA 0x004C2450 - InitAudioContext : zeroing ctor of the 0x25C audio/timeline ctx
// VA 0x00401360 - PhysicsSceneInit  : zeroing ctor of the 0x48 physics-scene wrapper
// VA 0x004C34A0 - WaveSeekAndFeed  : wave seek + feed-thread spawn (restart path)
//
// Not re-ported here (already covered elsewhere):
//   0x004C2760 - WaveStartPlayback, real body in src/media/wave_audio.cpp
//                (its former no-op twin in src/app/late_ports.cpp has since
//                been deleted; wave_audio.cpp is the only definition).
//   0x0044D610 - RebuildModelModePanel ("PostModelReload3"), real body in
//                src/window/ui_model_reload.cpp; two field deviations found
//                against the disassembly are fixed in that file.
//
// NOTE: the no-op InitAudioContext/PhysicsSceneInit/WaveSeekAndFeed twins formerly
// kept in src/app/late_ports.cpp are gone; this TU holds the only (full
// port) definitions, so there is no duplicate-symbol hazard any more.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <mmsystem.h>   // WAVEFORMATEX for dsound.h
#include <dsound.h>

#include <cstdint>
#include <process.h>   // _beginthread

#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/wave_audio_context.hpp"

namespace mikudancestudio {
namespace {

// VA 0x004C2CE0 - DirectSound feed-thread proc.  Streaming loop, verbatim
// from the disassembly:
//   1. Lock(0, nAvgBytesPerSec) once (Restore + retry on
//      DSERR_BUFFERLOST); failure exits.  WaveStreamFeed(p1, l1, p2, l2),
//      Unlock, Play(0, 0, DSBPLAY_LOOPING), wrapped = 0.
//   2. while (audio->ReadStopFlag() != 1):
//        Lock(writeOff, avgBytes) (+restore retry); failure exits.
//        if (l1 == 0 && l2 == 0) { Unlock(p1, 0, p2, 0); exit; }   // EOF
//        WaveStreamFeed(p1, l1, p2, l2); Unlock.
//        if (failCount > 2) { Stop(); exit; }                      // 0x4C2E67
//        play-cursor chase: do { GetCurrentPosition(&cur, 0);
//          wrapped ? (cur > last ? keep : wrapped = 0)
//                  : (cur > writeOff ? leave the wait);
//          last = cur; for (i < 10) { Sleep(10); if (audio->ReadStopFlag() == 1) break; }
//        } while (audio->ReadStopFlag() != 1);
//        advance: l2 != 0 ? (writeOff = l2, wrapped = 1)
//                         : (writeOff += l1; writeOff == bufBytes
//                            ? (writeOff = 0, wrapped = 1) : wrapped = 0);
//   3. exit epilogue: Stop(); Release(); ctx+0x14 = 0; audio->WriteStopFlag(2);
//      _endthread().  (The fail-count break calls Stop a second time via
//      the shared epilogue - harmless, kept as the original lays it out.)
void __cdecl WaveFeedThread(void* ctx) {
    auto* audio = static_cast<WaveAudioContext*>(ctx);
    IDirectSoundBuffer* buffer = audio->streamingBuffer;
    const DWORD avgBytes = audio->format.nAvgBytesPerSec;
    const std::int32_t bufBytes = audio->bufferBytes;
    auto lock = [&](DWORD off, void** p1, DWORD* l1, void** p2,
                    DWORD* l2) -> HRESULT {
        HRESULT hr = buffer->Lock(off, avgBytes, p1, l1, p2, l2, 0);
        if (hr == DSERR_BUFFERLOST) {                          // 0x4C2D14
            buffer->Restore();                                 // 0x4C2D1F
            hr = buffer->Lock(off, avgBytes, p1, l1, p2, l2, 0);
        }
        return hr;
    };
    auto exitThread = [&]() {
        buffer->Stop();                                        // 0x4C2F36
        buffer->Release();                                     // 0x4C2F4A
        audio->streamingBuffer = nullptr;                       // 0x4C2F4C
        audio->WriteStopFlag(2);                                              // 0x4C2F53
        _endthread();
    };

    // ---- 1. initial fill + Play ------------------------------------------
    void* p1 = nullptr;
    void* p2 = nullptr;
    DWORD l1 = 0;
    DWORD l2 = 0;
    if (lock(0, &p1, &l1, &p2, &l2) >= 0) {                    // 0x4C2D0D
        // x64 0x7FF7CB4FADFA..0x7FF7CB4FAE1A：两段读任一失败，本回合只
        // 自增一次 failureCount（计数在调用方，不在 feed 里）。
        if (!WaveStreamFeed(ctx, p1, static_cast<int>(l1), p2,
                            static_cast<int>(l2)))              // 0x4C2D69
            ++audio->failureCount;                              // 0x7FF7CB4FAE1A
        buffer->Unlock(p1, l1, p2, l2);                        // 0x4C2D8B
        buffer->Play(0, 0, DSBPLAY_LOOPING);                   // 0x4C2D9C
        bool wrapped = false;                                  // 0x4C2DA7
        if (audio->ReadStopFlag() != 1) {
            DWORD writeOff = l1;  // v3 seeded with the first lock length
            // ---- 2. streaming loop ----------------------------------------
            for (;;) {
                if (lock(writeOff, &p1, &l1, &p2, &l2) < 0)    // 0x4C2DD8
                    break;
                if (l1 == 0 && l2 == 0) {                      // 0x4C2E1A
                    buffer->Unlock(p1, 0, p2, 0);              // 0x4C2F24
                    break;
                }
                if (!WaveStreamFeed(ctx, p1, static_cast<int>(l1), p2,
                                    static_cast<int>(l2)))      // 0x4C2E3C
                    ++audio->failureCount;                      // 0x7FF7CB4FAF35
                buffer->Unlock(p1, l1, p2, l2);                // 0x4C2E5E
                if (audio->failureCount > 2) {                  // 0x4C2E67
                    buffer->Stop();                            // 0x4C2F31
                    break;
                }
                // play-cursor chase (0x4C2E6D..0x4C2EC4)
                DWORD last = 0;
                do {
                    DWORD cur = 0;
                    buffer->GetCurrentPosition(&cur, nullptr); // 0x4C2E80
                    if (wrapped) {                             // 0x4C2E93
                        if (cur <= last)
                            wrapped = false;
                    } else if (cur > writeOff) {
                        break;                                 // 0x4C2EC6
                    }
                    last = cur;
                    for (int i = 0; i < 10; ++i) {             // 0x4C2E9E
                        Sleep(10);
                        if (audio->ReadStopFlag() == 1)
                            break;
                    }
                } while (audio->ReadStopFlag() != 1);
                // advance the write offset (0x4C2EC6..0x4C2F05)
                if (audio->ReadStopFlag() != 1) {
                    if (l2 != 0) {
                        writeOff = l2;
                        wrapped = true;
                    } else {
                        writeOff += l1;
                        if (writeOff == static_cast<DWORD>(bufBytes)) {
                            writeOff = 0;
                            wrapped = true;
                        } else {
                            wrapped = false;
                        }
                    }
                    if (audio->ReadStopFlag() != 1)
                        continue;
                }
                break;
            }
        }
    }
    exitThread();
}

}  // namespace

// ---------------------------------------------------------------------------
// VA 0x004C2450 - InitAudioContext(this): partial zeroing of the 0x25C audio/
// timeline context right after the 0x47A648 allocation.  Original op order
// kept: the wave path's first wchar, the English-UI flag copy, both waveform
// array pointers, and the trailing dword at +0x258.  (The caller already
// memset the whole 0x25C block to zero, so these writes are value-neutral -
// ported verbatim anyway.)
// ---------------------------------------------------------------------------
void InitAudioContext(void* sub) {
    auto* audio = static_cast<WaveAudioContext*>(sub);
    audio->path[0] = L'\0';                                    // 0x4C2452
    audio->englishUI = 0;                                      // 0x4C2456
    audio->waveformMax = nullptr;                              // 0x4C245C
    audio->waveformMin = nullptr;                              // 0x4C245E
    audio->volume = 0;                                         // 0x4C2461
}

// ---------------------------------------------------------------------------
// VA 0x401360 - PhysicsSceneInit(this): zeroing ctor of the 0x48 physics-scene
// wrapper (stored at model+60 by the .pmd/.pmx loaders).  Zeroes dwords 1..17
// (bytes 0x04..0x44) in the original's unrolled order; dword 0 is left alone
// (the caller zeroed the whole 0x48 block first).  No destructor calls -
// plain stores.
// ---------------------------------------------------------------------------
void PhysicsSceneInit(PhysicsScene* scene) {
    // 0x401362..0x40138F: every slot except owner(+0) and groundBody is
    // explicitly zeroed (dword stores on x86, qword on x64).
    scene->gizmoSphereVB = nullptr;
    scene->gizmoSphereIB = nullptr;
    scene->gizmoCubeVB = nullptr;
    scene->gizmoCubeIB = nullptr;
    scene->gizmoSphere33VB = nullptr;
    scene->gizmoSphere33IB = nullptr;
    scene->gizmoArrowVB = nullptr;
    scene->gizmoIdentityIB = nullptr;
    scene->gizmoBoxSelVB = nullptr;
    scene->gizmoBoxSelIB = nullptr;
    scene->collisionConfig = nullptr;
    scene->dispatcher = nullptr;
    scene->broadphase = nullptr;
    scene->solver = nullptr;
    scene->world = nullptr;
    scene->constraintId = 0;
}

// ---------------------------------------------------------------------------
// VA 0x004C34A0 - WaveSeekAndFeed(this, double): wave seek-and-respawn used by the
// loop-playback restart paths (FrameDriver 0x46F383, command dispatch
// 0x4876CA, and 0x4C3530).  Guards on the streaming buffer, clears the feed
// stop/fail counters, aligns the target file offset down to a whole block,
// SetFilePointer(FILE_BEGIN), then _beginthread(0x4C2CE0, 0, this) with the
// handle kept at +0x234.  The x87 sequence (fild/fild/fadd/fmul/fdiv/fmulp,
// then __ftol2_sse and idiv) is mirrored expression-for-expression:
//   t = (double)blk * ((avgAsDouble * v) / (double)blk);
//   offset = dataOff - (int)t % blk + (int)t;
// (avgAsDouble = (double)nAvgBytesPerSec, +4294967296.0 when the raw dword
// reads back negative - the original's unsigned reinterpretation.)
// The original returns char (0 no-buffer / 1 spawned); the shared header
// declares the return void and no caller checks it - kept void.
// ---------------------------------------------------------------------------
void WaveSeekAndFeed(void* obj, double seconds) {
    auto* audio = static_cast<WaveAudioContext*>(obj);
    if (audio->streamingBuffer == nullptr)                      // 0x4C34A4
        return;                                                 // 0x4C34AA
    const std::int32_t avgBytes = audio->format.nAvgBytesPerSec; // 0x4C34B1
    const std::int32_t blkAlign = audio->format.nBlockAlign;     // 0x4C34B7
    audio->WriteStopFlag(0);                                        // 0x4C34BF
    audio->failureCount = 0;                                    // 0x4C34C9
    double avg = static_cast<double>(avgBytes);                 // 0x4C34D7 fild
    if (avgBytes < 0)                                           // 0x4C34DA jge
        avg += 4294967296.0;                                    // 0x4C34DC
    const double t = static_cast<double>(blkAlign) *
                     ((avg * seconds) / static_cast<double>(blkAlign));
    const std::int32_t whole = static_cast<std::int32_t>(t);    // 0x4C34EA ftol
    const std::int32_t rem = whole % blkAlign;                  // 0x4C34F2 idiv
    const std::int32_t dataOff = audio->dataOffset;              // 0x4C34F4
    HANDLE file = audio->fileHandle;                             // 0x4C3502
    SetFilePointer(file, dataOff - rem + whole, nullptr, FILE_BEGIN);
    audio->feedThread = _beginthread(WaveFeedThread, 0, obj);   // 0x4C3518
}

// ---------------------------------------------------------------------------
// VA 0x004C3530 - WaveRestartAt(this, double): audio-timer
// restart (the seek
// entry from ui_frame_step / ui_mouse_misc / ui_editor_click and the frame
// drivers 0x430F20/0x4312E0/0x446A70/0x44AAA0).  Sequence verbatim:
// KillTimer(hwnd, 0x64) on the ctx main HWND (+0x0C), CloseDataFile
// (0x4C2680), WaveStartPlayback (0x4C2760), call the streaming buffer's
// vtable slot 15 with the ctx dword at +0x258 - slot 15 (0x3C x86 / 0x78
// x64) is SetFrequency, NOT SetVolume (slot 17): the original hands its
// -10000..0 volume dword to SetFrequency, an invalid no-op call, so its
// WAV volume never took effect; replicated verbatim below, then
// WaveSeekAndFeed(this, seconds), SetTimer(hwnd, 0x64, 0x21, 0).
// ---------------------------------------------------------------------------
void WaveRestartAt(void* obj, double seconds) {  // VA 0x004C3530
    auto* audio = static_cast<WaveAudioContext*>(obj);
    HWND hwnd = audio->mainWindow;                              // 0x4C3533
    KillTimer(hwnd, 0x64);                                      // 0x4C3539
    CloseDataFile(obj);                                         // 0x4C3541
    WaveStartPlayback(obj);                                     // 0x4C3548
    IDirectSoundBuffer* buffer = audio->streamingBuffer;
    // x64 0x7FF7CB4FAC5E (call [rax+0x78], edx = [rbx+0x288]): the original
    // re-applies the volume dword through vtable slot 15 = SetFrequency, not
    // SetVolume (slot 17) - an invalid no-op call, so the original's WAV
    // volume never took effect.  DWORD cast = mov edx bit-pattern semantics.
    buffer->SetFrequency(static_cast<DWORD>(audio->volume));      // 0x4C355D
    WaveSeekAndFeed(obj, seconds);                              // 0x4C356B
    SetTimer(hwnd, 0x64, 0x21, nullptr);                        // 0x4C3580
}

}  // namespace mikudancestudio
