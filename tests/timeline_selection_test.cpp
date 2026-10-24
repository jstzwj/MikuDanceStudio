#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "../src/window/timeline_selection.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <tuple>
#include <vector>

namespace mikudancestudio {
// The selection collector has no window, renderer or device dependencies.
MMDApp::MMDApp() : state{} {}
}
using namespace mikudancestudio;
namespace {
int failures = 0;
void Expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::fprintf(stderr, "%s\n", message); }
}

struct Fixture {
    std::unique_ptr<MMDApp> app = std::make_unique<MMDApp>();
    std::vector<mdl::CameraKey> camera = std::vector<mdl::CameraKey>(mdl::kTimelineKeyCapacity);
    std::vector<mdl::LightKey> light = std::vector<mdl::LightKey>(mdl::kTimelineKeyCapacity);
    std::vector<mdl::SelfShadowKey> shadow = std::vector<mdl::SelfShadowKey>(mdl::kTimelineKeyCapacity);
    std::vector<mdl::GravityKey> gravity = std::vector<mdl::GravityKey>(mdl::kTimelineKeyCapacity);
    std::vector<std::vector<mdl::AccessoryKey>> accessories;

    Fixture() : accessories(std::size(app->state.accKeyTracks),
                           std::vector<mdl::AccessoryKey>(mdl::kTimelineKeyCapacity)) {
        app->state.cameraKeyTrack = camera.data();
        app->state.lightKeyTrack = light.data();
        app->state.selfShadowKeyTrack = shadow.data();
        app->state.gravityKeyTrack = gravity.data();
        for (std::size_t slot = 0; slot < accessories.size(); ++slot) {
            app->AccessoryKeys(static_cast<int>(slot)) = accessories[slot].data();
            for (auto& key : accessories[slot]) {
                key.visible = key.shadowEnabled = 1;
                key.parentModel = -1;
                key.scale = key.opacity = 1.0f;
            }
        }
        for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
            const auto frame = static_cast<std::uint32_t>((i * 17 + 31) % mdl::kTimelineKeyCapacity);
            camera[i].frame = light[i].frame = shadow[i].frame = gravity[i].frame = frame;
            gravity[i].noiseEnabled = 1;
            camera[i].distance = -45.0f;
            camera[i].parentBone = -1;
            for (auto& track : accessories) track[i].frame = frame;
        }
    }
    ~Fixture() { ReleaseRecords(); }
    void ReleaseRecords() {
        for (int band = 0; band < 8; ++band) {
            const auto id = static_cast<TimelineSelectionBand>(band);
            ::operator delete(app->TimelineSelectionRecords(id));
            app->TimelineSelectionRecords(id) = nullptr;
            app->TimelineSelectionCount(id) = 0;
        }
    }
    template<class Key>
    void CheckGlobal(const std::vector<Key>& keys, TimelineSelectionBand band) {
        std::vector<std::pair<std::int32_t, std::int32_t>> expected, actual;
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i].selected) expected.emplace_back(
                static_cast<std::int32_t>(keys[i].frame), static_cast<std::int32_t>(i));
        const auto count = app->TimelineSelectionCount(band);
        Expect(count == static_cast<std::int32_t>(expected.size()), "Incorrect global count");
        const auto* records = app->TimelineSelectionRecords(band);
        for (int i = 0; i < count; ++i) {
            actual.emplace_back(records[i].words[3], records[i].words[0]);
            if (i > 0) Expect(records[i - 1].words[3] <= records[i].words[3], "Signed frame order");
            // Used fields only: the other words have always been unspecified.
            std::printf("global %d %d %d\n", int(band), records[i].words[0], records[i].words[3]);
        }
        std::sort(expected.begin(), expected.end());
        std::sort(actual.begin(), actual.end());
        Expect(expected == actual, "Wrong global record index/frame");
        if (expected.empty()) Expect(records == nullptr, "Empty track allocated records");
    }
    template<class Key>
    void CheckKeyBytes(const std::vector<Key>& keys, std::vector<Key>& before, bool cleared) {
        if (cleared) for (auto& key : before) key.selected = 0;
        Expect(std::memcmp(keys.data(), before.data(), keys.size() * sizeof(Key)) == 0,
               "Collection or clearing changed unrelated key data");
    }
    void Check(const char* name) {
        ReleaseRecords();
        auto beforeCamera = camera; auto beforeLight = light;
        auto beforeShadow = shadow; auto beforeGravity = gravity;
        auto beforeAccessories = accessories;
        CollectGlobalTimelineSelection(*app);
        std::printf("case %s\n", name);
        CheckGlobal(camera, TimelineSelectionBand::Camera);
        CheckGlobal(light, TimelineSelectionBand::Light);
        CheckGlobal(shadow, TimelineSelectionBand::SelfShadow);
        CheckGlobal(gravity, TimelineSelectionBand::Gravity);
        using AccessoryEntry = std::tuple<std::int32_t, std::int32_t, std::int32_t>;
        std::vector<AccessoryEntry> expected, actual;
        for (std::size_t slot = 0; slot < accessories.size(); ++slot)
            for (std::size_t index = 0; index < accessories[slot].size(); ++index) {
                const auto& key = accessories[slot][index];
                if (key.selected) expected.emplace_back(static_cast<std::int32_t>(key.frame),
                    static_cast<std::int32_t>(index), static_cast<std::int32_t>(slot));
            }
        const auto count = app->TimelineSelectionCount(TimelineSelectionBand::Accessory);
        Expect(count == static_cast<std::int32_t>(expected.size()), "Incorrect accessory count");
        const auto* records = app->TimelineSelectionRecords(TimelineSelectionBand::Accessory);
        for (int i = 0; i < count; ++i) {
            actual.emplace_back(records[i].words[3], records[i].words[1], records[i].words[2]);
            if (i > 0) Expect(records[i - 1].words[3] <= records[i].words[3], "Accessory frame order");
            std::printf("accessory %d %d %d\n", records[i].words[1], records[i].words[2], records[i].words[3]);
        }
        std::sort(expected.begin(), expected.end()); std::sort(actual.begin(), actual.end());
        Expect(actual == expected, "Wrong accessory record index/slot/frame");
        if (expected.empty()) Expect(records == nullptr, "Empty accessory selection allocated records");
        auto checkBytes = [&](bool cleared) {
            CheckKeyBytes(camera, beforeCamera, cleared); CheckKeyBytes(light, beforeLight, cleared);
            CheckKeyBytes(shadow, beforeShadow, cleared); CheckKeyBytes(gravity, beforeGravity, cleared);
            for (std::size_t slot = 0; slot < accessories.size(); ++slot)
                CheckKeyBytes(accessories[slot], beforeAccessories[slot], cleared);
        };
        checkBytes(false);
        ClearGlobalTimelineKeySelection(*app);
        checkBytes(true);
    }
};
}

