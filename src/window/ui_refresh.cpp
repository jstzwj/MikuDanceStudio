// ===========================================================================
// VA 0x00440AC0 - RefreshRequest  (original: sub_440AC0)
// ===========================================================================
// UI selection-state setter reached from the scroll/wheel handlers, the
// frame driver (0x46B090) and the command dispatch (0x47E8A0) - the
// repaint-request funnel for the left-panel object list.  __thiscall with a
// single stack arg (area); this = the app object (g_Block).
//
// area selects the action:
//   -1..-4: activate track 0..3.  The four one-byte track flags at 0xA03E4
//           (app+0xA03E4..E7) become (1,0,0,0), (0,1,0,0), (0,0,1,0),
//           (0,0,0,1) respectively - but only if the target track flag is
//           currently 0 (an already-active track is a no-op).
//   other : object index into the 255-entry UI object pointer array at
//           0x9DD70 (objectSlots, "zeroed 0x3FC").  If the object
//           exists and its +0x4AC active flag is 0, all four track flags are
//           cleared and that object's flag is set to 1.
//
// Every successful path additionally clears the +0x4AC active flag of all
// 255 objects (51 groups x 5 pointers, sweep starting at 0x9DD74) and ends
// with PostLanguageSweep (0x42F1E0).  Note: 0x42F1E0 is called with `this`
// only - the area selector is consumed by this function and never forwarded.
//
// Reference: ../translated/MikuMikuDance/fcn_00440ac0.cpp
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

// --- sub_440AC0 (RefreshRequest) -------------------------------------------
// Each UI object (bone/expression entry behind the pointer array at
// 0x9DD70) carries a one-byte active flag at +0x4AC.
constexpr std::size_t kObjFlag4AC = 0x4AC;   // object active flag (byte)
constexpr int kSweepGroupCount = 0x33;       // 51 groups x 5 pointers = 255

// Clears the +0x4AC active flag of every object in the array.  This is the
// sweep shared by all branches (loc_440B00 / loc_440B90 / loc_440C20 /
// loc_440CB0 / loc_440D41): edx runs base+4 and per group touches
// [edx-4], [edx], [edx+4], [edx+8], [edx+0Ch], stride 0x14, 0x33 rounds.
void ClearAllObjectFlags(MMDApp* app) {
    for (int slot = 0; slot < kSweepGroupCount * 5; ++slot) {
        void* object = app->ObjectSlot(slot);
        if (object != nullptr)
            static_cast<unsigned char*>(object)[kObjFlag4AC] = 0;
    }
}

// --- sub_4C2680 (CloseDataFile) field offsets -------------------------------
// Offsets inside the 0x25C-byte stream/read context object (reached as
// app+0xCC (audioContext); e.g. Timer100 0x429770 does
// "mov ecx, [esi+0CCh]; call sub_4C2680").
constexpr std::size_t kStreamBufOffset   = 0x14;   // read buffer offset (dword)
constexpr std::size_t kStreamFilePtr     = 0x2C;   // FILE* stream
constexpr std::size_t kStreamAsyncActive = 0x234;  // async-read active (dword)
constexpr std::size_t kStreamFileHandle  = 0x244;  // file HANDLE
constexpr std::size_t kStreamCompletion  = 0x248;  // 0 idle / 1 stop / 2 done

}  // namespace

