#include "mikudancestudio/timeline_selection_grid.hpp"
#include "mikudancestudio/app_layout.hpp"
#include <memory>
#include "mikudancestudio/wave_audio_context.hpp"

#include <array>
#include <cstdio>
#include <thread>

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    std::array<unsigned char, 200 * 200> selected{};
    mikudancestudio::VisitTimelineSelectionCells(2, 5, 3, 6,
        [&](std::size_t cell, int column) {
            check(column >= 3 && column < 6, "Column cursor left the selection");
            selected[cell] = 1;
        });
    for (int row = 0; row < 200; ++row)
        for (int column = 0; column < 200; ++column)
            check(selected[row * 200 + column] ==
                      (row >= 2 && row < 5 && column >= 3 && column < 6),
                  "Rectangular selection drifted in a subsequent row");
    auto app = std::make_unique<mikudancestudio::MMDAppState>();
    static_assert(sizeof(app->rowHitBone) == 40000 * sizeof(int));
    static_assert(sizeof(app->rowHitMorph) == 40000 * sizeof(int));
    static_assert(sizeof(app->rowHitIk) == 40000 * sizeof(int));
    app->rowHitBone[39999] = 11;
    app->rowHitMorph[39999] = 22;
    app->rowHitIk[39999] = 33;
    check(app->rowHitBone[39999] == 11 && app->rowHitMorph[39999] == 22 &&
          app->rowHitIk[39999] == 33 && app->rowHitBand0[0] == 0,
          "Full-size hit grids must keep independent storage");
    int calls = 0;
    mikudancestudio::VisitTimelineSelectionCells(0, 0, 0, 5,
        [&](std::size_t, int) { ++calls; });
    mikudancestudio::VisitTimelineSelectionCells(0, 5, 4, 4,
        [&](std::size_t, int) { ++calls; });
    check(calls == 0, "Empty selection must not access the hit grid");

    // Exercise the same stop/completion publication used by the real WAV
    // worker without requiring an audio device or DirectSound buffer.
    mikudancestudio::WaveAudioContext audio{};
    int completionData = 0;
    std::thread worker([&] {
        while (audio.ReadStopFlag() != 1)
            SwitchToThread();
        completionData = 123;
        audio.WriteStopFlag(2);
    });
    audio.WriteStopFlag(1);
    const DWORD start = GetTickCount();
    while (audio.ReadStopFlag() != 2 && GetTickCount() - start < 5000)
        Sleep(1);
    const bool completed = audio.ReadStopFlag() == 2;
    check(completed, "Audio worker did not acknowledge stop");
    if (completed)
        check(completionData == 123, "Completion must publish worker writes");
    worker.join();
    return failures == 0 ? 0 : 1;
}