void TestModelRangeCell() {
    auto app = std::make_unique<MMDApp>();
    auto model = std::make_unique<mdl::ModelRecord>();
    std::vector<mdl::BoneKey> bones(mdl::kBoneKeyCapacity);
    std::vector<mdl::MorphKey> morphs(mdl::kMorphKeyCapacity);
    std::vector<mdl::DisplayKey> displays(mdl::kDisplayKeyCapacity);
    model->boneKeys = bones.data();
    model->morphKeys = morphs.data();
    model->displayKeys = displays.data();
    app->ModelSlot(0) = reinterpret_cast<unsigned char*>(model.get());
    // Original x64 RVA 0x3BBF0..0x3BC60: the hit-map value selects the
    // operation, while column/row select the rectangle, independently of key size.
    for (int hit : {-1, -2}) {
        for (bool shift : {false, true}) {
            app->ClearTimelineRange();
            app->SetTimelineRangeCell(3, 4);
            app->TimelineSelectionChanged() = 0;
            bones.front().allocated = bones.back().allocated = 0x80;
            morphs.front().allocated = morphs.back().allocated = 0x80;
            displays.front().allocated = displays.back().allocated = 0x80;
            bones.back().frame = morphs.back().frame = displays.back().frame = 1234;
            StageModelTimelineRangeCell(*app, hit, 7, 11, shift);
            const bool move = hit == -1 && !shift;
            Expect(app->TimelineRangeFirstColumn() == (move ? 3 : 7) &&
                   app->TimelineRangeFirstRow() == (move ? 4 : 11) &&
                   app->TimelineRangeLastColumn() == (move ? 4 : 8) &&
                   app->TimelineRangeLastRow() == (move ? 5 : 12),
                   "Range hit uses grid coordinates, independently of the key buffer address");
            Expect(app->TimelineSelectionChanged() == static_cast<std::uint8_t>(move) &&
                   app->TimelineRangeApplyEnabled() == static_cast<std::uint8_t>(hit == -1 && shift),
                   "Range sentinels retain the original Shift/move/apply transitions");
            const unsigned char selected = hit == -2 && !shift ? 0 : 0x80;
            Expect(bones.front().allocated == selected && bones.back().allocated == selected &&
                   morphs.front().allocated == selected && morphs.back().allocated == selected &&
                   displays.front().allocated == selected && displays.back().allocated == selected,
                   "Replacement range clears all model tracks only when Shift is not held");
            Expect(bones.back().frame == 1234 && morphs.back().frame == 1234 &&
                   displays.back().frame == 1234, "Range staging preserves key data");
        }
    }
}

int main() {
    TestModelRangeCell();
    Fixture f;
    f.Check("empty selections with nonzero neighbouring flags");
    for (std::size_t index : {0, 4, 5, 9998, 9999}) {
        f.camera[index].selected = 0x80; f.light[index].selected = 0xFF;
        f.shadow[index].selected = 2; f.gravity[index].selected = 1;
        for (int slot : {0, 4, 5, 253, 254}) f.accessories[slot][index].selected = 0x80;
    }
    // Force signed ordering and frame ties independently of record indices.
    f.camera[9999].frame = 0xFFFFFFFF;
    f.light[4].frame = 0x80000000;
    f.accessories[254][9999].frame = 0x80000000;
    f.accessories[4][5].frame = f.accessories[5][5].frame = 123;
    f.Check("group boundaries, final slots, nonbinary flags and signed frames");
    for (auto& key : f.camera) key.selected = 1;
    f.Check("entire camera track");
    for (auto& key : f.accessories.back()) key.selected = 1;
    f.Check("entire final accessory track");
    // Clearing alone accepts partially allocated accessory tables, as before.
    f.app->AccessoryKeys(127) = nullptr;
    f.camera.back().selected = f.accessories.back().back().selected = 1;
    ClearGlobalTimelineKeySelection(*f.app);
    Expect(!f.camera.back().selected && !f.accessories.back().back().selected,
           "Clearing stopped at a missing accessory track");
    return failures ? 1 : 0;
}
