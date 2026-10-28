#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstring>
#include <memory>
#include <vector>
#include <fcntl.h>
#include <io.h>

#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/pmm_scene_document.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "pmm_format_win.hpp"

int main() {
    using namespace mikudancestudio;
    auto app = std::make_unique<MMDApp>();
    std::memset(&app->state, 0, sizeof app->state);
    auto cameras = std::make_unique<std::vector<mdl::CameraKey>>(mdl::kTimelineKeyCapacity);
    auto lights = std::make_unique<std::vector<mdl::LightKey>>(mdl::kTimelineKeyCapacity);
    auto gravity = std::make_unique<std::vector<mdl::GravityKey>>(mdl::kTimelineKeyCapacity);
    auto shadows = std::make_unique<std::vector<mdl::SelfShadowKey>>(mdl::kTimelineKeyCapacity);
    app->CameraKeys() = cameras->data();
    app->LightKeys() = lights->data();
    app->GravityKeys() = gravity->data();
    app->ShadowKeys() = shadows->data();
    (*cameras)[0].next = 1;
    (*cameras)[1].frame = 1;
    (*cameras)[1].previous = 0;
    (*cameras)[1].next = 2;
    (*cameras)[1].selected = 1;
    (*cameras)[2].frame = 122;
    (*cameras)[2].previous = 1;
    ShiftCameraKeysAfterFrameDelete(cameras->data(), 0);
    if ((*cameras)[0].next != 2 || (*cameras)[1].frame != 0 ||
        (*cameras)[1].selected != 0 || (*cameras)[1].next != 0 ||
        (*cameras)[2].previous != 0 || (*cameras)[2].frame != 121)
        return 12;
    ShiftCameraKeysAfterFrameDelete(cameras->data(), 121);
    if ((*cameras)[0].next != 0 || (*cameras)[2].frame != 0)
        return 13;
    (*cameras)[1] = {};
    (*cameras)[2] = {};
    // Legacy state may still contain a selected, freed camera slot with stale
    // links. Purging that slot must preserve the live head -> key 3 chain.
    (*cameras)[0].next = 3;
    (*cameras)[3].frame = 20;
    (*cameras)[3].previous = 0;
    (*cameras)[1].selected = 1;
    (*cameras)[1].previous = 0;
    (*cameras)[1].next = 2;
    DeleteSelectedCameraKeys(cameras->data());
    if ((*cameras)[0].next != 3 || (*cameras)[1].selected != 0 ||
        (*cameras)[3].previous != 0) return 10;
    (*cameras)[3].selected = 1;
    DeleteSelectedCameraKeys(cameras->data());
    if ((*cameras)[0].next != 0 || (*cameras)[3].frame != 0) return 11;
    (*cameras)[1] = {};
    (*cameras)[3] = {};
    (*cameras)[0].fov = 30;
    (*cameras)[4].frame = 18;
    (*cameras)[4].distance = -15.0f;
    (*lights)[0].color[0] = 0.25f;
    (*gravity)[0].acceleration = 9.8f;
    (*shadows)[0].mode = 1;
    app->state.renderW = 640;
    app->state.renderH = 480;
    app->state.currentFrame = 18;
    app->SceneModified() = 1;

    HWND main = CreateWindowExW(0, L"STATIC", L"PMM save test", WS_OVERLAPPED,
                                0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    if (!main) return 1;
    HWND models = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD,
                                  0, 0, 1, 1, main,
                                  reinterpret_cast<HMENU>(
                                      static_cast<INT_PTR>(panel::kMainComboModel)),
                                  nullptr, nullptr);
    HWND accessories = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD,
                                       0, 0, 1, 1, main,
                                       reinterpret_cast<HMENU>(
                                           static_cast<INT_PTR>(panel::kAccessoryCombo)),
                                       nullptr, nullptr);
    if (!models || !accessories ||
        SendMessageA(models, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("camera")) < 0)
        return 2;
    app->Hwnd() = main;
    wchar_t directory[MAX_PATH]{}, path[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, directory) ||
        !GetTempFileNameW(directory, L"pmm", 0, path)) return 3;
    wcscpy_s(app->EnvFileName(), 256, path);
    SaveSceneFile(app.get());
    int fd = -1;
    const bool opened = _wsopen_s(&fd, path, _O_RDONLY | _O_BINARY,
                                  _SH_DENYNO, 0) == 0;
    format::PmmSceneDocument scene;
    const bool parsed = opened && format::ReadPmmSceneDocument(
        ReadPmmFileBytesResult, &fd, nullptr, nullptr, scene);
    if (fd >= 0) _close(fd);
    if (!parsed || scene.envelope.header.version != format::PmmVersion::V2 ||
        !scene.models.empty() || !scene.accessories.empty() ||
        scene.envelope.settings.renderWidth != 640 ||
        scene.timeline.currentFrame != 18 ||
        scene.cameraKeys.sparse.size() != 1 ||
        scene.cameraKeys.sparse[0].index != 4 ||
        scene.cameraKeys.sparse[0].value.distance != -15.0f ||
        scene.lightKeys.dense[0].color[0] != 0.25f ||
        scene.gravityKeys.dense[0].acceleration != 9.8f ||
        scene.selfShadowKeys.dense[0].mode != 1) return 4;
    // A stale predecessor on the head must be repaired before writing.
    (*cameras)[0].previous = 4;
    (*cameras)[0].next = 4;
    (*cameras)[4].previous = 0;
    SaveSceneFile(app.get());
    fd = -1;
    const bool reopened = _wsopen_s(&fd, path, _O_RDONLY | _O_BINARY,
                                   _SH_DENYNO, 0) == 0;
    format::PmmSceneDocument repaired;
    const bool repairedParsed = reopened && format::ReadPmmSceneDocument(
        ReadPmmFileBytesResult, &fd, nullptr, nullptr, repaired);
    if (fd >= 0) _close(fd);
    DeleteFileW(path);
    if (!repairedParsed || repaired.cameraKeys.dense[0].previous != 0 ||
        repaired.cameraKeys.dense[0].next != 4 ||
        repaired.cameraKeys.sparse.size() != 1 ||
        repaired.cameraKeys.sparse[0].value.previous != 0) return 9;

    auto model = std::make_unique<mdl::ModelRecord>();
    auto boneKeys = std::make_unique<std::vector<mdl::BoneKey>>(mdl::kBoneKeyCapacity);
    auto morphKeys = std::make_unique<std::vector<mdl::MorphKey>>(mdl::kMorphKeyCapacity);
    auto displayKeys = std::make_unique<std::vector<mdl::DisplayKey>>(mdl::kDisplayKeyCapacity);
    mdl::BoneRecord bones[1]{};
    mdl::MorphRecord morphs[1]{};
    mdl::IkChain ik[1]{};
    mdl::BoneOrderEntry bindings[1]{};
    unsigned char boneSelection[1]{1}, bonePhysics[1]{1};
    unsigned char ikStates[1]{1};
    mdl::BoneReference rigidStates[1]{{2, 3}};
    std::memcpy(model->name, "synthetic", 9);
    std::memcpy(bones[0].name, "center", 6);
    std::memcpy(morphs[0].name, "smile", 5);
    model->boneCount = 1;
    model->morphCount = 1;
    model->ikChainCount = 1;
    model->boneOrderCount = 1;
    model->boneTable = bones;
    model->morphs = morphs;
    model->ikChains = ik;
    model->boneOrderTable = bindings;
    model->boneKeys = boneKeys->data();
    model->morphKeys = morphKeys->data();
    model->displayKeys = displayKeys->data();
    model->boneSelection = boneSelection;
    model->bonePhysicsState = bonePhysics;
    model->maxFrame = 120;
    model->frameRegistrationSelection = 7;
    bones[0].trans[0] = 1.25f;
    bones[0].rotQuat[3] = 1.0f;
    bones[0].physicsDisabled = 1;
    morphs[0].value = 0.75f;
    ik[0].boneIndex = 0;
    ik[0].enabled = 1;
    bindings[0].boneIndex = 0;
    bindings[0].windowStart = 2;
    bindings[0].windowEnd = 40;
    bindings[0].linkedModel = 3;
    bindings[0].linkedBone = 4;
    (*boneKeys)[0].frame = 11;
    (*boneKeys)[0].position[0] = 2.5f;
    (*boneKeys)[0].rotation[3] = 1.0f;
    (*boneKeys)[0].allocated = 1;
    (*boneKeys)[0].physicsDisabled = 1;
    (*morphKeys)[0].frame = 12;
    (*morphKeys)[0].value = 0.5f;
    (*morphKeys)[0].allocated = 1;
    (*displayKeys)[0].frame = 13;
    (*displayKeys)[0].visible = 1;
    (*displayKeys)[0].allocated = 1;
    (*displayKeys)[0].ikStates = ikStates;
    (*displayKeys)[0].selectorStates = rigidStates;
    app->ModelSlots()[0] = model.get();
    if (SendMessageA(models, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("synthetic")) < 0 ||
        !GetTempFileNameW(directory, L"pmm", 0, path)) return 5;
    wcscpy_s(app->EnvFileName(), 256, path);
    app->SceneModified() = 1;
    SaveSceneFile(app.get());
    fd = -1;
    const bool modelOpened = _wsopen_s(&fd, path, _O_RDONLY | _O_BINARY,
                                       _SH_DENYNO, 0) == 0;
    format::PmmSceneDocument modelScene;
    const bool modelParsed = modelOpened && format::ReadPmmSceneDocument(
        ReadPmmFileBytesResult, &fd, nullptr, nullptr, modelScene);
    if (fd >= 0) _close(fd);
    DeleteFileW(path);
    app->ModelSlots()[0] = nullptr;
    DestroyWindow(main);
    app->CameraKeys() = nullptr;
    app->LightKeys() = nullptr;
    app->GravityKeys() = nullptr;
    app->ShadowKeys() = nullptr;
    if (!modelParsed || modelScene.models.size() != 1 ||
        modelScene.modelRegistrations.size() != 1 ||
        app->SceneModified() != 0 || app->state.windowLayoutReady != 1) return 6;
    const auto& saved = modelScene.models[0];
    return saved.slot == 0 && saved.name.size() == 9 &&
           std::memcmp(saved.name.data(), "synthetic", 9) == 0 &&
           saved.layout.boneCount == 1 && saved.layout.morphCount == 1 &&
           saved.layout.ikCount == 1 && saved.rigidCount == 1 &&
           saved.selection.maxFrame == 120 &&
           saved.boneKeys.dense[0].frame == 11 &&
           saved.boneKeys.dense[0].position[0] == 2.5f &&
           saved.boneKeys.dense[0].physicsDisabled == 1 &&
           saved.morphKeys.dense[0].frame == 12 &&
           saved.morphKeys.dense[0].value == 0.5f &&
           saved.displayKeys.dense[0].frame == 13 &&
           saved.displayKeys.dense[0].ikEnabled[0] == 1 &&
           saved.displayKeys.dense[0].rigidStates[0].modelIndex == 2 &&
           saved.displayKeys.dense[0].rigidStates[0].boneIndex == 3 &&
           saved.bonePose[0].components[0] == 1.25f &&
           saved.bonePose[0].flags[0] == 1 &&
           saved.morphPose[0] == 0.75f && saved.ikPose[0] == 1 &&
           saved.rigidPose[0].values[0] == 2 &&
           modelScene.modelRegistrations[0].selection == 7 ? 0 : 7;
}
