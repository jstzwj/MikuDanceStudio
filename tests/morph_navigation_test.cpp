#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include <cstdio>
#include <memory>

namespace {
int applied = 0;
int refreshed = 0;
}
namespace mikudancestudio {
// Isolate navigation from window/render initialization and downstream playback.
MMDApp::MMDApp() : state{} {}
void RefreshAfterFrameApply(MMDApp*) { ++applied; }
void PostViewRefresh(MMDApp*) { ++refreshed; }
void JumpNextKeyframe(MMDApp*);
void JumpPrevKeyframe(MMDApp*);
}

int main() {
    namespace mds = mikudancestudio;
    namespace mdl = mds::mdl;
    auto app = std::make_unique<mds::MMDApp>();
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::FrameGroup rows[2]{};
    mdl::MorphKey keys[6]{};
    // Display order differs from morph order. Only the second display row is
    // selected, so using the row index or half the record stride cannot pass.
    rows[0].targetIndex = 1;
    rows[1].targetIndex = 0;
    rows[1].selected = 1;
    keys[0].next = 2;
    keys[2].frame = 10; keys[2].next = 3;
    keys[3].frame = 30; keys[3].previous = 2;
    keys[1].next = 4;
    keys[4].frame = 15; keys[4].previous = 1; keys[4].next = 5;
    keys[5].frame = 25; keys[5].previous = 4;
    model->displayFrames = rows;
    model->morphKeys = keys;
    model->facialFrameCount = 2;
    app->SetSelectedModelSlot(0);
    app->ModelSlot(0) = reinterpret_cast<unsigned char*>(model.get());
    bool ok = true;
    auto check = [&](bool next, unsigned current, unsigned expected, bool applies) {
        app->state.currentFrame = current;
        const int beforeApply = applied, beforeRefresh = refreshed;
        if (next) mds::JumpNextKeyframe(app.get());
        else mds::JumpPrevKeyframe(app.get());
        if (app->state.currentFrame != expected ||
            applied - beforeApply != int(applies) ||
            refreshed - beforeRefresh != int(applies)) {
            std::fprintf(stderr, "%s from %u: expected %u/apply=%d, got %u/apply=%d/refresh=%d\n",
                next ? "next" : "prev", current, expected, int(applies),
                app->state.currentFrame, applied - beforeApply, refreshed - beforeRefresh);
            ok = false;
        }
    };
    check(true, 0, 10, true);
    check(true, 10, 30, true);
    check(true, 20, 30, true);
    check(true, 30, 30, false);
    check(false, 31, 30, true);
    check(false, 30, 10, true);
    check(false, 20, 10, true);
    check(false, 10, 0, true);
    check(false, 0, 0, true);
    rows[0].selected = 1;
    check(true, 10, 15, true);
    check(false, 30, 25, true);
    rows[1].selected = 0;
    check(true, 0, 15, true);
    check(false, 30, 25, true);
    rows[0].selected = 0;
    check(true, 12, 12, false);
    check(false, 12, 0, true);
    model->facialFrameCount = 0;
    model->displayFrames = nullptr;
    model->morphKeys = nullptr;
    check(true, 12, 12, false);
    check(false, 12, 0, true);
    return ok ? 0 : 1;
}
