#include "mikudancestudio/mmd_app.hpp"
#include <cstdio>
#include <memory>

namespace { int sweeps = 0, applied = 0, refreshed = 0; }
namespace mikudancestudio {
MMDApp::MMDApp() : state{} {}
MMDApp* g_Block = nullptr;
void PostLanguageSweep(MMDApp*) { ++sweeps; }
void RefreshAfterFrameApply(MMDApp*) { ++applied; }
void PostViewRefresh(MMDApp*) { ++refreshed; }
void RefreshRequest(int);
void JumpNextKeyframe(MMDApp*);
void JumpPrevKeyframe(MMDApp*);
}

int main() {
    namespace mds = mikudancestudio;
    auto app = std::make_unique<mds::MMDApp>();
    mds::g_Block = app.get();
    mds::mdl::AccessoryRecord first{}, last{};
    app->AccessorySlots()[0] = &first;
    app->AccessorySlots()[254] = &last;
    bool ok = true;
    auto expect = [&](bool condition, const char* name) {
        if (!condition) { std::fprintf(stderr, "%s\n", name); ok = false; }
    };
    // Opposite shadow/selection values expose accidental use of the render flag.
    first.shadowEnabled = 1;
    last.rowSelected = 1;
    mds::RefreshRequest(0);
    expect(first.rowSelected == 1 && last.rowSelected == 0, "select accessory, clear previous row");
    expect(first.shadowEnabled == 1 && last.shadowEnabled == 0, "selection preserves shadow flags");
    expect(sweeps == 1, "new accessory selection refreshes");
    mds::RefreshRequest(0);
    expect(sweeps == 1, "already selected accessory is no-op");
    mds::RefreshRequest(254);
    expect(first.rowSelected == 0 && last.rowSelected == 1 && sweeps == 2,
           "select final slot with shadow disabled");
    const mds::GlobalTimelineTrack tracks[] = {mds::GlobalTimelineTrack::Camera,
        mds::GlobalTimelineTrack::Light, mds::GlobalTimelineTrack::SelfShadow,
        mds::GlobalTimelineTrack::Gravity};
    for (int i = 0; i < 4; ++i) {
        app->ClearGlobalTimelineTrackSelection();
        first.rowSelected = last.rowSelected = 1;
        const int before = sweeps;
        mds::RefreshRequest(-1 - i);
        expect(!first.rowSelected && !last.rowSelected, "global track clears all accessory rows");
        expect(first.shadowEnabled == 1 && last.shadowEnabled == 0, "global selection preserves shadows");
        for (int j = 0; j < 4; ++j)
            expect(app->GlobalTrackSelected(tracks[j]) == (i == j), "exclusive global selection");
        expect(sweeps == before + 1, "global selection refreshes");
        mds::RefreshRequest(-1 - i);
        expect(sweeps == before + 1, "already selected global is no-op");
    }
    mds::RefreshRequest(254);
    for (auto track : tracks) expect(!app->GlobalTrackSelected(track), "accessory clears global selection");
    const int beforeMissing = sweeps;
    mds::RefreshRequest(128);
    expect(sweeps == beforeMissing && last.rowSelected == 1, "empty slot is no-op");

    mds::mdl::AccessoryKey firstKeys[3]{}, lastKeys[3]{};
    firstKeys[0].next = lastKeys[0].next = 1;
    firstKeys[1].frame = 15; lastKeys[1].frame = 10;
    firstKeys[1].next = lastKeys[1].next = 2;
    firstKeys[2].previous = lastKeys[2].previous = 1;
    firstKeys[2].frame = 25; lastKeys[2].frame = 30;
    app->AccessoryKeys(0) = firstKeys;
    app->AccessoryKeys(254) = lastKeys;
    app->state.optflag[0] = 1;
    auto navigate = [&](bool next, unsigned current, unsigned expected, bool changes) {
        app->state.currentFrame = current;
        const int beforeApply = applied, beforeRefresh = refreshed;
        if (next) mds::JumpNextKeyframe(app.get()); else mds::JumpPrevKeyframe(app.get());
        expect(app->state.currentFrame == expected, "navigation filters by row selection");
        expect(applied - beforeApply == int(changes) && refreshed - beforeRefresh == int(changes),
               "navigation refresh policy");
    };
    navigate(true, 0, 10, true);
    navigate(true, 10, 30, true);
    navigate(false, 30, 10, true);
    navigate(false, 31, 30, true);
    navigate(true, 30, 30, false);
    first.rowSelected = 1;
    navigate(true, 10, 15, true);
    navigate(false, 30, 25, true);
    last.rowSelected = 0;
    navigate(true, 0, 15, true);
    navigate(false, 30, 25, true);
    first.rowSelected = 0;
    navigate(true, 12, 12, false);
    navigate(false, 12, 0, true);
    expect(first.shadowEnabled == 1 && last.shadowEnabled == 0, "navigation preserves shadows");
    mds::g_Block = nullptr;
    return ok ? 0 : 1;
}
