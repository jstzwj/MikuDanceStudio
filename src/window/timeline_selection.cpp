#include "timeline_selection.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"

#include <cstdlib>
#include <iterator>

namespace mikudancestudio {

// Clear the active model's bone and morph key selections, plus the remaining
// display-key selections.  (sub_446A70 @ 0x447AF0 / 0x447BA0 / 0x447C30 etc.)
void ClearModelTimelineKeySelection(MMDApp& app) {
    // The original reaches this helper only while the model-edit context is
    // active.  During a partially completed mode switch MikuDanceStudio can briefly
    // expose the same row with no active slot, so preserve that entrance
    // invariant here instead of dereferencing slot 0/null.
    if (app.SelectedModel() == nullptr)
        return;
    mdl::ModelRecord* model = app.SelectedModel();
    mikudancestudio::mdl::BoneKey* boneKeys = model->boneKeys;
    for (std::size_t i = 0; i < mikudancestudio::mdl::kBoneKeyCapacity; ++i)
        boneKeys[i].allocated = 0;

    mikudancestudio::mdl::MorphKey* morphKeys = model->morphKeys;
    for (std::size_t i = 0; i < mikudancestudio::mdl::kMorphKeyCapacity; ++i)
        morphKeys[i].allocated = 0;
    mikudancestudio::mdl::DisplayKey* displayKeys = model->displayKeys;
    for (std::size_t i = 0; i < mikudancestudio::mdl::kDisplayKeyCapacity; ++i)
        displayKeys[i].allocated = 0;
}

// Original x64 RVA 0x3BBF0..0x3BC60 (morph), 0x3C3B0..0x3C420 (bone).
void StageModelTimelineRangeCell(MMDApp& app, std::int32_t type, std::int32_t column,
                                 std::int32_t row, bool shiftActive) {
    if (type == -1) { // loc_4483B8 / loc_448AB8
        if (shiftActive) {
            app.SetTimelineRangeCell(column, row);
            app.TimelineRangeApplyEnabled() = 1;
        } else {
            app.TimelineSelectionChanged() = 1;
        }
    } else { // type == -2, loc_4483F1/0x448AF1
        app.SetTimelineRangeCell(column, row);
        if (!shiftActive)
            ClearModelTimelineKeySelection(app);
    }
}

// Original 0x40EC70 compares the fourth word as a signed frame number.
int CompareFunction(const void* a, const void* b) {
    const std::int32_t keyA = static_cast<const TimelineSelectionRecord*>(a)->words[3];
    const std::int32_t keyB = static_cast<const TimelineSelectionRecord*>(b)->words[3];
    if (keyA >= keyB)
        return keyA > keyB;
    return -1;
}

namespace {
TimelineSelectionRecord* AllocateRecords(MMDApp& app, TimelineSelectionBand band) {
    const auto count = static_cast<std::size_t>(app.TimelineSelectionCount(band));
    auto* records = static_cast<TimelineSelectionRecord*>(
        ::operator new(count * sizeof(TimelineSelectionRecord)));
    app.TimelineSelectionRecords(band) = records;
    return records;
}

void SortRecords(MMDApp& app, TimelineSelectionBand band) {
    std::qsort(app.TimelineSelectionRecords(band),
               static_cast<std::size_t>(app.TimelineSelectionCount(band)),
               sizeof(TimelineSelectionRecord), CompareFunction);
}

template <class Key>
void FillSelectionRecords(MMDApp& app, const Key* keys, TimelineSelectionBand band) {
    if (app.TimelineSelectionCount(band) <= 0)
        return;
    TimelineSelectionRecord* out = AllocateRecords(app, band);
    // The original unrolls five records per iteration; retain index order.
    for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
        if (keys[i].selected != 0) {
            out->words[0] = static_cast<std::int32_t>(i);
            out->words[3] = static_cast<std::int32_t>(keys[i].frame);
            ++out;
        }
    }
    SortRecords(app, band);
}
}

// Original accessory clear ends at byte 0x927C0: exactly 10000 records,
// matching the other global tracks (0x447460..0x447A30).
void ClearGlobalTimelineKeySelection(MMDApp& app) {
    for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
        app.CameraKeys()[i].selected = 0;
        app.LightKeys()[i].selected = 0;
        app.ShadowKeys()[i].selected = 0;
        app.GravityKeys()[i].selected = 0;
    }
    for (int slot = 0; slot < static_cast<int>(std::size(app.state.accKeyTracks)); ++slot) {
        mdl::AccessoryKey* keys = app.AccessoryKeys(slot);
        if (keys == nullptr)
            continue;
        for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i)
            keys[i].selected = 0;
    }
}

void CollectGlobalTimelineSelection(MMDApp& app) {
    const auto* camera = app.CameraKeys();
    const auto* light = app.LightKeys();
    const auto* shadow = app.ShadowKeys();
    const auto* gravity = app.GravityKeys();
    for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
        if (camera[i].selected != 0)
            ++app.TimelineSelectionCount(TimelineSelectionBand::Camera);
        if (light[i].selected != 0)
            ++app.TimelineSelectionCount(TimelineSelectionBand::Light);
        if (shadow[i].selected != 0)
            ++app.TimelineSelectionCount(TimelineSelectionBand::SelfShadow);
        if (gravity[i].selected != 0)
            ++app.TimelineSelectionCount(TimelineSelectionBand::Gravity);
        for (int slot = 0; slot < static_cast<int>(std::size(app.state.accKeyTracks)); ++slot) {
            if (app.AccessoryKeys(slot)[i].selected != 0)
                ++app.TimelineSelectionCount(TimelineSelectionBand::Accessory);
        }
    }
    FillSelectionRecords(app, camera, TimelineSelectionBand::Camera);
    FillSelectionRecords(app, light, TimelineSelectionBand::Light);
    FillSelectionRecords(app, shadow, TimelineSelectionBand::SelfShadow);
    FillSelectionRecords(app, gravity, TimelineSelectionBand::Gravity);
    if (app.TimelineSelectionCount(TimelineSelectionBand::Accessory) > 0) {
        TimelineSelectionRecord* out = AllocateRecords(app, TimelineSelectionBand::Accessory);
        // Frame index, then accessory slot: this is the original 51 x 5 order.
        for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
            for (int slot = 0; slot < static_cast<int>(std::size(app.state.accKeyTracks)); ++slot) {
                const mdl::AccessoryKey& key = app.AccessoryKeys(slot)[i];
                if (key.selected != 0) {
                    out->words[1] = static_cast<std::int32_t>(i);
                    out->words[2] = slot;
                    out->words[3] = static_cast<std::int32_t>(key.frame);
                    ++out;
                }
            }
        }
        SortRecords(app, TimelineSelectionBand::Accessory);
    }
}

} // namespace mikudancestudio
