#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

using namespace mikudancestudio::format;
namespace {
void CheckAt(bool value, int line) {
    if (!value) {
        std::fprintf(stderr, "check failed at line %d\n", line);
        std::abort();
    }
}
#define Check(value) CheckAt((value), __LINE__)
PmmSceneDocument Fixture() {
    PmmSceneDocument scene;
    PmmCameraKey camera;
    camera.fov = 45;
    scene.cameraKeys.dense.push_back(camera);
    PmmLightKey light0, light10;
    light0.frame = 0;
    light0.next = 7;
    light0.color = {0.2f, 0.4f, 0.6f};
    light0.direction = {0.0f, -1.0f, 0.0f};
    light10.frame = 10;
    light10.previous = 0;
    light10.color = {0.8f, 0.6f, 0.4f};
    light10.direction = {1.0f, 0.0f, 0.0f};
    scene.lightKeys.dense.push_back(light0);
    scene.lightKeys.sparse.push_back({7, 0, light10});
    PmmGravityKey gravity0, gravity10;
    gravity0.next = 6;
    gravity0.noiseEnabled = 1;
    gravity0.noise = 3;
    gravity0.acceleration = 2.0f;
    gravity0.direction = {0.0f, -1.0f, 0.0f};
    gravity10.frame = 10;
    gravity10.previous = 0;
    gravity10.noise = 10;
    gravity10.acceleration = 12.0f;
    gravity10.direction = {1.0f, 0.0f, 0.0f};
    scene.gravityKeys.dense.push_back(gravity0);
    scene.gravityKeys.sparse.push_back({6, 0, gravity10});
    PmmSceneModel model;
    model.slot = 3;
    model.layout.boneCount = 1;
    model.layout.morphCount = 1;
    model.layout.ikCount = 1;
    model.rigidCount = 1;
    model.rigidSources = {-1};
    PmmBoneKey bone0, bone10;
    bone0.frame = 0;
    bone0.next = 4;
    bone0.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    bone10.frame = 10;
    bone10.previous = 0;
    bone10.position = {10.0f, 0.0f, 0.0f};
    bone10.rotation = {0.0f, 0.0f, 0.70710677f, 0.70710677f};
    model.boneKeys.dense.push_back(bone0);
    model.boneKeys.sparse.push_back({4, 0, bone10});
    PmmMorphKey morph0, morph10;
    morph0.frame = 0;
    morph0.next = 5;
    morph0.value = 0.0f;
    morph10.frame = 10;
    morph10.previous = 0;
    morph10.value = 1.0f;
    model.morphKeys.dense.push_back(morph0);
    model.morphKeys.sparse.push_back({5, 0, morph10});
    PmmDisplayKey display0, display10;
    display0.frame = 0;
    display0.next = 9;
    display0.visible = 1;
    display0.ikEnabled = {0};
    display0.rigidStates = {{2, 3}};
    display10.frame = 10;
    display10.previous = 0;
    display10.visible = 0;
    display10.ikEnabled = {1};
    display10.rigidStates = {{4, 5}};
    model.displayKeys.dense.push_back(display0);
    model.displayKeys.sparse.push_back({9, 0, display10});
    scene.models.push_back(std::move(model));
    return scene;
}
void CheckSelectorBonePoses() {
    auto scene = Fixture();
    auto& model = scene.models[0];
    model.rigidSources[0] = 0;
    auto& bone10 = model.boneKeys.sparse[0].value;
    bone10.next = 8;
    PmmBoneKey bone20 = bone10;
    bone20.frame = 20;
    bone20.previous = 4;
    bone20.next = 0;
    bone20.position[0] = 20.0f;
    model.boneKeys.sparse.push_back({8, 0, bone20});
    auto& display0 = model.displayKeys.dense[0];
    display0.next = 6;
    auto& display15 = model.displayKeys.sparse[0].value;
    display15.frame = 15;
    display15.previous = 6;
    display15.rigidStates = {{2, 3}};
    PmmDisplayKey display5 = display15;
    display5.frame = 5;
    display5.previous = 0;
    display5.next = 9;
    display5.rigidStates = {{4, 5}};
    model.displayKeys.sparse.push_back({6, 0, display5});
    PmmSceneEvaluation evaluated;
    for (const auto [frame, position] :
         {std::pair{3u, 0.0f}, std::pair{7u, 10.0f},
          std::pair{12u, 10.0f}, std::pair{17u, 20.0f}}) {
        Check(EvaluatePmmSceneFrame(scene, frame, evaluated));
        Check(evaluated.models[0].bones[0].translation[0] == position);
    }
    model.boneKeys.sparse[0].value.next = 0;
    model.boneKeys.sparse.pop_back();
    Check(EvaluatePmmSceneFrame(scene, 17, evaluated));
    Check(evaluated.models[0].bones[0].translation[0] == 0.0f &&
          evaluated.models[0].bones[0].rotation[3] == 1.0f);
    const auto left = model.boneKeys.dense[0];
    const auto right = model.boneKeys.sparse[0].value;
    const PmmEvaluatedSelector window{2, 3, 5, 15};
    Check(SelectPmmBoneSegmentPose(0, 10, 4.5, window) ==
          PmmBoneSelectorResult::PreviousKey);
    Check(SelectPmmBoneSegmentPose(0, 10, 5.0, window) ==
          PmmBoneSelectorResult::NextKey);
    auto pose = evaluated.models[0].bones[0];
    Check(ApplyPmmBoneSelectorPose(PmmBoneSelectorResult::NextKey,
                                   left, right, true, pose));
    Check(pose.translation[0] == 10.0f);
    auto nonUnit = right;
    nonUnit.rotation[3] = 0.5f;
    Check(ApplyPmmBoneSelectorPose(PmmBoneSelectorResult::Reset,
                                   left, nonUnit, true, pose));
    Check(pose.rotation[3] == 0.5f);
    Check(ApplyPmmBoneSelectorPose(PmmBoneSelectorResult::Reset,
                                   left, nonUnit, false, pose));
    Check(pose.rotation[3] == 1.0f);
}
void CheckStatefulPlayback() {
    auto scene = Fixture();
    PmmScenePlaybackState playback;
    Check(StartPmmScenePlayback(scene, playback));
    Check(playback.initialized && playback.frame == 0.0 &&
          playback.models[0].bones[0].active);
    Check(AdvancePmmScenePlayback(scene, 5.0f / 30.0f, playback));
    Check(playback.pose.models[0].bones[0].translation[0] == 5.0f &&
          playback.pose.models[0].morphValues[0] == 0.5f &&
          playback.pose.models[0].visible == 1 &&
          playback.pose.lightState[0] == 0.5f);
    Check(playback.pose.gravity.noiseEnabled == 1 &&
          playback.pose.gravity.noise == 6 &&
          playback.pose.gravity.acceleration == 7.0f &&
          playback.pose.gravity.direction[0] == 0.5f);
    scene.models[0].boneKeys.sparse[0].value.position[0] = 20.0f;
    scene.models[0].morphKeys.sparse[0].value.value = 0.4f;
    Check(AdvancePmmScenePlayback(scene, 5.0f / 30.0f, playback));
    Check(playback.pose.models[0].bones[0].translation[0] == 10.0f &&
          playback.pose.models[0].morphValues[0] == 0.2f);
    const float roundedSeconds = 5.5006f / 30.0f;
    Check(AdvancePmmScenePlayback(scene, roundedSeconds, playback));
    Check(playback.frame == 5.501);
    Check(AdvancePmmScenePlayback(scene, roundedSeconds, playback));
    Check(playback.frame == 5.501);
    Check(AdvancePmmScenePlayback(scene, 10.0f / 30.0f, playback));
    Check(playback.pose.models[0].bones[0].translation[0] == 20.0f &&
          playback.pose.models[0].visible == 0 &&
          playback.pose.models[0].ikEnabled[0] == 1);
    Check(AdvancePmmScenePlayback(scene, 11.0f / 30.0f, playback));
    Check(!playback.models[0].bones[0].active &&
          !playback.models[0].morphs[0].active &&
          !playback.models[0].display.active);
    scene.models[0].boneKeys.sparse[0].value.position[0] = 30.0f;
    Check(AdvancePmmScenePlayback(scene, 12.0f / 30.0f, playback));
    Check(playback.pose.models[0].bones[0].translation[0] == 20.0f);
    const auto beforeRewind = playback.frame;
    Check(!AdvancePmmScenePlayback(scene, 5.0f / 30.0f, playback));
    Check(playback.frame == beforeRewind);
    Check(StartPmmScenePlayback(scene, playback));
    Check(AdvancePmmScenePlayback(scene, 12.0f / 30.0f, playback));
    Check(playback.pose.models[0].bones[0].translation[0] == 30.0f);

    scene = Fixture();
    scene.models[0].rigidSources[0] = 0;
    auto& display = scene.models[0].displayKeys.sparse[0].value;
    display.frame = 15;
    display.rigidStates = {{4, 5}};
    Check(StartPmmScenePlayback(scene, playback));
    Check(AdvancePmmScenePlayback(scene, 11.0f / 30.0f, playback));
    Check(playback.pose.models[0].bones[0].translation[0] == 10.0f &&
          !playback.models[0].bones[0].active);
    Check(AdvancePmmScenePlayback(scene, 17.0f / 30.0f, playback));
    Check(playback.pose.models[0].bones[0].translation[0] == 10.0f &&
          playback.pose.models[0].selectors[0].windowStart == 15);
    PmmSceneEvaluation direct;
    Check(EvaluatePmmSceneFrame(scene, 17, direct));
    Check(direct.models[0].bones[0].translation[0] == 0.0f);
    Check(StartPmmScenePlayback(scene, playback));
    Check(AdvancePmmScenePlayback(scene, 17.0f / 30.0f, playback));
    Check(playback.pose.models[0].bones[0].translation[0] == 0.0f);

    Check(StartPmmScenePlayback(scene, playback));
    const auto beforeFailure = playback;
    scene.models[0].boneKeys.sparse[0].value.previous = 99;
    Check(!AdvancePmmScenePlayback(scene, 5.0f / 30.0f, playback));
    Check(playback.frame == beforeFailure.frame &&
          playback.pose.models[0].bones[0].translation ==
              beforeFailure.pose.models[0].bones[0].translation);
}
void CheckCameraEvaluation() {
    auto scene = Fixture();
    auto& head = scene.cameraKeys.dense[0];
    head.next = 7;
    head.eye = {0.0f, 2.0f, 4.0f};
    head.target = {0.0f, 1.0f, 2.0f};
    head.distance = -10.0f;
    head.fov = 30;
    head.parentModel = 2;
    head.parentBone = 3;
    auto next = head;
    next.frame = 10;
    next.previous = 0;
    next.next = 0;
    next.eye = {10.0f, 12.0f, 14.0f};
    next.target = {2.0f, 3.0f, 4.0f};
    next.distance = -20.0f;
    next.fov = 60;
    next.perspective = 1;
    next.parentModel = 4;
    next.parentBone = 5;
    scene.cameraKeys.sparse.push_back({7, 0, next});
    PmmSceneEvaluation result;
    Check(EvaluatePmmSceneFrame(scene, 5, result));
    Check(result.camera.position[0] == 5.0f &&
          result.camera.rotation[1] == 2.0f &&
          result.camera.distance == -15.0f &&
          result.camera.fov == 45.0f &&
          result.camera.parentModel == 2 &&
          result.camera.perspective == 0);
    Check(EvaluatePmmSceneFrame(scene, 10, result));
    Check(result.camera.position[0] == 10.0f &&
          result.camera.parentModel == 4 &&
          result.camera.perspective == 1);
    PmmScenePlaybackState playback;
    Check(StartPmmScenePlayback(scene, playback));
    Check(AdvancePmmScenePlayback(scene, 5.0f / 30.0f, playback));
    Check(playback.pose.camera.position[0] == 5.0f);
    auto& curve = scene.cameraKeys.sparse[0].value.interpolation;
    curve[0][0] = 10;
    curve[1][0] = 100;
    curve[2][0] = 100;
    curve[3][0] = 110;
    PmmEvaluatedCamera seekCurve, playbackCurve;
    Check(EvaluatePmmCameraTrack(scene.cameraKeys, 2.5,
                                 PmmCameraInterpolation::Seek, seekCurve));
    Check(EvaluatePmmCameraTrack(scene.cameraKeys, 2.5,
                                 PmmCameraInterpolation::Playback,
                                 playbackCurve));
    Check(seekCurve.position[0] > 3.0f &&
          seekCurve.position[0] < 10.0f &&
          std::abs(seekCurve.position[0] - playbackCurve.position[0]) < 1e-5f &&
          seekCurve.position[1] == 4.5f);
    scene.cameraKeys.sparse[0].value.previous = 99;
    const auto saved = result.camera;
    Check(!EvaluatePmmSceneFrame(scene, 5, result));
    Check(result.camera.position == saved.position);
    scene.cameraKeys.sparse[0].value.previous = 0;
    scene.cameraKeys.sparse[0].value.frame = 1;
    PmmEvaluatedCamera camera;
    Check(EvaluatePmmCameraTrack(scene.cameraKeys, 0.5,
                                 PmmCameraInterpolation::Playback, camera));
    Check(camera.position[0] == 0.0f);
}
void CheckAccessoryEvaluation() {
    auto scene = Fixture();
    PmmSceneAccessory accessory;
    accessory.slot = 6;
    PmmAccessoryKey first, last;
    first.next = 8;
    first.visible = 1;
    first.shadowEnabled = 1;
    first.parentModel = 2;
    first.parentBone = 3;
    first.position = {1.0f, 2.0f, 3.0f};
    first.rotation = {4.0f, 5.0f, 6.0f};
    first.scale = 1.0f;
    first.opacity = 0.2f;
    last = first;
    last.frame = 10;
    last.previous = 0;
    last.next = 0;
    last.visible = 0;
    last.shadowEnabled = 0;
    last.parentModel = 4;
    last.parentBone = 5;
    last.position[0] = 10.0f;
    last.rotation[0] = 9.0f;
    last.scale = 3.0f;
    last.opacity = 0.8f;
    accessory.keyRecords.dense.push_back(first);
    accessory.keyRecords.sparse.push_back({8, 0, last});
    scene.accessories.push_back(accessory);
    PmmSceneEvaluation result;
    Check(EvaluatePmmSceneFrame(scene, 5, result));
    const auto& middle = result.accessories[0];
    Check(middle.slot == 6 && middle.visible == 1 &&
          middle.shadowEnabled == 1 && middle.parentModel == 2 &&
          middle.parentBone == 3 && middle.position[0] == 1.0f &&
          middle.rotation[0] == 4.0f && middle.scale == 2.0f &&
          std::abs(middle.opacity - 0.5f) < 1e-6f);
    Check(EvaluatePmmSceneFrame(scene, 10, result));
    Check(result.accessories[0].visible == 0 &&
          result.accessories[0].position[0] == 10.0f &&
          result.accessories[0].parentBone == 5 &&
          result.accessories[0].scale == 3.0f);
    PmmScenePlaybackState playback;
    Check(StartPmmScenePlayback(scene, playback));
    Check(AdvancePmmScenePlayback(scene, 5.0f / 30.0f, playback));
    Check(playback.pose.accessories[0].scale == 2.0f &&
          playback.pose.accessories[0].position[0] == 1.0f);
    Check(AdvancePmmScenePlayback(scene, 11.0f / 30.0f, playback));
    Check(playback.pose.accessories[0].position[0] == 10.0f &&
          !playback.accessories[0].active);
    scene.accessories[0].keyRecords.sparse[0].value.previous = 99;
    Check(!EvaluatePmmSceneFrame(scene, 5, result));
}
void CheckSelfShadowEvaluation() {
    auto scene = Fixture();
    PmmSelfShadowKey first, last;
    first.frame = 0;
    first.next = 6;
    first.mode = 1;
    first.distance = 0.01f;
    last.frame = 10;
    last.previous = 0;
    last.mode = 2;
    last.distance = 0.02f;
    scene.selfShadowKeys.dense.push_back(first);
    scene.selfShadowKeys.sparse.push_back({6, 0, last});
    PmmSceneEvaluation pose;
    Check(EvaluatePmmSceneFrame(scene, 5, pose));
    Check(pose.selfShadowMode == 1 && pose.selfShadowDistance == 0.01f);
    Check(EvaluatePmmSceneFrame(scene, 10, pose));
    Check(pose.selfShadowMode == 2 && pose.selfShadowDistance == 0.02f);
    PmmScenePlaybackState playback;
    Check(StartPmmScenePlayback(scene, playback));
    Check(AdvancePmmScenePlayback(scene, 5.0f / 30.0f, playback));
    Check(playback.pose.selfShadowMode == 1 &&
          playback.pose.selfShadowDistance == 0.01f);
    Check(AdvancePmmScenePlayback(scene, 10.0f / 30.0f, playback));
    Check(playback.pose.selfShadowMode == 2 &&
          playback.pose.selfShadowDistance == 0.02f);
    scene.selfShadowKeys.sparse[0].value.previous = 99;
    Check(!EvaluatePmmSceneFrame(scene, 10, pose));
    Check(pose.selfShadowMode == 2);
}
void CheckGravityEvaluation() {
    auto scene = Fixture();
    PmmSceneEvaluation pose;
    Check(EvaluatePmmSceneFrame(scene, 5, pose));
    Check(pose.gravity.noiseEnabled == 0 && pose.gravity.noise == 6 &&
          pose.gravity.acceleration == 7.0f &&
          pose.gravity.direction[1] == -0.5f);
    Check(EvaluatePmmSceneFrame(scene, 10, pose));
    Check(pose.gravity.noiseEnabled == 0 && pose.gravity.noise == 10 &&
          pose.gravity.acceleration == 12.0f);
    PmmEvaluatedGravity fractional;
    Check(EvaluatePmmGravityTrack(scene.gravityKeys, 5.5, fractional));
    Check(fractional.noiseEnabled == 1 && fractional.noise == 6 &&
          fractional.acceleration == 7.5f);
    Check(EvaluatePmmSceneFrame(scene, 0, pose));
    Check(pose.gravity.noiseEnabled == 1 && pose.gravity.noise == 3);
    const auto previous = fractional;
    scene.gravityKeys.sparse[0].value.previous = 99;
    Check(!EvaluatePmmGravityTrack(scene.gravityKeys, 5.5, fractional));
    Check(fractional.noise == previous.noise &&
          fractional.acceleration == previous.acceleration);
    Check(!EvaluatePmmSceneFrame(scene, 5, pose));
    Check(pose.frame == 0);
}
void CheckBrokenCameraLink() {
    auto scene = Fixture();
    scene.cameraKeys.dense[0].next = 15;
    PmmScenePlaybackState playback;
    Check(StartPmmScenePlayback(scene, playback));
    Check(!AdvancePmmScenePlayback(scene, 5.0f / 30.0f, playback));
    Check(playback.frame == 0.0);
}
}
int main() {
    // Reference x86 double accumulation and x64 mulss/addss differ by one
    // bit for this input. Preserve the architecture and signed zero.
    PmmBoneKey roundingLeft, roundingRight;
    roundingRight.frame = 10;
    roundingLeft.position = {0.1f, -0.0f, 0.0f};
    roundingRight.position = {0.2f, 0.0f, 0.0f};
    PmmEvaluatedBone rounded;
    Check(InterpolatePmmBoneSegment(roundingLeft, roundingRight, 3, rounded));
    std::uint32_t positionBits = 0, zeroBits = 0;
    std::memcpy(&positionBits, &rounded.translation[0], sizeof positionBits);
    std::memcpy(&zeroBits, &rounded.translation[1], sizeof zeroBits);
#if defined(_M_IX86)
    Check(positionBits == 0x3E051EB9 && zeroBits == 0x80000000);
#else
    Check(positionBits == 0x3E051EB8 && zeroBits == 0x80000000);
#endif
    PmmEvaluatedSelector endAtNext;
    endAtNext.windowEnd = 10;
    Check(SelectPmmBoneSegmentPose(0, 10, 5, endAtNext) ==
          PmmBoneSelectorResult::PreviousKey);
    Check(SelectPmmBoneSegmentPose(3, 10, 1, endAtNext) ==
          PmmBoneSelectorResult::PreviousKey);
    {auto scene=Fixture();std::array<float,6> light{},expected{};
        const auto& a=scene.lightKeys.dense[0];const auto& b=scene.lightKeys.sparse[0].value;
        Check(EvaluatePmmLightTrack(scene.lightKeys,3.25,PmmLightInterpolation::Playback,light));
        Check(InterpolatePmmLightSegment(a,b,3.25,PmmLightInterpolation::Playback,expected) && light==expected);
        Check(EvaluatePmmLightTrack(scene.lightKeys,3,PmmLightInterpolation::FrameSeek,light));
        Check(InterpolatePmmLightSegment(a,b,3,PmmLightInterpolation::FrameSeek,expected) && light==expected);
        scene.lightKeys.sparse[0].value.next=7;Check(!EvaluatePmmLightTrack(scene.lightKeys,0,PmmLightInterpolation::FrameSeek,light));}
    CheckGravityEvaluation();
    CheckSelfShadowEvaluation();
    CheckBrokenCameraLink();
    CheckCameraEvaluation();
    CheckAccessoryEvaluation();
    CheckSelectorBonePoses();
    CheckStatefulPlayback();
    std::vector<PmmDisplayKeyLink> displayLinks = {
        {0, 0, 1, 1}, {10, 0, 2, 0}, {20, 1, 0, 1}};
    const auto readDisplay = [](void* source, std::uint32_t index,
                                PmmDisplayKeyLink& key) -> bool {
        const auto& keys = *static_cast<std::vector<PmmDisplayKeyLink>*>(source);
        if (index >= keys.size()) return false;
        key = keys[index];
        return true;
    };
    PmmDisplayCursor displayCursor;
    PmmDisplaySelection selected;
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 5.0,
                              PmmDisplayTraversal::Seek, displayCursor,
                              selected));
    Check(selected.kind == PmmDisplaySelectionKind::Between &&
          selected.heldIndex == 0 && selected.cursorIndex == 1 &&
          selected.visible == 1);
    std::vector<PmmRigidState> selectorPairs = {{2, 3}, {2, 3}, {4, 5}};
    struct SelectorSource {
        std::vector<PmmDisplayKeyLink>* links;
        std::vector<PmmRigidState>* pairs;
    } selectorSource{&displayLinks, &selectorPairs};
    const auto readSelectorLink = [](void* source, std::uint32_t index,
                                     PmmDisplayKeyLink& key) -> bool {
        const auto& keys = *static_cast<SelectorSource*>(source)->links;
        if (index >= keys.size()) return false;
        key = keys[index];
        return true;
    };
    const auto readSelectorPair = [](void* source, std::uint32_t index,
                                     std::uint32_t selector,
                                     PmmRigidState& pair) -> bool {
        const auto& pairs = *static_cast<SelectorSource*>(source)->pairs;
        if (selector != 0 || index >= pairs.size()) return false;
        pair = pairs[index];
        return true;
    };
    PmmEvaluatedSelector window;
    Check(EvaluatePmmSelectorWindow(readSelectorLink, readSelectorPair,
                                    &selectorSource, selected, 0, window));
    Check(window.linkedModel == 2 && window.linkedBone == 3 &&
          window.windowStart == 0 && window.windowEnd == 20);
    selectorPairs[1] = {6, 7};
    Check(EvaluatePmmSelectorWindow(readSelectorLink, readSelectorPair,
                                    &selectorSource, selected, 0, window));
    Check(window.linkedModel == 2 && window.windowEnd == 10);
    selectorPairs[1] = {2, 3};
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 10.0,
                              PmmDisplayTraversal::Seek, displayCursor,
                              selected));
    Check(EvaluatePmmSelectorWindow(readSelectorLink, readSelectorPair,
                                    &selectorSource, selected, 0, window));
    Check(window.linkedModel == 2 && window.windowStart == 0 &&
          window.windowEnd == 20);
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 15.0,
                              PmmDisplayTraversal::Seek, displayCursor,
                              selected));
    Check(EvaluatePmmSelectorWindow(readSelectorLink, readSelectorPair,
                                    &selectorSource, selected, 0, window));
    Check(window.linkedModel == 2 && window.windowStart == 0 &&
          window.windowEnd == 20);
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 20.0,
                              PmmDisplayTraversal::Seek, displayCursor,
                              selected));
    Check(EvaluatePmmSelectorWindow(readSelectorLink, readSelectorPair,
                                    &selectorSource, selected, 0, window));
    Check(window.linkedModel == 4 && window.linkedBone == 5 &&
          window.windowStart == 20 && window.windowEnd == 0);
    const auto previousWindow = window;
    displayLinks[2].previous = 99;
    Check(!EvaluatePmmSelectorWindow(readSelectorLink, readSelectorPair,
                                     &selectorSource, selected, 0, window));
    Check(window.linkedModel == previousWindow.linkedModel &&
          window.windowStart == previousWindow.windowStart);
    displayLinks[2].previous = 1;
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 10.0,
                              PmmDisplayTraversal::Seek, displayCursor,
                              selected));
    Check(selected.kind == PmmDisplaySelectionKind::Exact &&
          selected.heldIndex == 1 && selected.visible == 0);
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 25.0,
                              PmmDisplayTraversal::Seek, displayCursor,
                              selected));
    Check(selected.kind == PmmDisplaySelectionKind::Terminal &&
          selected.heldIndex == 2 && selected.visible == 1);
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 5.0,
                              PmmDisplayTraversal::InitializePlayback,
                              displayCursor, selected));
    Check(displayCursor.index == 1 && displayCursor.active &&
          selected.kind == PmmDisplaySelectionKind::None);
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 5.5,
                              PmmDisplayTraversal::AdvancePlayback,
                              displayCursor, selected));
    Check(selected.kind == PmmDisplaySelectionKind::Between &&
          selected.heldIndex == 0);
    displayLinks[0].visible = 0;
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 5.5,
                              PmmDisplayTraversal::AdvancePlayback,
                              displayCursor, selected));
    Check(selected.visible == 0);
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 10.0,
                              PmmDisplayTraversal::AdvancePlayback,
                              displayCursor, selected));
    Check(selected.kind == PmmDisplaySelectionKind::Exact &&
          selected.heldIndex == 1);
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 25.0,
                              PmmDisplayTraversal::AdvancePlayback,
                              displayCursor, selected));
    Check(selected.kind == PmmDisplaySelectionKind::Terminal &&
          selected.heldIndex == 2 && !displayCursor.active);
    Check(SelectPmmDisplayKey(readDisplay, &displayLinks, 25.0,
                              PmmDisplayTraversal::InitializePlayback,
                              displayCursor, selected));
    Check(selected.kind == PmmDisplaySelectionKind::Terminal &&
          displayCursor.index == 2 && !displayCursor.active);
    displayLinks[2].previous = 99;
    Check(!SelectPmmDisplayKey(readDisplay, &displayLinks, 25.0,
                               PmmDisplayTraversal::Seek, displayCursor,
                               selected));
    auto scene = Fixture();
    PmmSceneEvaluation evaluated;
    Check(EvaluatePmmSceneFrame(scene, 0, evaluated));
    Check(evaluated.models[0].morphValues[0] == 0.0f &&
          evaluated.models[0].bones[0].translation[0] == 0.0f &&
          evaluated.models[0].visible == 1 &&
          evaluated.models[0].ikEnabled[0] == 0 &&
          evaluated.models[0].selectors[0].linkedModel == 2 &&
          evaluated.models[0].selectors[0].windowEnd == 10 &&
          evaluated.lightState[0] == 0.2f);
    Check(EvaluatePmmSceneFrame(scene, 5, evaluated));
    Check(evaluated.models[0].morphValues[0] == 0.5f &&
          evaluated.models[0].bones[0].translation[0] == 5.0f &&
          std::fabs(evaluated.models[0].bones[0].rotation[2] - 0.3826834f) <
              0.00001f &&
          evaluated.models[0].visible == 1 &&
          evaluated.models[0].ikEnabled[0] == 0 &&
          evaluated.lightState[0] == 0.5f &&
          evaluated.lightState[3] == 0.5f);
    scene.models[0].displayKeys.dense[0].visible = 0;
    scene.models[0].displayKeys.dense[0].ikEnabled[0] = 1;
    Check(EvaluatePmmSceneFrame(scene, 5, evaluated));
    Check(evaluated.models[0].visible == 0 &&
          evaluated.models[0].ikEnabled[0] == 1);
    scene.models[0].displayKeys.dense[0].visible = 1;
    scene.models[0].displayKeys.dense[0].ikEnabled[0] = 0;
    PmmEvaluatedBone playingBone;
    Check(InterpolatePmmBonePlaybackSegment(
        scene.models[0].boneKeys.dense[0],
        scene.models[0].boneKeys.sparse[0].value, 5.0, playingBone));
    Check(playingBone.translation[0] == 5.0f &&
          std::fabs(playingBone.rotation[2] - 0.3826834f) < 0.00001f);
    Check(InterpolatePmmBonePlaybackSegment(
        scene.models[0].boneKeys.dense[0],
        scene.models[0].boneKeys.sparse[0].value, 5.5, playingBone));
    Check(playingBone.translation[0] == 5.5f &&
          std::fabs(playingBone.rotation[2] - 0.4186597f) < 0.00001f);
    std::array<float, 6> soughtLight{};
    Check(InterpolatePmmLightSegment(
        scene.lightKeys.dense[0], scene.lightKeys.sparse[0].value, 5.0,
        PmmLightInterpolation::FrameSeek, soughtLight));
    Check(soughtLight[0] == 0.5f && soughtLight[3] == 0.5f);
    Check(InterpolatePmmLightSegment(
        scene.lightKeys.dense[0], scene.lightKeys.sparse[0].value, 2.5,
        PmmLightInterpolation::Playback, soughtLight));
    Check(soughtLight[3] == 0.25f && soughtLight[4] == -0.75f);
    const auto previousLight = soughtLight;
    Check(!InterpolatePmmLightSegment(
        scene.lightKeys.dense[0], scene.lightKeys.sparse[0].value, 10.0,
        PmmLightInterpolation::FrameSeek, soughtLight));
    Check(soughtLight == previousLight);
    Check(EvaluatePmmSceneFrame(scene, 10, evaluated));
    Check(evaluated.models[0].morphValues[0] == 1.0f &&
          evaluated.models[0].bones[0].translation[0] == 10.0f &&
          evaluated.models[0].visible == 0 &&
          evaluated.models[0].ikEnabled[0] == 1 &&
          evaluated.models[0].selectors[0].linkedModel == 4 &&
          evaluated.models[0].selectors[0].windowStart == 10);
    Check(EvaluatePmmSceneFrame(scene, 20, evaluated));
    Check(evaluated.models[0].morphValues[0] == 1.0f &&
          evaluated.lightState[0] == 0.8f);
    scene.models[0].morphKeys.sparse[0].value.previous = 42;
    Check(!EvaluatePmmSceneFrame(scene, 5, evaluated));
    Check(evaluated.frame == 20); // failure preserves the previous output
}
