// Test-only comparison of a fully loaded PMM v1 scene with its disk records.
// Built only with MIKUDANCESTUDIO_DIAG; the normal application has no probe.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <vector>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/pmm_scene_document.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include "mikudancestudio/scene_session.hpp"
#include "mikudancestudio/scene_timeline_bridge.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "pmm_format_win.hpp"
#include "pmm_v1_layout_win.hpp"

namespace mikudancestudio {
namespace {
struct Reporter {
    FILE* file = nullptr;
    unsigned checks = 0, failures = 0;

    template <class T, class U>
    void Equal(const char* field, T actual, U expected, int slot = -1, int index = -1) {
        ++checks;
        if (actual == expected)
            return;
        ++failures;
        std::fprintf(file, "FAIL %s slot=%d index=%d actual=%g expected=%g\n", field, slot, index,
                     static_cast<double>(actual), static_cast<double>(expected));
    }
    void Present(const char* field, bool present, int slot = -1) {
        ++checks;
        if (present)
            return;
        ++failures;
        std::fprintf(file, "FAIL %s slot=%d missing\n", field, slot);
    }
    void Near(const char* field, float actual, float expected, int slot, int index) {
        ++checks;
        if (std::isfinite(actual) && std::isfinite(expected) &&
            std::fabs(actual - expected) <= 0.00001f)
            return;
        ++failures;
        std::fprintf(file, "FAIL %s slot=%d index=%d actual=%g expected=%g\n", field, slot, index,
                     static_cast<double>(actual), static_cast<double>(expected));
    }
};
bool ReportCameraChain(FILE* report, const char* stage, const mdl::CameraKey* keys) {
    if (!keys) {
        std::fprintf(report, "CAMERA CHAIN stage=%s missing\n", stage);
        return false;
    }
    std::uint32_t occupied = 0;
    for (std::uint32_t i = 1; i < mdl::kTimelineKeyCapacity; ++i)
        if (keys[i].frame != 0)
            ++occupied;
    std::uint32_t index = 0, reached = 0, bad = 0;
    const char* reason = "ok";
    for (; reached <= occupied; ++reached) {
        const auto next = keys[index].next;
        if (next == 0)
            break;
        if (next >= mdl::kTimelineKeyCapacity) {
            bad = next;
            reason = "out-of-range";
            break;
        }
        if (keys[next].frame == 0) {
            bad = next;
            reason = "unused-slot";
            break;
        }
        if (keys[next].previous != index || keys[next].frame <= keys[index].frame) {
            bad = next;
            reason = "broken-backlink-or-order";
            break;
        }
        index = next;
    }
    if (reached > occupied && keys[index].next != 0)
        reason = "cycle";
    else if (reached != occupied && reason[0] == 'o')
        reason = "orphan";
    else if (keys[0].previous != 0 && reason[0] == 'o')
        reason = "head-previous";
    else if (keys[0].frame != 0 && reason[0] == 'o')
        reason = "head-frame";
    std::fprintf(report,
                 "CAMERA CHAIN stage=%s head.next=%u occupied=%u reached=%u bad=%u reason=%s\n",
                 stage, keys[0].next, occupied, reached, bad, reason);
    if (std::strcmp(stage, "loaded") == 0) {
        std::uint32_t cursor = keys[0].next;
        for (int n = 0;
             cursor && cursor < mdl::kTimelineKeyCapacity && keys[cursor].frame != 0 && n < 4;
             ++n) {
            std::fprintf(report, "CAMERA KEY index=%u frame=%u previous=%u next=%u selected=%u\n",
                         cursor, keys[cursor].frame, keys[cursor].previous, keys[cursor].next,
                         keys[cursor].selected);
            cursor = keys[cursor].next;
        }
    }
    return std::strcmp(reason, "ok") == 0;
}
}

void WritePmmV1RuntimeParityReport(MMDApp* app) {
    wchar_t reportPath[1024]{}, fixturePath[1024]{};
    if (!GetEnvironmentVariableW(L"MDS_PMM_RUNTIME_REPORT", reportPath, 1024) ||
        !GetEnvironmentVariableW(L"MDS_PMM_GUI_FIXTURE", fixturePath, 1024))
        return;
    FILE* report = nullptr;
    if (_wfopen_s(&report, reportPath, L"wb") != 0 || !report)
        return;
    Reporter result{report};
    int fd = -1;
    if (_wsopen_s(&fd, fixturePath, _O_RDONLY | _O_BINARY, _SH_DENYNO, _S_IREAD) != 0) {
        std::fputs("FAIL fixture-open\n", report);
        std::fclose(report);
        return;
    }
    PathResolutionWorkspace paths{};
    PathToProjectDir(paths.projectDirectory, fixturePath);
    wcscpy_s(paths.executableDirectory, paths.projectDirectory);
    PmmV1FileLayoutContext resolver{app->Renderer(), &paths};
    format::PmmSceneDocument disk;
    const bool parsed = format::ReadPmmSceneDocument(ReadPmmFileBytesResult, &fd,
                                                     ResolvePmmV1FileLayout, &resolver, disk);
    _close(fd);
    if (!parsed || resolver.modelUnavailable) {
        std::fputs("FAIL fixture-parse\n", report);
        std::fclose(report);
        return;
    }
    result.Present("v1.camera.chain.loaded",
                   ReportCameraChain(report, "loaded", app->CameraKeys()));

    std::size_t runtimeModelCount = 0;
    for (int slot = 0; slot < kModelSlotCount; ++slot)
        if (app->ModelSlot(slot))
            ++runtimeModelCount;
    result.Equal("model.count", runtimeModelCount, disk.models.size());
    result.Present("scene.session", app->PortableSceneSession() != nullptr);
    result.Equal("scene.renderWidth", app->state.renderW, disk.envelope.settings.renderWidth);
    result.Equal("scene.renderHeight", app->state.renderH, disk.envelope.settings.renderHeight);
    result.Equal("scene.currentFrame", app->state.currentFrame, disk.timeline.currentFrame);
    result.Equal("scene.timelineStart", app->state.timelineStartFrame, disk.timeline.startFrame);
    result.Equal("scene.lastRegisteredFrame", app->state.lastRegisteredFrame,
                 disk.timeline.lastRegisteredFrame);
    result.Equal("scene.cameraFov", app->state.cameraFov, disk.envelope.settings.cameraFov);
    result.Near("v1.gravity.magnitude", app->GravityMagnitude(), disk.physics.gravityMagnitude, -1,
                0);
    result.Equal("v1.gravity.noise", app->GravityNoise(), disk.physics.gravityNoise);
    result.Equal("v1.gravity.noiseEnabled", app->GravityNoiseEnabled(),
                 disk.physics.gravityNoiseEnabled);
    for (int axis = 0; axis < 3; ++axis)
        result.Near("v1.gravity.direction", app->GravityDirection()[axis],
                    disk.physics.gravityDirection[axis], -1, axis);
    for (int i = 0; i < 3; ++i) {
        result.Equal("scene.lightColor", app->LightColor()[i], disk.lightState[i], -1, i);
        result.Equal("scene.lightDirection", app->LightDirection()[i], disk.lightState[i + 3], -1,
                     i);
    }

    for (const auto& expected : disk.models) {
        const int slot = expected.slot;
        mdl::ModelRecord* raw = app->ModelSlot(slot);
        result.Present("model", raw != nullptr, slot);
        if (!raw)
            continue;
        const mdl::ModelRecord& model = *raw;
        result.Equal("model.boneCount", model.boneCount, expected.layout.boneCount, slot);
        result.Equal("model.morphCount", model.morphCount, expected.layout.morphCount, slot);
        result.Equal("model.ikCount", model.ikChainCount, expected.layout.ikCount, slot);
        result.Equal("model.maxFrame", model.maxFrame, expected.selection.maxFrame, slot);
        result.Equal("model.selectedBone", model.selectedBone, expected.selection.selectedBone,
                     slot);
        for (std::size_t i = 0; i < expected.bonePose.size() && i < model.boneCount; ++i) {
            const auto& pose = expected.bonePose[i];
            const auto& bone = model.boneTable[i];
            for (int axis = 0; axis < 3; ++axis)
                result.Equal("bone.translation", bone.trans[axis], pose.components[axis], slot,
                             static_cast<int>(i));
            // PMM v1 writes rotQuat[0] twice; the eighth float is final.
            result.Equal("bone.quaternion0", bone.rotQuat[0], pose.components[7], slot,
                         static_cast<int>(i));
            for (int axis = 1; axis < 4; ++axis)
                result.Equal("bone.quaternion", bone.rotQuat[axis], pose.components[axis + 3], slot,
                             static_cast<int>(i));
            result.Equal("bone.physicsState", model.bonePhysicsState[i],
                         static_cast<std::uint8_t>(pose.flags[0] == 1), slot, static_cast<int>(i));
            result.Equal("bone.selection", model.boneSelection[i],
                         static_cast<std::uint8_t>(pose.flags[1] == 1), slot, static_cast<int>(i));
        }
        for (std::size_t i = 0; i < expected.morphPose.size() && i < model.morphCount; ++i)
            result.Equal("morph.value", model.morphs[i].value, expected.morphPose[i], slot,
                         static_cast<int>(i));
        for (std::size_t i = 0; i < expected.ikPose.size() && i < model.ikChainCount; ++i)
            result.Equal("ik.enabled", model.ikChains[i].enabled,
                         static_cast<std::uint8_t>(expected.ikPose[i] == 1), slot,
                         static_cast<int>(i));
    }
    format::PmmSceneEvaluation evaluated;
    if (app->state.currentFrame >= 0 &&
        format::EvaluatePmmSceneFrame(disk, static_cast<std::uint32_t>(app->state.currentFrame),
                                      evaluated)) {
        for (int i = 0; i < 3; ++i) {
            result.Equal("evaluated.light.color", app->LightColor()[i], evaluated.lightState[i], -1,
                         i);
            result.Equal("evaluated.light.direction", app->LightDirection()[i],
                         evaluated.lightState[i + 3], -1, i);
        }
        for (const auto& expected : evaluated.models) {
            const mdl::ModelRecord* raw = app->ModelSlot(expected.slot);
            if (!raw)
                continue;
            const mdl::ModelRecord& model = *raw;
            result.Equal("evaluated.model.visible", model.loadComplete, expected.visible,
                         expected.slot);
            for (std::size_t i = 0; i < expected.bones.size(); ++i) {
                const auto& live = model.boneTable[i];
                const auto type = static_cast<std::int8_t>(live.type);
                if ((type > static_cast<std::int8_t>(mdl::BoneType::Effector) &&
                     live.type != mdl::BoneType::FixedAxis) ||
                    live.slotIndex >= 0 || live.hasRigidBody != 0)
                    continue;
                for (int axis = 0; axis < 3; ++axis)
                    result.Equal("evaluated.bone.translation", live.trans[axis],
                                 expected.bones[i].translation[axis], expected.slot,
                                 static_cast<int>(i));
                for (int axis = 0; axis < 4; ++axis)
                    result.Equal("evaluated.bone.rotation", live.rotQuat[axis],
                                 expected.bones[i].rotation[axis], expected.slot,
                                 static_cast<int>(i));
            }
            for (std::size_t i = 0; i < expected.morphValues.size(); ++i)
                result.Equal("evaluated.morph.value", model.morphs[i].value,
                             expected.morphValues[i], expected.slot, static_cast<int>(i));
            for (std::size_t i = 0; i < expected.ikEnabled.size(); ++i)
                result.Equal("evaluated.ik.enabled", model.ikChains[i].enabled,
                             expected.ikEnabled[i], expected.slot, static_cast<int>(i));
        }
    } else {
        result.Present("evaluation", false);
    }
    // Seek a few non-current frames through the Windows runtime, compare the
    // portable result, then restore every runtime field touched by seeking.
    for (const std::uint32_t frame : {0u, 1u, 10u, 30u, 100u}) {
        if (!format::EvaluatePmmSceneFrame(disk, frame, evaluated)) {
            result.Present("sampled.evaluation", false);
            continue;
        }
        std::fprintf(report, "FRAME %u\n", frame);
        for (const auto& expected : evaluated.models) {
            mdl::ModelRecord* raw = app->ModelSlot(expected.slot);
            if (!raw)
                continue;
            auto& model = *raw;
            std::vector<mdl::BoneRecord> bones;
            std::vector<mdl::MorphRecord> morphs;
            std::vector<mdl::IkChain> ik;
            std::vector<mdl::BoneOrderEntry> order;
            std::vector<std::uint32_t> boneCursors, morphCursors;
            std::vector<unsigned char> boneActive, morphActive;
            if (model.boneCount)
                bones.assign(model.boneTable, model.boneTable + model.boneCount);
            if (model.morphCount)
                morphs.assign(model.morphs, model.morphs + model.morphCount);
            if (model.ikChainCount)
                ik.assign(model.ikChains, model.ikChains + model.ikChainCount);
            if (model.boneOrderCount)
                order.assign(model.boneOrderTable, model.boneOrderTable + model.boneOrderCount);
            const auto visibility = model.loadComplete;
            const auto displayCursor = model.displayKeyCursor;
            const auto displayActive = model.displayTrackActive;
            if (model.boneCount) {
                boneCursors.assign(model.boneKeyCursors, model.boneKeyCursors + model.boneCount);
                boneActive.assign(model.boneTrackActive, model.boneTrackActive + model.boneCount);
            }
            if (model.morphCount) {
                morphCursors.assign(model.morphKeyCursors,
                                    model.morphKeyCursors + model.morphCount);
                morphActive.assign(model.morphTrackActive,
                                   model.morphTrackActive + model.morphCount);
            }
            SeekModelFrame(*raw, static_cast<int>(frame), 0);
            auto* timelineSession = app->PortableSceneSession();
            result.Present("v1.timeline.session", timelineSession != nullptr, expected.slot);
            if (timelineSession)
                result.Equal("v1.timeline.seek.frame", timelineSession->Pose().frame, frame,
                             expected.slot);
            result.Equal("sampled.visible", model.loadComplete, expected.visible, expected.slot);
            for (std::size_t i = 0; i < expected.bones.size(); ++i) {
                const auto& live = model.boneTable[i];
                const auto type = static_cast<std::int8_t>(live.type);
                if ((type > static_cast<std::int8_t>(mdl::BoneType::Effector) &&
                     live.type != mdl::BoneType::FixedAxis) ||
                    live.slotIndex >= 0 || live.hasRigidBody != 0)
                    continue;
                for (int axis = 0; axis < 3; ++axis)
                    result.Equal("sampled.bone.translation", live.trans[axis],
                                 expected.bones[i].translation[axis], expected.slot,
                                 static_cast<int>(i));
                for (int axis = 0; axis < 4; ++axis) {
#if defined(_M_IX86)
                    // x86 seek rounds CRT double intermediates back to float;
                    // the portable scene evaluator follows the x64 float
                    // interpolation path. Compare their numeric pose.
                    result.Near("sampled.bone.rotation", live.rotQuat[axis],
                                expected.bones[i].rotation[axis], expected.slot,
                                static_cast<int>(i));
#else
                    result.Equal("sampled.bone.rotation", live.rotQuat[axis],
                                 expected.bones[i].rotation[axis], expected.slot,
                                 static_cast<int>(i));
#endif
                }
            }
            for (std::size_t i = 0; i < expected.morphValues.size(); ++i)
                result.Equal("sampled.morph", model.morphs[i].value, expected.morphValues[i],
                             expected.slot, static_cast<int>(i));
            for (std::size_t i = 0; i < expected.ikEnabled.size(); ++i)
                result.Equal("sampled.ik", model.ikChains[i].enabled, expected.ikEnabled[i],
                             expected.slot, static_cast<int>(i));
            // Exercise the incremental playback path on the same real scene.
            // Seek and playback use different float rounding, so compare
            // numerical poses within a tight tolerance rather than bitwise.
            InitModelTrackCursors(raw, 0.0f, 0);
            AdvanceModelKeyframes(raw, static_cast<float>(frame) / 30.0f, 0);
            result.Equal("playback.visible", model.loadComplete, expected.visible, expected.slot);
            for (std::size_t i = 0; i < expected.bones.size(); ++i) {
                const auto& live = model.boneTable[i];
                const auto type = static_cast<std::int8_t>(live.type);
                if ((type > static_cast<std::int8_t>(mdl::BoneType::Effector) &&
                     live.type != mdl::BoneType::FixedAxis) ||
                    live.slotIndex >= 0 || live.hasRigidBody != 0)
                    continue;
                for (int axis = 0; axis < 3; ++axis)
                    result.Near("playback.bone.translation", live.trans[axis],
                                expected.bones[i].translation[axis], expected.slot,
                                static_cast<int>(i));
                for (int axis = 0; axis < 4; ++axis)
                    result.Near("playback.bone.rotation", live.rotQuat[axis],
                                expected.bones[i].rotation[axis], expected.slot,
                                static_cast<int>(i));
            }
            for (std::size_t i = 0; i < expected.morphValues.size(); ++i)
                result.Near("playback.morph", model.morphs[i].value, expected.morphValues[i],
                            expected.slot, static_cast<int>(i));
            for (std::size_t i = 0; i < expected.ikEnabled.size(); ++i)
                result.Equal("playback.ik", model.ikChains[i].enabled, expected.ikEnabled[i],
                             expected.slot, static_cast<int>(i));
            std::copy(bones.begin(), bones.end(), model.boneTable);
            std::copy(morphs.begin(), morphs.end(), model.morphs);
            std::copy(ik.begin(), ik.end(), model.ikChains);
            std::copy(order.begin(), order.end(), model.boneOrderTable);
            std::copy(boneCursors.begin(), boneCursors.end(), model.boneKeyCursors);
            std::copy(boneActive.begin(), boneActive.end(), model.boneTrackActive);
            std::copy(morphCursors.begin(), morphCursors.end(), model.morphKeyCursors);
            std::copy(morphActive.begin(), morphActive.end(), model.morphTrackActive);
            model.loadComplete = visibility;
            model.displayKeyCursor = displayCursor;
            model.displayTrackActive = displayActive;
        }
    }
    auto* timelineSession = app->PortableSceneSession();
    if (timelineSession && !disk.cameraKeys.dense.empty()) {
        const auto savedFrame = app->state.currentFrame;
        std::array<float, 3> savedPosition{}, savedRotation{};
        std::copy_n(app->CameraPosition(), 3, savedPosition.begin());
        std::copy_n(app->CameraRotation(), 3, savedRotation.begin());
        const float savedDistance = app->CameraDistance();
        const float savedFov = app->CameraFov();
        const auto savedPerspective = app->CameraPerspective();
        const auto savedParentModel = app->CameraParentModel();
        const auto savedParentBone = app->CameraParentBone();
        for (const int frame : {0, 1, 10, 30, 100}) {
            app->state.currentFrame = frame;
            ReloadModels(app);
            result.Equal("v1.camera.session.frame", timelineSession->Pose().frame,
                         static_cast<std::uint32_t>(frame));
            const auto& camera = timelineSession->Pose().camera;
            for (int axis = 0; axis < 3; ++axis) {
                result.Near("v1.camera.position", app->CameraPosition()[axis],
                            camera.position[axis], frame, axis);
                result.Near("v1.camera.rotation", app->CameraRotation()[axis],
                            camera.rotation[axis], frame, axis);
            }
            result.Near("v1.camera.distance", app->CameraDistance(), camera.distance, frame, 0);
            result.Near("v1.camera.fov", app->CameraFov(), camera.fov, frame, 0);
            result.Equal("v1.camera.perspective", app->CameraPerspective(), camera.perspective);
        }
        std::copy(savedPosition.begin(), savedPosition.end(), app->CameraPosition());
        std::copy(savedRotation.begin(), savedRotation.end(), app->CameraRotation());
        app->CameraDistance() = savedDistance;
        app->CameraFov() = savedFov;
        app->CameraPerspective() = savedPerspective;
        app->CameraParentModel() = savedParentModel;
        app->CameraParentBone() = savedParentBone;
        app->state.currentFrame = savedFrame;
    }
    if (timelineSession && app->CameraKeys()) {
        result.Present("v1.camera.chain.before-head-edit",
                       ReportCameraChain(report, "before-head-edit", app->CameraKeys()));
        auto& cameraHead = app->CameraKeys()[0];
        const float editedX = cameraHead.eye[0] + 4.0f;
        cameraHead.eye[0] = editedX;
        app->state.currentFrame = 0;
        ReloadModels(app);
        result.Present("v1.camera.key.sync", timelineSession->RuntimeTracksChanged());
        result.Equal("v1.camera.edit.pose", timelineSession->Pose().camera.position[0], editedX);
        result.Equal("v1.camera.edit.runtime", app->CameraPosition()[0], editedX);
        std::fprintf(report, "BRIDGE v1 camera edit frame=0 x=%g\n", static_cast<double>(editedX));
        result.Present("v1.camera.chain.after-head-edit",
                       ReportCameraChain(report, "after-head-edit", app->CameraKeys()));
        const auto headNext = cameraHead.next;
        for (unsigned registration = 0; registration < 2; ++registration) {
            // Exercise the actual camera register command at frame zero,
            // with sparse successors. Re-registration must update the dense
            // head rather than allocate a frame-zero sparse record.
            SendMessageW(app->MainWindow(), WM_COMMAND, 452, 0);
            result.Equal("v1.camera.register.head.next", cameraHead.next, headNext);
            result.Present("v1.camera.chain.after-head-register",
                           ReportCameraChain(report, "after-head-register", app->CameraKeys()));
        }
        ReloadModels(app);
        result.Equal("v1.camera.register.session", timelineSession->Pose().camera.position[0],
                     editedX);
    }
    result.Present("v1.shadow.track",
                   !disk.selfShadowKeys.dense.empty() && app->ShadowKeys() != nullptr);
    if (timelineSession && !disk.selfShadowKeys.dense.empty() && app->ShadowKeys()) {
        const auto savedFrame = app->state.currentFrame;
        const auto savedPlaybackCursor = app->PlaybackCursorSeconds();
        for (const int frame : {0, 5, 30}) {
            std::uint8_t expectedMode = 0;
            float expectedDistance = 0;
            if (!format::EvaluatePmmSelfShadowTrack(disk.selfShadowKeys, frame, expectedMode,
                                                    expectedDistance)) {
                result.Present("v1.shadow.evaluation", false, frame);
                continue;
            }
            app->state.currentFrame = frame;
            RefreshSelfShadowPanel(app);
            result.Equal("v1.shadow.seek.mode", app->ShadowMode(), expectedMode, frame);
            result.Near("v1.shadow.seek.distance", app->ShadowDistance(), expectedDistance, frame,
                        0);
        }
        auto& head = app->ShadowKeys()[0];
        head.mode = head.mode == 1 ? 2 : 1;
        head.distance += 0.001f;
        app->state.currentFrame = 0;
        RefreshSelfShadowPanel(app);
        result.Equal("v1.shadow.edit.mode", app->ShadowMode(), head.mode);
        result.Near("v1.shadow.edit.distance", app->ShadowDistance(), head.distance, 0, 0);
        result.Equal("v1.shadow.edit.session",
                     timelineSession->Document().selfShadowKeys.dense[0].mode, head.mode);
        std::fprintf(report, "BRIDGE v1 shadow edit frame=0 mode=%u distance=%g\n", head.mode,
                     static_cast<double>(head.distance));
        app->PlaybackCursorSeconds() = 0.0f;
        StartPortableScenePlayback(app);
        app->PlaybackCursorSeconds() = 5.0f / 30.0f;
        AdvancePortableScenePlayback(app);
        ApplyPortableSceneSelfShadow(app);
        result.Equal("v1.shadow.playback.mode", app->ShadowMode(),
                     timelineSession->Pose().selfShadowMode);
        result.Near("v1.shadow.playback.distance", app->ShadowDistance(),
                    timelineSession->Pose().selfShadowDistance, 5, 0);
        app->state.currentFrame = 0;
        RefreshSelfShadowPanel(app);
        app->state.currentFrame = savedFrame;
        app->PlaybackCursorSeconds() = savedPlaybackCursor;
    }
    if (timelineSession) {
        const auto found =
            std::find_if(disk.accessories.begin(), disk.accessories.end(), [app](const auto& item) {
                return app->AccessorySlot(item.slot) && app->AccessoryKeys(item.slot);
            });
        result.Present("v1.accessory.loaded", found != disk.accessories.end());
        if (found != disk.accessories.end()) {
            const int slot = found->slot;
            auto& object = *app->AccessorySlot(slot);
            const int savedFrame = app->state.currentFrame;
            const auto savedObject = object;
            for (int frame : {0, 5, 30}) {
                app->state.currentFrame = frame;
                ApplyAccessoryTrack(app, slot);
                const auto pose =
                    std::find_if(timelineSession->Pose().accessories.begin(),
                                 timelineSession->Pose().accessories.end(),
                                 [slot](const auto& item) { return item.slot == slot; });
                result.Present("v1.accessory.pose",
                               pose != timelineSession->Pose().accessories.end(), slot);
                if (pose == timelineSession->Pose().accessories.end())
                    continue;
                result.Equal("v1.accessory.visible", object.visible, pose->visible, slot, frame);
                result.Equal("v1.accessory.shadow", object.shadowEnabled, pose->shadowEnabled, slot,
                             frame);
                result.Equal("v1.accessory.parentModel", object.parentModel, pose->parentModel,
                             slot, frame);
                result.Equal("v1.accessory.parentBone", object.parentBone, pose->parentBone, slot,
                             frame);
                result.Near("v1.accessory.scale", object.scale, pose->scale, slot, frame);
                result.Near("v1.accessory.opacity", object.opacity, pose->opacity, slot, frame);
                for (int axis = 0; axis < 3; ++axis) {
                    result.Near("v1.accessory.position", object.position[axis],
                                pose->position[axis], slot, axis);
                    result.Near("v1.accessory.rotation", object.rotation[axis],
                                pose->rotation[axis], slot, axis);
                }
            }
            object = savedObject;
            app->state.currentFrame = 0;
            auto& key = app->AccessoryKeys(slot)[0];
            key.position[0] += 4.0f;
            ApplyAccessoryTrack(app, slot);
            result.Equal("v1.accessory.edit.runtime", object.position[0], key.position[0], slot);
            result.Present("v1.accessory.edit.sync", timelineSession->RuntimeTracksChanged(), slot);
            std::fprintf(report, "BRIDGE v1 accessory edit slot=%d frame=0 x=%g\n", slot,
                         static_cast<double>(key.position[0]));
            app->PlaybackCursorSeconds() = 0.0f;
            StartPortableScenePlayback(app);
            app->PlaybackCursorSeconds() = 5.0f / 30.0f;
            AdvancePortableScenePlayback(app);
            ApplyPortableSceneAccessories(app);
            const auto pose = std::find_if(timelineSession->Pose().accessories.begin(),
                                           timelineSession->Pose().accessories.end(),
                                           [slot](const auto& item) { return item.slot == slot; });
            result.Present("v1.accessory.playback.pose",
                           pose != timelineSession->Pose().accessories.end(), slot);
            if (pose != timelineSession->Pose().accessories.end())
                result.Near("v1.accessory.playback.runtime", object.position[0], pose->position[0],
                            slot, 5);
            std::fprintf(report, "BRIDGE v1 accessory playback slot=%d frame=5\n", slot);
            app->state.currentFrame = savedFrame;
        }
    }
    if (timelineSession) {
        const auto& models = timelineSession->Document().models;
        const auto found =
            std::find_if(models.begin(), models.end(), [](const format::PmmSceneModel& model) {
                return model.layout.morphCount > 0 && model.layout.boneCount > 0;
            });
        if (found != models.end()) {
            const std::size_t modelIndex = static_cast<std::size_t>(found - models.begin());
            auto* raw = app->ModelSlot(found->slot);
            auto& model = *raw;
            std::uint32_t editBone = model.boneCount;
            for (std::uint32_t i = 0; i < model.boneCount; ++i) {
                const auto& bone = model.boneTable[i];
                const auto type = static_cast<std::int8_t>(bone.type);
                if ((type <= static_cast<std::int8_t>(mdl::BoneType::Effector) ||
                     bone.type == mdl::BoneType::FixedAxis) &&
                    bone.slotIndex < 0 && !bone.hasRigidBody && model.boneKeys[i].frame == 0 &&
                    model.boneKeys[i].next == 0) {
                    editBone = i;
                    break;
                }
            }
            result.Present("v1.timeline.editable", editBone < model.boneCount, found->slot);
            if (editBone < model.boneCount) {
                constexpr float kBoneEditDelta = 0.125f;
                const float originalX = model.boneKeys[editBone].position[0];
                model.boneKeys[editBone].position[0] = originalX + kBoneEditDelta;
                SeekModelFrame(*raw, 0, 0);
                result.Present("v1.timeline.key.sync", timelineSession->RuntimeTracksChanged(),
                               found->slot);
                result.Equal(
                    "v1.timeline.edit.pose",
                    timelineSession->Pose().models[modelIndex].bones[editBone].translation[0],
                    originalX + kBoneEditDelta);
                result.Equal("v1.timeline.edit.runtime", model.boneTable[editBone].trans[0],
                             originalX + kBoneEditDelta);
                std::fprintf(report, "BRIDGE v1 edit slot=%u bone=%u x=%g\n", found->slot, editBone,
                             static_cast<double>(model.boneTable[editBone].trans[0]));
                app->PlaybackCursorSeconds() = 0.0f;
                StartPortableScenePlayback(app);
                app->PlaybackCursorSeconds() = 5.0f / 30.0f;
                AdvancePortableScenePlayback(app);
                ApplyPortableSceneCamera(app);
                result.Present("v1.timeline.playing", timelineSession->Playing(), found->slot);
                const auto& playbackCamera = timelineSession->Pose().camera;
                for (int axis = 0; axis < 3; ++axis) {
                    result.Near("v1.camera.playback.position", app->CameraPosition()[axis],
                                playbackCamera.position[axis], found->slot, axis);
                    result.Near("v1.camera.playback.rotation", app->CameraRotation()[axis],
                                playbackCamera.rotation[axis], found->slot, axis);
                }
                result.Near("v1.camera.playback.distance", app->CameraDistance(),
                            playbackCamera.distance, found->slot, 0);
                result.Near("v1.camera.playback.fov", app->CameraFov(), playbackCamera.fov,
                            found->slot, 0);
                result.Equal("v1.camera.playback.perspective", app->CameraPerspective(),
                             playbackCamera.perspective);
                std::fprintf(report, "BRIDGE v1 camera playback frame=5 x=%g\n",
                             static_cast<double>(app->CameraPosition()[0]));
                result.Near(
                    "v1.timeline.playback.runtime", model.boneTable[editBone].trans[0],
                    timelineSession->Pose().models[modelIndex].bones[editBone].translation[0],
                    found->slot, static_cast<int>(editBone));
                std::fprintf(report, "BRIDGE v1 playback models=%zu\n",
                             timelineSession->Pose().models.size());
            }
        } else {
            result.Present("v1.timeline.model", false);
        }
    }
    result.Present("v1.camera.chain.before-save",
                   ReportCameraChain(report, "before-save", app->CameraKeys()));
    std::fprintf(report, "%s checks=%u failures=%u\n", result.failures ? "FAIL" : "PASS",
                 result.checks, result.failures);
    std::fclose(report);
}

void WritePmmV2MultiParityReport(MMDApp* app, const wchar_t* reportPath,
                                 const wchar_t* fixturePath) {
    FILE* report = nullptr;
    if (_wfopen_s(&report, reportPath, L"wb") != 0 || !report)
        return;
    Reporter result{report};
    int fd = -1;
    format::SceneSession disk;
    if (_wsopen_s(&fd, fixturePath, _O_RDONLY | _O_BINARY, _SH_DENYNO, _S_IREAD) != 0 ||
        !format::SceneSession::Load(ReadPmmFileBytesResult, &fd, nullptr, nullptr, disk)) {
        std::fputs("FAIL fixture-parse\n", report);
        if (fd >= 0)
            _close(fd);
        std::fclose(report);
        return;
    }
    _close(fd);
    const auto& sources = disk.Document().models;
    auto* live = app->PortableSceneSession();
    if (sources.size() != 2 || !live || live->Document().models.size() != 2) {
        std::fputs("FAIL session-models\n", report);
        std::fclose(report);
        return;
    }
    auto* first = app->ModelSlot(sources[0].slot);
    auto* second = app->ModelSlot(sources[1].slot);
    result.Present("first.model", first != nullptr, sources[0].slot);
    result.Present("second.model", second != nullptr, sources[1].slot);
    if (!first || !second) {
        std::fclose(report);
        return;
    }
    auto& firstModel = *first;
    auto& secondModel = *second;
    result.Present("first.bone", firstModel.boneCount > 0, sources[0].slot);
    result.Present("second.bone", secondModel.boneCount > 0, sources[1].slot);
    if (!firstModel.boneCount || !secondModel.boneCount) {
        std::fclose(report);
        return;
    }
    for (int frame : {0, 12, 15, 25}) {
        SeekModelFrame(*first, frame, 0);
        result.Equal("seek.frame", live->Pose().frame, static_cast<std::uint32_t>(frame));
        result.Equal("seek.models", live->Pose().models.size(), 2u);
        result.Near("seek.first", firstModel.boneTable[0].trans[0],
                    live->Pose().models[0].bones[0].translation[0], sources[0].slot, frame);
    }
    std::uint32_t editBone = secondModel.boneCount;
    for (std::uint32_t i = 0; i < secondModel.boneCount; ++i) {
        const auto& bone = secondModel.boneTable[i];
        const auto type = static_cast<std::int8_t>(bone.type);
        if ((type <= static_cast<std::int8_t>(mdl::BoneType::Effector) ||
             bone.type == mdl::BoneType::FixedAxis) &&
            bone.slotIndex < 0 && !bone.hasRigidBody && secondModel.boneKeys[i].frame == 0 &&
            secondModel.boneKeys[i].next == 0) {
            editBone = i;
            break;
        }
    }
    if (editBone == secondModel.boneCount) {
        std::fputs("FAIL edit-key\n", report);
        std::fclose(report);
        return;
    }
    const float originalX = secondModel.boneKeys[editBone].position[0];
    const float firstX = firstModel.boneTable[editBone].trans[0];
    secondModel.boneKeys[editBone].position[0] = originalX + 4.0f;
    SeekModelFrame(*first, 0, 0);
    result.Equal("edit.second.pose", live->Pose().models[1].bones[editBone].translation[0],
                 originalX + 4.0f);
    // A seek applies the evaluated pose only to its requested model slot.
    SeekModelFrame(*second, 0, 0);
    result.Equal("edit.second.runtime", secondModel.boneTable[editBone].trans[0], originalX + 4.0f);
    result.Equal("edit.first.runtime", firstModel.boneTable[editBone].trans[0], firstX);
    std::fprintf(report, "MULTI edit second.bone=%u x=%g\n", editBone,
                 static_cast<double>(secondModel.boneTable[editBone].trans[0]));
    app->PlaybackCursorSeconds() = 0;
    StartPortableScenePlayback(app);
    app->PlaybackCursorSeconds() = 15.0f / 30.0f;
    AdvancePortableScenePlayback(app);
    result.Present("playback.started", live->Playing());
    result.Equal("playback.models", live->Pose().models.size(), 2u);
    result.Near("playback.first", firstModel.boneTable[editBone].trans[0],
                live->Pose().models[0].bones[editBone].translation[0], sources[0].slot, 15);
    result.Near("playback.second", secondModel.boneTable[editBone].trans[0],
                live->Pose().models[1].bones[editBone].translation[0], sources[1].slot, 15);
    std::fprintf(report, "MULTI playback models=%zu\n", live->Pose().models.size());
    std::fprintf(report, "%s checks=%u failures=%u\n", result.failures ? "FAIL" : "PASS",
                 result.checks, result.failures);
    std::fclose(report);
}

void WritePmmV2AccessoryParityReport(MMDApp* app, const wchar_t* reportPath) {
    FILE* report = nullptr;
    if (_wfopen_s(&report, reportPath, L"wb") != 0 || !report)
        return;
    Reporter result{report};
    auto* session = app->PortableSceneSession();
    result.Present("v2.accessory.session", session != nullptr);
    if (session) {
        result.Present("v2.accessory.count", !session->Document().accessories.empty());
        const auto found =
            std::find_if(session->Document().accessories.begin(),
                         session->Document().accessories.end(), [app](const auto& item) {
                             return app->AccessorySlot(item.slot) && app->AccessoryKeys(item.slot);
                         });
        result.Present("v2.accessory.loaded", found != session->Document().accessories.end());
        if (found != session->Document().accessories.end()) {
            const int slot = found->slot;
            auto& object = *app->AccessorySlot(slot);
            const auto savedObject = object;
            const int savedFrame = app->state.currentFrame;
            for (int frame : {0, 5, 30}) {
                app->state.currentFrame = frame;
                ApplyAccessoryTrack(app, slot);
                const auto pose = std::find_if(
                    session->Pose().accessories.begin(), session->Pose().accessories.end(),
                    [slot](const auto& item) { return item.slot == slot; });
                result.Present("v2.accessory.pose", pose != session->Pose().accessories.end(),
                               slot);
                if (pose == session->Pose().accessories.end())
                    continue;
                result.Equal("v2.accessory.visible", object.visible, pose->visible, slot, frame);
                result.Equal("v2.accessory.shadow", object.shadowEnabled, pose->shadowEnabled, slot,
                             frame);
                result.Equal("v2.accessory.parentModel", object.parentModel, pose->parentModel,
                             slot, frame);
                result.Equal("v2.accessory.parentBone", object.parentBone, pose->parentBone, slot,
                             frame);
                result.Near("v2.accessory.scale", object.scale, pose->scale, slot, frame);
                result.Near("v2.accessory.opacity", object.opacity, pose->opacity, slot, frame);
                for (int axis = 0; axis < 3; ++axis) {
                    result.Near("v2.accessory.position", object.position[axis],
                                pose->position[axis], slot, axis);
                    result.Near("v2.accessory.rotation", object.rotation[axis],
                                pose->rotation[axis], slot, axis);
                }
            }
            object = savedObject;
            app->state.currentFrame = 0;
            auto& key = app->AccessoryKeys(slot)[0];
            key.position[0] += 2.0f;
            ApplyAccessoryTrack(app, slot);
            result.Equal("v2.accessory.edit", object.position[0], key.position[0], slot);
            app->PlaybackCursorSeconds() = 0.0f;
            StartPortableScenePlayback(app);
            app->PlaybackCursorSeconds() = 5.0f / 30.0f;
            AdvancePortableScenePlayback(app);
            ApplyPortableSceneAccessories(app);
            const auto pose =
                std::find_if(session->Pose().accessories.begin(), session->Pose().accessories.end(),
                             [slot](const auto& item) { return item.slot == slot; });
            result.Present("v2.accessory.playback.pose", pose != session->Pose().accessories.end(),
                           slot);
            if (pose != session->Pose().accessories.end())
                result.Near("v2.accessory.playback", object.position[0], pose->position[0], slot,
                            5);
            app->state.currentFrame = savedFrame;
        }
    }
    std::fprintf(report, "%s checks=%u failures=%u\n", result.failures ? "FAIL" : "PASS",
                 result.checks, result.failures);
    std::fclose(report);
}

void WritePmmV2SelectorParityReport(MMDApp* app) {
    wchar_t reportPath[1024]{}, fixturePath[1024]{};
    if (!GetEnvironmentVariableW(L"MDS_PMM_V2_SELECTOR_REPORT", reportPath, 1024) ||
        !GetEnvironmentVariableW(L"MDS_PMM_V2_SELECTOR_FIXTURE", fixturePath, 1024))
        return;
    if (GetEnvironmentVariableW(L"MDS_PMM_V2_ACCESSORY_FIXTURE", nullptr, 0)) {
        WritePmmV2AccessoryParityReport(app, reportPath);
        return;
    }
    if (GetEnvironmentVariableW(L"MDS_PMM_V2_MULTI_FIXTURE", nullptr, 0)) {
        WritePmmV2MultiParityReport(app, reportPath, fixturePath);
        return;
    }
    FILE* report = nullptr;
    if (_wfopen_s(&report, reportPath, L"wb") != 0 || !report)
        return;
    Reporter result{report};
    int fd = -1;
    format::SceneSession session;
    if (_wsopen_s(&fd, fixturePath, _O_RDONLY | _O_BINARY, _SH_DENYNO, _S_IREAD) != 0 ||
        !format::SceneSession::Load(ReadPmmFileBytesResult, &fd, nullptr, nullptr, session)) {
        std::fputs("FAIL fixture-parse\n", report);
        if (fd >= 0)
            _close(fd);
        std::fclose(report);
        return;
    }
    _close(fd);
    const auto& disk = session.Document();
    if (disk.models.size() != 1 || disk.models[0].rigidSources.size() < 2 ||
        disk.models[0].rigidSources[0] != -1 || disk.models[0].rigidSources[1] != 0) {
        std::fputs("FAIL fixture-selector-mapping\n", report);
        std::fclose(report);
        return;
    }
    const auto& source = disk.models[0];
    mdl::ModelRecord* raw = app->ModelSlot(source.slot);
    result.Present("model", raw != nullptr, source.slot);
    if (!raw) {
        std::fclose(report);
        return;
    }
    auto& model = *raw;
    if (model.boneCount && model.boneOrderCount > 1)
        std::fprintf(report, "BONE0 type=%d slot=%d rigid=%u selector1.bone=%d\n",
                     static_cast<int>(model.boneTable[0].type), model.boneTable[0].slotIndex,
                     model.boneTable[0].hasRigidBody, model.boneOrderTable[1].boneIndex);
    result.Present("selector", model.boneOrderCount > 1, source.slot);
    result.Present("display-key-1", source.displayKeys.sparse.size() >= 2, source.slot);
    result.Present("bone-key-1", source.boneKeys.sparse.size() >= 2, source.slot);
    if (model.boneOrderCount < 2 || source.displayKeys.sparse.size() < 2 ||
        source.boneKeys.sparse.size() < 2 || !model.boneCount) {
        std::fclose(report);
        return;
    }
    result.Equal("selector1.bone", model.boneOrderTable[1].boneIndex, 0, source.slot);
    result.Equal("bone0.slot", model.boneTable[0].slotIndex, 1, source.slot);
    result.Equal("bone0.rigid", model.boneTable[0].hasRigidBody, 0, source.slot);
    for (const auto& sparse : source.displayKeys.sparse) {
        if (sparse.index < 0 || sparse.index >= static_cast<int>(mdl::kDisplayKeyCapacity))
            continue;
        const auto& live = model.displayKeys[sparse.index];
        result.Equal("key.frame", live.frame, sparse.value.frame, source.slot, sparse.index);
        result.Equal("key.previous", live.previous, sparse.value.previous, source.slot,
                     sparse.index);
        result.Equal("key.next", live.next, sparse.value.next, source.slot, sparse.index);
        const auto& pair = mdl::SelectorStates(live)[0];
        result.Equal("key.selector.model", pair.modelIndex, sparse.value.rigidStates[0].modelIndex,
                     source.slot, sparse.index);
        result.Equal("key.selector.bone", pair.boneIndex, sparse.value.rigidStates[0].boneIndex,
                     source.slot, sparse.index);
    }
    for (const auto& sparse : source.boneKeys.sparse) {
        if (sparse.index < 0)
            continue;
        const auto& live = model.boneKeys[sparse.index];
        result.Equal("bone-key.frame", live.frame, sparse.value.frame, source.slot, sparse.index);
        result.Equal("bone-key.position-x", live.position[0], sparse.value.position[0], source.slot,
                     sparse.index);
    }
    std::vector<mdl::BoneRecord> bones;
    std::vector<mdl::MorphRecord> morphs;
    std::vector<mdl::IkChain> ik;
    std::vector<mdl::BoneOrderEntry> order;
    std::vector<std::uint32_t> boneCursors, morphCursors;
    std::vector<unsigned char> boneActive, morphActive;
    if (model.boneCount) {
        bones.assign(model.boneTable, model.boneTable + model.boneCount);
        boneCursors.assign(model.boneKeyCursors, model.boneKeyCursors + model.boneCount);
        boneActive.assign(model.boneTrackActive, model.boneTrackActive + model.boneCount);
    }
    if (model.morphCount) {
        morphs.assign(model.morphs, model.morphs + model.morphCount);
        morphCursors.assign(model.morphKeyCursors, model.morphKeyCursors + model.morphCount);
        morphActive.assign(model.morphTrackActive, model.morphTrackActive + model.morphCount);
    }
    if (model.ikChainCount)
        ik.assign(model.ikChains, model.ikChains + model.ikChainCount);
    order.assign(model.boneOrderTable, model.boneOrderTable + model.boneOrderCount);
    const auto visibility = model.loadComplete;
    const auto displayCursor = model.displayKeyCursor;
    const auto displayActive = model.displayTrackActive;
    const auto compare = [&](const char* prefix, const format::PmmEvaluatedSelector& expected,
                             int frame, int selectorIndex) {
        const auto& live = model.boneOrderTable[selectorIndex];
        result.Equal(prefix, live.linkedModel, expected.linkedModel, frame, selectorIndex);
        result.Equal("selector.bone", live.linkedBone, expected.linkedBone, frame, selectorIndex);
        result.Equal("selector.start", live.windowStart, expected.windowStart, frame,
                     selectorIndex);
        result.Equal("selector.end", live.windowEnd, expected.windowEnd, frame, selectorIndex);
    };
    const auto compareBone = [&](const char* prefix, const format::PmmEvaluatedBone& expected,
                                 int frame) {
        const auto& live = model.boneTable[0];
        result.Near(prefix, live.trans[0], expected.translation[0], frame, 0);
        for (int axis = 1; axis < 3; ++axis)
            result.Near("bone.translation", live.trans[axis], expected.translation[axis], frame,
                        axis);
        for (int axis = 0; axis < 4; ++axis)
            result.Near("bone.rotation", live.rotQuat[axis], expected.rotation[axis], frame, axis);
    };
    for (const std::uint32_t frame : {0u, 5u, 10u, 12u, 15u, 17u, 20u, 25u, 30u}) {
        if (!session.Seek(frame) || session.Pose().models.size() != 1 ||
            session.Pose().models[0].selectors.size() < 2 ||
            session.Pose().models[0].bones.empty()) {
            result.Present("selector.evaluation", false, frame);
            continue;
        }
        const auto& evaluated = session.Pose();
        const auto& expected = evaluated.models[0].selectors[0];
        std::fprintf(report, "FRAME %u expected=(%d,%d,%u,%u) bone0.x=%g\n", frame,
                     expected.linkedModel, expected.linkedBone, expected.windowStart,
                     expected.windowEnd,
                     static_cast<double>(evaluated.models[0].bones[0].translation[0]));
        SeekModelFrame(*raw, static_cast<int>(frame), 0);
        compare("seek.selector.model", expected, frame, 0);
        compare("seek.selector1.model", evaluated.models[0].selectors[1], frame, 1);
        compareBone("seek.bone0.x", evaluated.models[0].bones[0], frame);
        InitModelTrackCursors(raw, 0.0f, 0);
        AdvanceModelKeyframes(raw, static_cast<float>(frame) / 30.0f, 0);
        compare("playback.selector.model", expected, frame, 0);
        compare("playback.selector1.model", evaluated.models[0].selectors[1], frame, 1);
        compareBone("playback.bone0.x", evaluated.models[0].bones[0], frame);
    }
    if (!session.StartPlayback()) {
        result.Present("playback.start", false);
    } else {
        InitModelTrackCursors(raw, 0.0f, 0);
        for (const float frame :
             {0.0f, 5.0f, 5.5f, 10.0f, 12.0f, 15.0f, 17.0f, 20.0f, 25.0f, 30.0f, 35.0f}) {
            const float seconds = frame / 30.0f;
            if (!session.Advance(seconds)) {
                result.Present("playback.advance", false, static_cast<int>(frame));
                break;
            }
            AdvanceModelKeyframes(raw, seconds, 0);
            const auto& expected = session.Pose().models[0];
            const auto& playback = session.Playback();
            const int sample = static_cast<int>(frame * 10.0f);
            std::fprintf(report, "SEQUENCE frame=%g bone0.x=%g bone0.active=%u\n",
                         static_cast<double>(frame),
                         static_cast<double>(expected.bones[0].translation[0]),
                         playback.models[0].bones[0].active ? 1u : 0u);
            compare("sequence.selector0.model", expected.selectors[0], sample, 0);
            compare("sequence.selector1.model", expected.selectors[1], sample, 1);
            compareBone("sequence.bone0.x", expected.bones[0], sample);
            result.Equal("sequence.visible", model.loadComplete, expected.visible, sample);
            result.Equal("sequence.display.cursor", model.displayKeyCursor,
                         playback.models[0].display.index, sample);
            result.Equal("sequence.display.active", model.displayTrackActive,
                         playback.models[0].display.active ? 1 : 0, sample);
            result.Equal("sequence.bone0.cursor", model.boneKeyCursors[0],
                         playback.models[0].bones[0].index, sample);
            result.Equal("sequence.bone0.active", model.boneTrackActive[0],
                         playback.models[0].bones[0].active ? 1 : 0, sample);
            for (std::size_t i = 0; i < expected.morphValues.size(); ++i)
                result.Near("sequence.morph", model.morphs[i].value, expected.morphValues[i],
                            sample, static_cast<int>(i));
            for (std::size_t i = 0; i < expected.ikEnabled.size(); ++i)
                result.Equal("sequence.ik", model.ikChains[i].enabled, expected.ikEnabled[i],
                             sample, static_cast<int>(i));
        }
    }
    auto* timelineSession = app->PortableSceneSession();
    result.Present("timeline.session", timelineSession != nullptr);
    if (timelineSession) {
        for (const std::uint32_t frame : {0u, 12u, 25u}) {
            SeekModelFrame(*raw, static_cast<int>(frame), 0);
            result.Present("timeline.seek.frame", timelineSession->Pose().frame == frame, frame);
            result.Present("timeline.key.sync",
                           timelineSession->Document().models[0].slot == source.slot, frame);
            compareBone("timeline.seek.bone0", timelineSession->Pose().models[0].bones[0], frame);
        }
        const float previousCursor = app->PlaybackCursorSeconds();
        app->PlaybackCursorSeconds() = 0.0f;
        InitModelTrackCursors(raw, 0.0f, 0);
        StartPortableScenePlayback(app);
        result.Present("timeline.playback.start", timelineSession->Playing());
        for (const float frame : {0.0f, 5.5f, 10.0f, 17.0f, 25.0f, 35.0f}) {
            app->PlaybackCursorSeconds() = frame / 30.0f;
            PlaybackPoseAdvance(app, 1);
            result.Present("timeline.playback.frame",
                           timelineSession->Pose().frame == static_cast<std::uint32_t>(frame),
                           static_cast<int>(frame));
            compareBone("timeline.playback.bone0", timelineSession->Pose().models[0].bones[0],
                        static_cast<int>(frame));
        }
        const auto firstIndex = model.boneKeys[0].next;
        const bool editable = firstIndex > 0 && firstIndex < mdl::kBoneKeyCapacity &&
                              model.boneKeys[firstIndex].frame == 15;
        result.Present("timeline.edit.key", editable);
        if (editable) {
            auto& first = model.boneKeys[firstIndex];
            const float savedX = first.position[0];
            first.position[0] = savedX + 4.0f;
            SeekModelFrame(*raw, 15, 0);
            result.Equal("timeline.edit.pose",
                         timelineSession->Pose().models[0].bones[0].translation[0], savedX + 4.0f);
            result.Equal("timeline.edit.runtime", model.boneTable[0].trans[0], savedX + 4.0f);
            std::fprintf(report, "BRIDGE edit frame=15 bone0.x=%g\n",
                         static_cast<double>(model.boneTable[0].trans[0]));
            first.position[0] = savedX;

            std::uint32_t freeIndex = 0;
            for (std::uint32_t i = model.boneCount; i < mdl::kBoneKeyCapacity; ++i)
                if (i != firstIndex && model.boneKeys[i].frame == 0 &&
                    model.boneKeys[i].previous == 0 && model.boneKeys[i].next == 0) {
                    freeIndex = i;
                    break;
                }
            result.Present("timeline.insert.slot", freeIndex != 0);
            if (freeIndex) {
                const auto savedHead = model.boneKeys[0];
                const auto savedFirst = first;
                const auto savedFree = model.boneKeys[freeIndex];
                auto& inserted = model.boneKeys[freeIndex];
                inserted = savedHead;
                inserted.frame = 7;
                inserted.previous = 0;
                inserted.next = firstIndex;
                inserted.position[0] = 7.0f;
                inserted.allocated = 1;
                model.boneKeys[0].next = freeIndex;
                first.previous = freeIndex;
                SeekModelFrame(*raw, 7, 0);
                result.Equal("timeline.insert.pose",
                             timelineSession->Pose().models[0].bones[0].translation[0], 7.0f);
                result.Equal("timeline.insert.runtime", model.boneTable[0].trans[0], 7.0f);
                std::fprintf(report, "BRIDGE insert frame=7 bone0.x=%g\n",
                             static_cast<double>(model.boneTable[0].trans[0]));
                model.boneKeys[0] = savedHead;
                first = savedFirst;
                model.boneKeys[freeIndex] = savedFree;
            }
            SeekModelFrame(*raw, 0, 0);
        }
        app->PlaybackCursorSeconds() = previousCursor;
    }
    if (auto* shadowSession = app->PortableSceneSession();
        shadowSession && app->ShadowKeys() &&
        shadowSession->Document().selfShadowKeys.sparse.size() == 2) {
        const auto savedFrame = app->state.currentFrame;
        const auto savedPlaybackCursor = app->PlaybackCursorSeconds();
        for (const int frame : {0, 5, 10, 15, 20, 25}) {
            app->state.currentFrame = frame;
            RefreshSelfShadowPanel(app);
            result.Equal("shadow.seek.frame", shadowSession->Pose().frame,
                         static_cast<std::uint32_t>(frame));
            result.Equal("shadow.seek.mode", app->ShadowMode(),
                         shadowSession->Pose().selfShadowMode, frame);
            result.Near("shadow.seek.distance", app->ShadowDistance(),
                        shadowSession->Pose().selfShadowDistance, frame, 0);
        }
        auto& head = app->ShadowKeys()[0];
        head.mode = 2;
        head.distance += 0.001f;
        app->state.currentFrame = 0;
        RefreshSelfShadowPanel(app);
        result.Equal("shadow.edit.mode", app->ShadowMode(), head.mode);
        result.Near("shadow.edit.distance", app->ShadowDistance(), head.distance, 0, 0);
        result.Equal("shadow.edit.session", shadowSession->Document().selfShadowKeys.dense[0].mode,
                     head.mode);
        std::fprintf(report, "BRIDGE shadow edit frame=0 mode=%u distance=%g\n", head.mode,
                     static_cast<double>(head.distance));
        app->PlaybackCursorSeconds() = 0.0f;
        StartPortableScenePlayback(app);
        app->PlaybackCursorSeconds() = 25.0f / 30.0f;
        AdvancePortableScenePlayback(app);
        ApplyPortableSceneSelfShadow(app);
        result.Equal("shadow.playback.mode", app->ShadowMode(),
                     shadowSession->Pose().selfShadowMode);
        result.Near("shadow.playback.distance", app->ShadowDistance(),
                    shadowSession->Pose().selfShadowDistance, 25, 0);
        app->state.currentFrame = 0;
        RefreshSelfShadowPanel(app);
        app->PlaybackCursorSeconds() = savedPlaybackCursor;
        app->state.currentFrame = savedFrame;
    } else {
        result.Present("shadow.timeline", false);
    }
    if (auto* gravitySession = app->PortableSceneSession();
        gravitySession && app->GravityKeys() &&
        gravitySession->Document().gravityKeys.sparse.size() == 2) {
        const auto savedFrame = app->state.currentFrame;
        const auto savedCursor = app->PlaybackCursorSeconds();
        SendMessageW(app->MainWindow(), WM_COMMAND, 266, 0);
        const HWND dialog = app->GravitySettingDialog();
        result.Present("gravity.dialog", dialog != nullptr);
        for (const int frame : {0, 5, 10, 15, 20, 25}) {
            format::PmmEvaluatedGravity expected;
            if (!format::EvaluatePmmGravityTrack(disk.gravityKeys, frame, expected, true)) {
                result.Present("gravity.evaluation", false, frame);
                continue;
            }
            app->state.currentFrame = frame;
            ApplyGravityTrack(app);
            result.Equal("gravity.seek.frame", gravitySession->Pose().frame,
                         static_cast<std::uint32_t>(frame));
            result.Equal("gravity.seek.noiseEnabled", app->GravityNoiseEnabled(),
                         expected.noiseEnabled, frame);
            result.Equal("gravity.seek.noise", app->GravityNoise(), expected.noise, frame);
            result.Near("gravity.seek.acceleration", app->GravityMagnitude(), expected.acceleration,
                        frame, 0);
            for (int axis = 0; axis < 3; ++axis)
                result.Near("gravity.seek.direction", app->GravityDirection()[axis],
                            expected.direction[axis], frame, axis);
            if (frame == 5 && dialog) {
                char actual[32]{}, formatted[32]{};
                std::snprintf(formatted, sizeof formatted, "%3.2f", expected.acceleration);
                GetWindowTextA(GetDlgItem(dialog, panel::kGravityAccelEdit), actual, sizeof actual);
                result.Present("gravity.dialog.acceleration", std::strcmp(actual, formatted) == 0);
                const int directionEdits[] = {panel::kGravityDirXEdit, panel::kGravityDirYEdit,
                                              panel::kGravityDirZEdit};
                for (int axis = 0; axis < 3; ++axis) {
                    std::snprintf(formatted, sizeof formatted, "%3.2f", expected.direction[axis]);
                    GetWindowTextA(GetDlgItem(dialog, directionEdits[axis]), actual, sizeof actual);
                    result.Present("gravity.dialog.direction", std::strcmp(actual, formatted) == 0);
                }
                GetWindowTextA(GetDlgItem(dialog, panel::kGravityNoiseEdit), actual, sizeof actual);
                result.Equal("gravity.dialog.noise", std::atoi(actual), expected.noise);
                result.Equal("gravity.dialog.enabled",
                             IsDlgButtonChecked(dialog, panel::kGravityNoiseCheckbox),
                             static_cast<UINT>(BST_CHECKED));
                std::fprintf(report, "BRIDGE gravity dialog frame=5 noise=%d acceleration=%g\n",
                             expected.noise, static_cast<double>(expected.acceleration));
            }
        }
        auto& head = app->GravityKeys()[0];
        head.noiseEnabled = 1;
        head.noise += 1;
        head.acceleration += 0.25f;
        head.direction[0] += 0.125f;
        app->state.currentFrame = 0;
        ApplyGravityTrack(app);
        result.Equal("gravity.edit.noise", app->GravityNoise(), head.noise);
        result.Near("gravity.edit.acceleration", app->GravityMagnitude(), head.acceleration, 0, 0);
        result.Near("gravity.edit.session",
                    gravitySession->Document().gravityKeys.dense[0].acceleration, head.acceleration,
                    0, 0);
        std::fprintf(report, "BRIDGE gravity edit frame=0 noise=%d acceleration=%g\n", head.noise,
                     static_cast<double>(head.acceleration));
        app->PlaybackCursorSeconds() = 0.0f;
        StartPortableScenePlayback(app);
        for (const float frame : {0.0f, 5.0f, 5.5f, 10.0f, 15.0f, 20.0f, 25.0f}) {
            app->PlaybackCursorSeconds() = frame / 30.0f;
            AdvancePortableScenePlayback(app);
            ApplyPortableSceneGravity(app);
            const auto& pose = gravitySession->Pose().gravity;
            const int sample = static_cast<int>(frame * 10.0f);
            result.Equal("gravity.playback.noiseEnabled", app->GravityNoiseEnabled(),
                         pose.noiseEnabled, sample);
            result.Equal("gravity.playback.noise", app->GravityNoise(), pose.noise, sample);
            result.Near("gravity.playback.acceleration", app->GravityMagnitude(), pose.acceleration,
                        sample, 0);
            for (int axis = 0; axis < 3; ++axis)
                result.Near("gravity.playback.direction", app->GravityDirection()[axis],
                            pose.direction[axis], sample, axis);
        }
        std::fprintf(report, "BRIDGE gravity playback frame=25 noise=%d\n", app->GravityNoise());
        if (dialog)
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        app->state.currentFrame = savedFrame;
        app->PlaybackCursorSeconds() = savedCursor;
    } else {
        result.Present("gravity.timeline", false);
    }
    std::copy(bones.begin(), bones.end(), model.boneTable);
    std::copy(morphs.begin(), morphs.end(), model.morphs);
    std::copy(ik.begin(), ik.end(), model.ikChains);
    std::copy(order.begin(), order.end(), model.boneOrderTable);
    std::copy(boneCursors.begin(), boneCursors.end(), model.boneKeyCursors);
    std::copy(boneActive.begin(), boneActive.end(), model.boneTrackActive);
    std::copy(morphCursors.begin(), morphCursors.end(), model.morphKeyCursors);
    std::copy(morphActive.begin(), morphActive.end(), model.morphTrackActive);
    model.loadComplete = visibility;
    model.displayKeyCursor = displayCursor;
    model.displayTrackActive = displayActive;
    std::fprintf(report, "%s checks=%u failures=%u\n", result.failures ? "FAIL" : "PASS",
                 result.checks, result.failures);
    std::fclose(report);
}
}
