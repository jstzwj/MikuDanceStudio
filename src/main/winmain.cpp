// ===========================================================================
// VA 0x004C4460 - WinMain  (original: _WinMain@16)
// ===========================================================================
// Full 1:1 port.  Flow (verified against decompilation + disassembly):
//   1. operator new(0xA4530) -> ctor (0x42AE60) -> Block (0x54593C)
//      -> memset 0 -> InitDefaults (0x40A730)
//   2. command line -> ConvertAnsiToWide (0x407A70) into wchar_t[256]
//      @ this+0xA0900;  empty command line -> swprintf_s(buf, 0x100, L"%s%s")
//      with ZERO varargs (faithful to the original call site).
//   3. InitMainWindowAndD3D (0x47A5B0);  failure -> return 0.
//   4. FPS-capped PeekMessage loop; idle branch computes the frame delta
//      from timeGetTime() and calls the frame driver (0x46B090).
//   5. WM_QUIT -> ShutdownCleanup (0x462C40) -> free(Block), Block = 0.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <timeapi.h>   // timeGetTime (excluded by WIN32_LEAN_AND_MEAN)
#include <new>

#include <cstdint>
#include <cwchar>
#include <cstdio>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mme_host_api.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd) {
    using namespace mikudancestudio;

    (void)hPrevInstance;

    struct EffectRuntime {
        explicit EffectRuntime(HINSTANCE instance) { MmeHostInitializeRuntime(instance); }
        ~EffectRuntime() { MmeHostShutdownRuntime(); }
    } effectRuntime(hInstance);

    // operator new(0xA4530), then ctor 0x42AE60 and the global Block
    // assignment.  The x64 original calls THROWING operator new
    // (__imp_??2@YAPEAX_K@Z at 0x7FF7CB4FB31F, size 0xA55E0) and never
    // null-checks - an OOM propagates bad_alloc.
    MMDApp* app = new MMDApp();
    g_Block = app;
    app->state = MMDAppState{};  // original: memset(p, 0, 0xA4530)
    app->InitDefaults();                                          // 0x40A730

    if (*lpCmdLine != '\0') {
        wchar_t converted[256];  // `Source` local in the original (ebp-0x204)
        ConvertAnsiToWide(app->Renderer(),                  // [Block+0xA06C4]
                          lpCmdLine, converted, 0x100);           // 0x407A70
        wcscpy_s(app->EnvFileName(), 0x100, converted);
    } else {
        // Original pushes only Format/BufferCount/Buffer - no varargs
        // (verified in disassembly at 0x4C44CA..0x4C44DA).  VC9 read two
        // garbage stack "pointers" and produced a junk path that the load
        // step then failed to open.  Deterministic equivalent: two empty
        // strings, so the buffer ends up empty and the load fails the same
        // way without relying on undefined behaviour of the modern CRT.
        swprintf_s(app->EnvFileName(), 0x100, g_SourceFormat, L"", L"");
    }

    if (!InitMainWindowAndD3D(g_Block, hInstance, nShowCmd))       // 0x47A5B0
        return 0;

    MSG msg;
    std::uint32_t timeHigh = 0;         // v7/ebp - high 32 bits of last tick
    DWORD timeLow = timeGetTime();      // Time/ebx
    app->MilliToSec() = 0.001f;         // [Block+0xA0B70] = flt_5318D0

    PeekMessageA(&msg, nullptr, 0, 0, 0);  // PM_NOREMOVE prime, original arg set
    // DIAG(fps): once per second append loop/pump/sleep statistics to the
    // file named by MIKUDANCESTUDIO_PUMP_STATS.  Purely observational -
    // the loop below is untouched when the variable is unset.
    char pumpStatsPath[MAX_PATH]{};
    std::FILE* pumpStats = nullptr;
    if (GetEnvironmentVariableA("MIKUDANCESTUDIO_PUMP_STATS",
                                pumpStatsPath, MAX_PATH) > 0) {
        pumpStats = std::fopen(pumpStatsPath, "a");
    }
    DWORD statsT0 = timeGetTime();
    std::uint64_t statsLoops = 0, statsMsgs = 0, statsPumps = 0;
    double statsSleepMs = 0.0, statsPumpMs = 0.0;
    while (msg.message != WM_QUIT) {       // 18
        if (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (pumpStats != nullptr) ++statsMsgs;
        } else {
            std::uint32_t nowLow = timeGetTime();           // v9
            std::uint32_t nowHigh = 0;                       // v10
            // v13 = (now - last) * 0.001.  x64 0x7FF7CB4FB4B0..0x7FF7CB4FB4B8:
            // the subtraction runs against the FULL 64-bit last tick
            // (sub rax, rdi), then one cvtsi2ss and a single-precision
            // mulss by the 0.001f slot (xmm7, same bits as MilliToSec).
            // The old port multiplied in double.
            const std::int64_t lastTick =
                (static_cast<std::int64_t>(timeHigh) << 32) | timeLow;
            float delta = static_cast<float>(
                static_cast<std::int64_t>(nowLow) - lastTick) * 0.001f;
            app->DeltaTime() = delta;                        // [Block+0xA077C]

            // v14 = 1/fpsLimit - delta (divss + subss, both float);
            // Sleep when cap enabled and positive.
            float sleepSec = 1.0f / app->FpsLimit() - delta;
            if (app->RecordingWindow() == nullptr && sleepSec > 0.0f) {
                DWORD pre = timeGetTime();
                Sleep(static_cast<DWORD>(sleepSec * 1000.0f));
                // x64 0x7FF7CB4FB4FD: divss by the same 0.001f slot, then
                // cvttss2si r64.  Float division, not double: the last-ulp
                // difference is magnified by the truncation below.
                std::uint64_t addMs = static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(sleepSec / 0.001f));
                nowHigh = static_cast<std::uint32_t>(addMs >> 32);
                nowLow += static_cast<std::uint32_t>(addMs);
                app->DeltaTime() = sleepSec + app->DeltaTime();
                if (pumpStats != nullptr) statsSleepMs += timeGetTime() - pre;
            }
            app->TimeNowLow() = nowLow;                      // [Block+0xA0B68]
            app->TimeNowHigh() = nowHigh;                    // [Block+0xA0B6C]
            timeLow = nowLow;
            timeHigh = nowHigh;

            DWORD pumpT0 = pumpStats != nullptr ? timeGetTime() : 0;
            FrameDriver(g_Block);                            // 0x46B090
            if (pumpStats != nullptr) {
                statsPumpMs += timeGetTime() - pumpT0;
                ++statsPumps;
                ++statsLoops;
                DWORD elapsed = timeGetTime() - statsT0;
                if (elapsed >= 1000) {
                    std::fprintf(pumpStats,
                        "loops=%llu msgs=%llu pump=%llu pumpMs=%.0f "
                        "sleepMs=%.0f fpsLimit=%.2f\n",
                        (unsigned long long)statsLoops,
                        (unsigned long long)statsMsgs,
                        (unsigned long long)statsPumps,
                        statsPumpMs, statsSleepMs, app->FpsLimit());
                    std::fflush(pumpStats);
                    statsT0 += elapsed;
                    statsLoops = statsMsgs = statsPumps = 0;
                    statsSleepMs = statsPumpMs = 0.0;
                }
            }
        }
    }
    if (pumpStats != nullptr) std::fclose(pumpStats);

    if (g_Block != nullptr) {
        MMDApp* victim = g_Block;
        ShutdownCleanup(g_Block);                            // 0x462C40
        delete victim;
        g_Block = nullptr;
    }
    return static_cast<int>(msg.wParam);
}