// ===========================================================================
// VA 0x00440AC0 - original: sub_440AC0
// ===========================================================================
void RefreshRequest(int area) {
    MMDApp* app = g_Block;

    // ---- area == -1: activate track 0 ------------------------------------
    if (area == -1) {                       // 0x00440AC8 cmp ebx, -1
        if (app->GlobalTrackSelected(GlobalTimelineTrack::Camera) == 0) {
            app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Camera);
            ClearAllObjectFlags(app);
            PostLanguageSweep(app);         // 0x00440B48 call sub_42F1E0
        }
        return;
    }

    // ---- area == -2: activate track 1 ------------------------------------
    if (area == -2) {                       // 0x00440B53 cmp ebx, -2
        if (app->GlobalTrackSelected(GlobalTimelineTrack::Light) == 0) {
            app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Light);
            ClearAllObjectFlags(app);
            PostLanguageSweep(app);         // 0x00440BD8 call sub_42F1E0
        }
        return;
    }

    // ---- area == -3: activate track 2 ------------------------------------
    if (area == -3) {                       // 0x00440BE3 cmp ebx, -3
        if (app->GlobalTrackSelected(GlobalTimelineTrack::SelfShadow) == 0) {
            app->SelectGlobalTimelineTrack(GlobalTimelineTrack::SelfShadow);
            ClearAllObjectFlags(app);
            PostLanguageSweep(app);         // 0x00440C68 call sub_42F1E0
        }
        return;
    }

    // ---- area == -4: activate track 3 ------------------------------------
    if (area == -4) {                       // 0x00440C73 cmp ebx, -4
        if (app->GlobalTrackSelected(GlobalTimelineTrack::Gravity) == 0) {
            app->SelectGlobalTimelineTrack(GlobalTimelineTrack::Gravity);
            ClearAllObjectFlags(app);
            PostLanguageSweep(app);         // 0x00440CF8 call sub_42F1E0
        }
        return;
    }

    // ---- default: object index into the 0x9DD70 pointer array ------------
    void* obj = app->ObjectSlot(area);
    if (obj != nullptr &&
        static_cast<unsigned char*>(obj)[kObjFlag4AC] == 0) {
        app->ClearGlobalTimelineTrackSelection();
        ClearAllObjectFlags(app);
        // 0x00440D89: the selected pointer is re-read after the sweep (the
        // sweep cleared its flag too) and only then set to 1.
        obj = app->ObjectSlot(area);
        static_cast<unsigned char*>(obj)[kObjFlag4AC] = 1;
        PostLanguageSweep(app);             // 0x00440D97 call sub_42F1E0
    }
}

// ===========================================================================
// VA 0x004C2680 - CloseDataFile  (original: sub_4C2680)
// ===========================================================================
// Stream/read-context teardown for the background data loader.  The context
// is the 0x25C-byte object reached through app+0xCC (audioContext;
// Timer100 0x429770 and FrameDriver 0x46B090 load it into ecx).  __thiscall,
// no stack args; the port keeps the placeholder void* file = the context.
//
//   1. if an async read is active (ctx+0x234 != 0) and the completion flag
//      (ctx+0x248) is still 0, set it to 1 and spin on Sleep(1) until a
//      worker thread writes 2, then yield once with Sleep(0);
//   2. _fclose(ctx+0x2C) if non-null;
//   3. CloseHandle(ctx+0x244) if non-null;
//   4. zero ctx+0x2C / +0x234 / +0x14 / +0x244 - the body of the tail call
//      (jmp) to sub_4C2660, VA 0x004C2660, inlined 1:1 below.
//
// Reference: ../translated/MikuMikuDance/fcn_004c2680.cpp
// =========================================================================//
void CloseDataFile(void* file) {
    auto* audio = static_cast<WaveAudioContext*>(file);

    // ---- step 1: wait for an in-flight async read to finish --------------
    // 0x004C2683 cmp [esi+234h], 0 / 0x004C268C test [esi+248h]
    if (audio->feedThread != 0 && audio->ReadStopFlag() == 0) {
        audio->WriteStopFlag(1);
        while (audio->ReadStopFlag() != 2)
            Sleep(1);
        Sleep(0);
    }

    // ---- step 2: close the FILE* stream ----------------------------------
    FILE* stream = audio->parseStream;
    if (stream != nullptr)
        std::fclose(stream);                // 0x004C26CE (original imports
                                            // _fclose; same CRT function)

    // ---- step 3: close the file handle -----------------------------------
    HANDLE hFile = audio->fileHandle;
    if (hFile != nullptr)
        CloseHandle(hFile);                 // 0x004C26E1

    // ---- step 4: reset context fields (tail call to sub_4C2660) ----------
    // 0x004C2660: xor eax,eax; mov [ecx+2Ch],eax; mov [ecx+234h],eax;
    //             mov [ecx+14h],eax; mov [ecx+244h],eax; retn
    audio->parseStream = nullptr;
    audio->feedThread = 0;
    audio->streamingBuffer = nullptr;
    audio->fileHandle = nullptr;
}

}  // namespace mikudancestudio
