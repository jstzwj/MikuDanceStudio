#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include <cmath>
#include <memory>
#include <utility>

using namespace mikudancestudio;
namespace {
bool Near(float actual, float expected) {
    return std::fabs(actual - expected) < 0.00001f;
}
bool CheckSelectorBonePoses() {
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bone{};
    mdl::BoneKey keys[3]{};
    mdl::DisplayKey display[3]{};
    mdl::BoneReference pairs[3] = {{2, 3}, {4, 5}, {2, 3}};
    mdl::BoneOrderEntry selector{};
    std::uint32_t cursor = 0;
    std::uint8_t active = 0;
    model->boneCount = 1;
    model->boneTable = &bone;
    model->boneKeys = keys;
    model->boneKeyCursors = &cursor;
    model->boneTrackActive = &active;
    model->displayKeys = display;
    model->boneOrderCount = 1;
    model->boneOrderTable = &selector;
    bone.slotIndex = 0;
    bone.type = mdl::BoneType::RotateMove;
    keys[0].next = 1;
    keys[0].rotation[3] = 1.0f;
    keys[1].frame = 10;
    keys[1].previous = 0;
    keys[1].next = 2;
    keys[1].position[0] = 10.0f;
    keys[1].rotation[3] = 0.5f;
    keys[2].frame = 20;
    keys[2].previous = 1;
    keys[2].position[0] = 20.0f;
    keys[2].rotation[3] = 0.75f;
    display[0].next = 1;
    display[1].frame = 5;
    display[1].previous = 0;
    display[1].next = 2;
    display[2].frame = 15;
    display[2].previous = 1;
    for (int i = 0; i < 3; ++i)
        display[i].selectorStates = &pairs[i];
    auto* raw = model.get();
    for (const auto [frame, expected] :
         {std::pair{3, 0.0f}, std::pair{7, 10.0f},
          std::pair{12, 10.0f}, std::pair{17, 20.0f}}) {
        SeekModelFrame(*raw, frame, 0);
        if (!Near(bone.trans[0], expected)) return false;
        InitModelTrackCursors(raw, 0.0f, 0);
        AdvanceModelKeyframes(raw, static_cast<float>(frame) / 30.0f, 0);
        if (!Near(bone.trans[0], expected)) return false;
    }
    keys[1].next = 0;
    SeekModelFrame(*raw, 17, 0);
    if (!Near(bone.trans[0], 0.0f) ||
        !Near(bone.rotQuat[3], 1.0f)) return false;
    InitModelTrackCursors(raw, 0.0f, 0);
    AdvanceModelKeyframes(raw, 17.0f / 30.0f, 0);
    if (!Near(bone.trans[0], 0.0f) || !Near(bone.rotQuat[3], 0.5f))
        return false;
    InitModelTrackCursors(raw, 0.0f, 0);
    AdvanceModelKeyframes(raw, 11.0f / 30.0f, 0);
    if (!Near(bone.trans[0], 10.0f) || active != 0) return false;
    AdvanceModelKeyframes(raw, 17.0f / 30.0f, 0);
    return Near(bone.trans[0], 10.0f) &&
           Near(bone.rotQuat[3], 0.5f) && active == 0;
}
}
int main() {
    if (!CheckSelectorBonePoses()) return 13;
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bone{};
    mdl::MorphRecord morph{};
    mdl::BoneKey boneKeys[2]{};
    mdl::MorphKey morphKeys[2]{};
    mdl::DisplayKey display[2]{};
    mdl::BoneReference selectorPairs[2] = {{2, 3}, {4, 5}};
    mdl::BoneOrderEntry selector{};
    mdl::IkChain ik{};
    std::uint8_t ikState[2] = {1, 0};
    model->boneCount = 1;
    model->morphCount = 1;
    model->ikChainCount = 1;
    model->boneOrderCount = 1;
    model->boneOrderTable = &selector;
    model->boneTable = &bone;
    model->morphs = &morph;
    model->boneKeys = boneKeys;
    model->morphKeys = morphKeys;
    model->displayKeys = display;
    model->ikChains = &ik;
    std::uint32_t boneCursor = 0, morphCursor = 0;
    std::uint8_t boneActive = 0, morphActive = 0;
    model->boneKeyCursors = &boneCursor;
    model->morphKeyCursors = &morphCursor;
    model->boneTrackActive = &boneActive;
    model->morphTrackActive = &morphActive;
    bone.slotIndex = -1;
    bone.type = mdl::BoneType::RotateMove;
    boneKeys[0].frame = 0;
    boneKeys[0].next = 1;
    boneKeys[0].rotation[3] = 1.0f;
    boneKeys[1].frame = 10;
    boneKeys[1].previous = 0;
    boneKeys[1].position[0] = 10.0f;
    boneKeys[1].rotation[2] = 0.70710677f;
    boneKeys[1].rotation[3] = 0.70710677f;
    morphKeys[0].next = 1;
    morphKeys[1].frame = 10;
    morphKeys[1].previous = 0;
    morphKeys[1].value = 1.0f;
    display[0].next = 1;
    display[0].visible = 1;
    display[0].ikStates = &ikState[0];
    display[0].selectorStates = &selectorPairs[0];
    display[1].frame = 10;
    display[1].previous = 0;
    display[1].visible = 0;
    display[1].ikStates = &ikState[1];
    display[1].selectorStates = &selectorPairs[1];
    auto* raw = model.get();
    SeekModelFrame(*raw, 5, 0);
    if (!Near(bone.trans[0], 5.0f) ||
        !Near(bone.rotQuat[2], 0.3826834f) ||
        !Near(morph.value, 0.5f) || ik.enabled != 1 ||
        selector.linkedModel != 2 || selector.linkedBone != 3 ||
        selector.windowStart != 0 || selector.windowEnd != 10) return 1;
    // Edits to runtime keys must affect the next scrub immediately.
    boneKeys[1].position[0] = 20.0f;
    morphKeys[1].value = 0.4f;
    SeekModelFrame(*raw, 5, 0);
    if (!Near(bone.trans[0], 10.0f) || !Near(morph.value, 0.2f)) return 2;
    SeekModelFrame(*raw, 10, 0);
    if (!Near(bone.trans[0], 20.0f) || !Near(morph.value, 0.4f) ||
        model->loadComplete != 0 || ik.enabled != 0 ||
        selector.linkedModel != 4 || selector.linkedBone != 5 ||
        selector.windowStart != 10 || selector.windowEnd != 0) return 3;
    InitModelTrackCursors(raw, 0.0f, 0);
    AdvanceModelKeyframes(raw, 5.0f / 30.0f, 0);
    if (!Near(morph.value, 0.2f) || !Near(bone.trans[0], 10.0f) ||
        !Near(bone.rotQuat[2], 0.3826834f) ||
        model->loadComplete != 1 || ik.enabled != 1 ||
        selector.linkedModel != 2 || selector.windowEnd != 10) return 6;
    boneKeys[1].position[0] = 30.0f;
    morphKeys[1].value = 0.8f;
    AdvanceModelKeyframes(raw, 5.0f / 30.0f, 0);
    if (!Near(morph.value, 0.4f) || !Near(bone.trans[0], 15.0f)) return 7;
    AdvanceModelKeyframes(raw, 5.5f / 30.0f, 0);
    if (!Near(morph.value, 0.44f) || !Near(bone.trans[0], 16.5f) ||
        !Near(bone.rotQuat[2], 0.4186597f)) return 8;
    ikState[0] = 0;
    display[0].visible = 0;
    AdvanceModelKeyframes(raw, 5.5f / 30.0f, 0);
    if (model->loadComplete != 0 || ik.enabled != 0) return 9;
    selectorPairs[0] = {6, 7};
    AdvanceModelKeyframes(raw, 5.5f / 30.0f, 0);
    if (selector.linkedModel != 6 || selector.linkedBone != 7 ||
        selector.windowStart != 0 || selector.windowEnd != 10) return 12;
    ikState[1] = 1;
    display[1].visible = 1;
    AdvanceModelKeyframes(raw, 10.0f / 30.0f, 0);
    if (model->loadComplete != 1 || ik.enabled != 1 ||
        selector.linkedModel != 4 || selector.windowStart != 10) return 10;
    AdvanceModelKeyframes(raw, 11.0f / 30.0f, 0);
    if (model->loadComplete != 1 || ik.enabled != 1 ||
        model->displayTrackActive != 0) return 11;
    auto app = std::make_unique<MMDApp>();
    mdl::LightKey lightKeys[2]{};
    lightKeys[0].next = 1;
    lightKeys[0].color[0] = 0.2f;
    lightKeys[0].direction[1] = -1.0f;
    lightKeys[1].frame = 10;
    lightKeys[1].previous = 0;
    lightKeys[1].color[0] = 0.8f;
    lightKeys[1].direction[0] = 1.0f;
    app->LightKeys() = lightKeys;
    app->state.currentFrame = 5;
    RefreshLightPanel(app.get());
    if (!Near(app->LightColor()[0], 0.5f) ||
        !Near(app->LightDirection()[0], 0.5f) ||
        !Near(app->LightDirection()[1], -0.5f)) return 4;
    lightKeys[1].color[0] = 0.4f;
    RefreshLightPanel(app.get());
    if (!Near(app->LightColor()[0], 0.3f)) return 5;
    return 0;
}
