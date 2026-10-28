#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/vmd_format.hpp"

int main() {
    using namespace mikudancestudio;
    auto app = std::make_unique<MMDApp>();
    std::memset(&app->state, 0, sizeof app->state);
    g_Block = app.get();
    app->state.optflag[0] = 1;  // camera mode does not require a model
    std::vector<mdl::CameraKey> cameras(mdl::kTimelineKeyCapacity);
    std::vector<mdl::LightKey> lights(mdl::kTimelineKeyCapacity);
    std::vector<mdl::SelfShadowKey> shadows(mdl::kTimelineKeyCapacity);
    app->CameraKeys() = cameras.data();
    app->LightKeys() = lights.data();
    app->ShadowKeys() = shadows.data();
    cameras[1].selected = 1;
    cameras[1].frame = 45;
    cameras[1].distance = -20.0f;
    cameras[1].interpolation[2][5] = 76;
    cameras[1].fov = 32;
    lights[2].selected = 1;
    lights[2].frame = 47;
    lights[2].color[0] = 0.5f;
    shadows[3].selected = 1;
    shadows[3].frame = 44;
    shadows[3].mode = 2;

    wchar_t directory[MAX_PATH]{};
    wchar_t path[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, directory) ||
        !GetTempFileNameW(directory, L"vmd", 0, path)) return 1;
    SaveVmdFile(path);
    std::FILE* stream = nullptr;
    const bool opened = _wfopen_s(&stream, path, L"rb") == 0 && stream != nullptr;
    format::VmdDocument motion;
    const bool parsed = opened && format::ReadVmd(stream, motion);
    if (stream != nullptr) std::fclose(stream);
    DeleteFileW(path);
    g_Block = nullptr;
    app->CameraKeys() = nullptr;
    app->LightKeys() = nullptr;
    app->ShadowKeys() = nullptr;
    if (!parsed || !motion.bones.empty() || !motion.morphs.empty() ||
        !motion.ik.empty() || motion.cameras.size() != 1 ||
        motion.lights.size() != 1 || motion.shadows.size() != 1 ||
        motion.cameras[0].frame != 1 || motion.lights[0].frame != 3 ||
        motion.shadows[0].frame != 0 ||
        motion.cameras[0].interpolation[5][1] != 76 ||
        motion.cameras[0].fov != 32 || motion.lights[0].color[0] != 0.5f) {
        std::fputs("VMD Windows save adapter changed track data\n", stderr);
        return 1;
    }

    auto model = std::make_unique<mdl::ModelRecord>();
    auto boneKeys = std::make_unique<std::vector<mdl::BoneKey>>(mdl::kBoneKeyCapacity);
    std::vector<mdl::MorphKey> morphKeys(mdl::kMorphKeyCapacity);
    std::vector<mdl::DisplayKey> displayKeys(mdl::kDisplayKeyCapacity);
    mdl::BoneRecord bones[1]{};
    mdl::MorphRecord morphs[1]{};
    mdl::IkChain chains[1]{};
    unsigned char ikStates[1]{1};
    std::memcpy(model->name, "model", 5);
    std::memcpy(bones[0].name, "center", 6);
    std::memcpy(morphs[0].name, "smile", 5);
    model->boneCount = 1;
    model->morphCount = 1;
    model->ikChainCount = 1;
    model->boneTable = bones;
    model->morphs = morphs;
    model->ikChains = chains;
    model->boneKeys = boneKeys->data();
    model->morphKeys = morphKeys.data();
    model->displayKeys = displayKeys.data();
    chains[0].boneIndex = 0;
    auto& boneKey = (*boneKeys)[0];
    boneKey.allocated = 1;
    boneKey.frame = 100;
    boneKey.position[0] = 1.5f;
    boneKey.rotation[3] = 1.0f;
    boneKey.interpolation[0] = 20;
    boneKey.physicsDisabled = 1;
    morphKeys[0].allocated = 1;
    morphKeys[0].frame = 102;
    morphKeys[0].value = 0.75f;
    displayKeys[0].allocated = 1;
    displayKeys[0].frame = 101;
    displayKeys[0].visible = 1;
    displayKeys[0].ikStates = ikStates;
    app->state.optflag[0] = 0;
    app->SelectedModel() = model.get();
    g_Block = app.get();
    if (!GetTempFileNameW(directory, L"vmd", 0, path)) return 1;
    SaveVmdFile(path);
    stream = nullptr;
    const bool modelOpened = _wfopen_s(&stream, path, L"rb") == 0 &&
                             stream != nullptr;
    format::VmdDocument modelMotion;
    const bool modelParsed = modelOpened && format::ReadVmd(stream, modelMotion);
    if (stream != nullptr) std::fclose(stream);
    DeleteFileW(path);
    g_Block = nullptr;
    app->SelectedModel() = nullptr;
    if (!modelParsed || modelMotion.bones.size() != 1 ||
        modelMotion.morphs.size() != 1 || modelMotion.ik.size() != 1 ||
        modelMotion.bones[0].frame != 0 ||
        modelMotion.morphs[0].frame != 2 || modelMotion.ik[0].frame != 1 ||
        modelMotion.bones[0].position[0] != 1.5f ||
        modelMotion.bones[0].interpolation[0] != 20 ||
        modelMotion.bones[0].interpolation[2] != 0x63 ||
        modelMotion.bones[0].interpolation[3] != 0x0f ||
        modelMotion.ik[0].entries.size() != 1 ||
        modelMotion.ik[0].entries[0].enabled != 1) {
        std::fputs("VMD Windows model save changed bone, morph or IK data\n", stderr);
        return 1;
    }
    std::puts("VMD Windows save adapter passed");
}
