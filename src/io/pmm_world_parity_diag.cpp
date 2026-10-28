#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <memory>
#include <share.h>
#include <sys/stat.h>
#include <string>
#include <vector>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/render_snapshot.hpp"
#include "mikudancestudio/scene_session.hpp"
#include "pmm_format_win.hpp"

namespace mikudancestudio {
namespace {
std::unique_ptr<format::SceneSession> preparedSession;
std::wstring preparedPath;
bool forceAtLoad = false;
int ReadModelBytes(void* context, void* destination, std::size_t length) {
    return _read(*static_cast<int*>(context), destination, static_cast<unsigned>(length));
}
bool ResolveLoadedModel(void* context, std::uint8_t slot, const std::array<char, 256>&,
                        format::ModelDocument& output) {
    auto* app = static_cast<MMDApp*>(context);
    if (slot >= kModelSlotCount || !app->ModelSlot(slot))
        return false;
    const auto* path = app->ModelSlot(slot)->path;
    int fd = -1;
    if (!path || !*path || _wsopen_s(&fd, path, _O_BINARY | _O_RDONLY, _SH_DENYNO, _S_IREAD) != 0)
        return false;
    const bool loaded = format::ReadModelDocument(ReadModelBytes, &fd, output);
    _close(fd);
    return loaded;
}
bool Near(float actual, float expected) {
    return std::isfinite(actual) && std::isfinite(expected) &&
           std::fabs(actual - expected) <=
               0.0001f * (std::max)({1.0f, std::fabs(actual), std::fabs(expected)});
}
std::array<float, 3> RuntimePoint(const mdl::BoneRecord& bone) {
    std::array<float, 3> point{};
    for (int axis = 0; axis < 3; ++axis) {
        point[axis] = bone.matInit[12 + axis];
        for (int column = 0; column < 3; ++column)
            point[axis] += bone.position[column] * bone.matInit[column * 4 + axis];
    }
    return point;
}
void Compare(FILE* report, const char* kind, unsigned frame, int slot, int bone,
             const float* actual, const float* expected, int count, unsigned& checks,
             unsigned& mismatches) {
    for (int element = 0; element < count; ++element) {
        ++checks;
        if (Near(actual[element], expected[element]))
            continue;
        ++mismatches;
        std::fprintf(report,
                     "WORLD MISMATCH kind=%s frame=%u slot=%d bone=%d element=%d "
                     "runtime=%g portable=%g delta=%g\n",
                     kind, frame, slot, bone, element, static_cast<double>(actual[element]),
                     static_cast<double>(expected[element]),
                     static_cast<double>(actual[element] - expected[element]));
    }
}
}

void PreparePmmWorldParityReport(MMDApp* app) {
    wchar_t path[1024]{};
    if (!GetEnvironmentVariableW(L"MDS_PMM_WORLD_REPORT", path, 1024) ||
        !app->PortableSceneSession())
        return;
    auto session = std::make_unique<format::SceneSession>();
    int fd = -1;
    const bool loaded =
        _wsopen_s(&fd, app->EnvFileName(), _O_BINARY | _O_RDONLY, _SH_DENYNO, _S_IREAD) == 0 &&
        format::SceneSession::Load(ReadPmmFileBytesResult, &fd, ResolveLoadedModel, app, *session);
    if (fd >= 0)
        _close(fd);
    preparedPath = app->EnvFileName();
    preparedSession = loaded ? std::move(session) : nullptr;
}

void WritePmmWorldParityReport(MMDApp* app) {
    wchar_t path[1024]{};
    if (!GetEnvironmentVariableW(L"MDS_PMM_WORLD_REPORT", path, 1024) || !preparedSession ||
        preparedPath != app->EnvFileName())
        return;
    static std::wstring reportedScene;
    static std::wstring pendingScene;
    static unsigned renderedFrames = 0;
    const std::wstring scenePath = app->EnvFileName();
    if (scenePath.empty() || reportedScene == scenePath)
        return;
    if (pendingScene != scenePath) {
        pendingScene = scenePath;
        renderedFrames = 0;
    }
    if (!forceAtLoad && ++renderedFrames < 5)
        return;
    FILE* report = nullptr;
    if (_wfopen_s(&report, path, L"wb") != 0 || !report)
        return;
    auto& session = *preparedSession;
    const unsigned frame = static_cast<unsigned>(app->CurrentFrame());
    if (!session.Seek(frame)) {
        std::fprintf(report, "WORLD UNAVAILABLE frame=%u scene-or-model\n", frame);
        std::fclose(report);
        return;
    }
    render::SceneSnapshot snapshot;
    if (!render::CaptureSceneSnapshot(session, snapshot)) {
        std::fprintf(report, "WORLD UNAVAILABLE frame=%u snapshot\n", frame);
        std::fclose(report);
        return;
    }
    reportedScene = scenePath;
    unsigned checks = 0, mismatches = 0, supported = 0, skipped = 0;
    unsigned leftBones = 0, leftPoseMismatches = 0;
    unsigned cameraCompared = 0, accessoriesCompared = 0;
    unsigned attachedAccessories = 0;
    const auto& models = session.Document().models;
    for (std::size_t modelIndex = 0; modelIndex < models.size(); ++modelIndex) {
        const auto slot = models[modelIndex].slot;
        auto* raw = app->ModelSlot(slot);
        if (!raw)
            continue;
        const auto& runtime = *raw;
        const auto& portableModel = snapshot.models[modelIndex];
        const auto& world = portableModel.bones;
        if (portableModel.slot != slot || world.size() != runtime.boneCount) {
            std::fprintf(report, "WORLD UNAVAILABLE frame=%u slot=%u bones\n", frame, slot);
            continue;
        }
        std::fprintf(report,
                     "SNAPSHOT MODEL frame=%u slot=%u visible=%u vertices=%u "
                     "indices=%u batches=%zu\n",
                     frame, slot, portableModel.visible ? 1u : 0u, portableModel.vertexCount,
                     portableModel.indexCount, portableModel.batches.size());
        for (std::size_t index = 0; index < world.size(); ++index) {
            const auto& bone = runtime.boneTable[index];
            const auto& pose = session.Pose().models[modelIndex].bones[index];
            char nameBytes[sizeof(bone.name) + 1]{};
            char englishName[sizeof(bone.nameEn) + 1]{};
            std::memcpy(nameBytes, bone.name, sizeof(bone.name));
            std::memcpy(englishName, bone.nameEn, sizeof(bone.nameEn));
            wchar_t name[32]{};
            MultiByteToWideChar(932, 0, nameBytes, -1, name, 32);
            if (std::wcschr(name, L'\u5de6') || std::strstr(englishName, "Left") ||
                std::strstr(englishName, "left")) {
                ++leftBones;
                float poseDelta = 0;
                for (int axis = 0; axis < 3; ++axis)
                    poseDelta =
                        (std::max)(poseDelta, std::fabs(bone.trans[axis] - pose.translation[axis]));
                for (int axis = 0; axis < 4; ++axis)
                    poseDelta =
                        (std::max)(poseDelta, std::fabs(bone.rotQuat[axis] - pose.rotation[axis]));
                if (poseDelta > 0.0001f)
                    ++leftPoseMismatches;
                const auto anchor = RuntimePoint(bone);
                std::fprintf(report,
                             "WORLD LEFT frame=%u slot=%u bone=%zu nameEn=%s "
                             "poseDelta=%g supported=%d parent=%d type=%d rigid=%u "
                             "anchor=(%g,%g,%g)\n",
                             frame, slot, index, englishName, static_cast<double>(poseDelta),
                             world[index].has_value(), bone.parent, static_cast<int>(bone.type),
                             bone.hasRigidBody, static_cast<double>(anchor[0]),
                             static_cast<double>(anchor[1]), static_cast<double>(anchor[2]));
                if (poseDelta > 0.0001f)
                    for (int axis = 0; axis < 4; ++axis)
                        std::fprintf(report,
                                     "WORLD LEFT POSE frame=%u slot=%u bone=%zu "
                                     "element=%d runtime=%g portable=%g\n",
                                     frame, slot, index, axis,
                                     static_cast<double>(bone.rotQuat[axis]),
                                     static_cast<double>(pose.rotation[axis]));
            }
            if (!world[index]) {
                ++skipped;
                continue;
            }
            ++supported;
            for (int axis = 0; axis < 3; ++axis)
                if (!Near(bone.trans[axis], pose.translation[axis]))
                    std::fprintf(report,
                                 "WORLD POSE frame=%u slot=%u bone=%zu translation=%d "
                                 "runtime=%g portable=%g\n",
                                 frame, slot, index, axis, static_cast<double>(bone.trans[axis]),
                                 static_cast<double>(pose.translation[axis]));
            for (int axis = 0; axis < 4; ++axis)
                if (!Near(bone.rotQuat[axis], pose.rotation[axis]))
                    std::fprintf(report,
                                 "WORLD POSE frame=%u slot=%u bone=%zu rotation=%d "
                                 "runtime=%g portable=%g\n",
                                 frame, slot, index, axis, static_cast<double>(bone.rotQuat[axis]),
                                 static_cast<double>(pose.rotation[axis]));
            Compare(report, "matrix", frame, slot, static_cast<int>(index), bone.matInit,
                    world[index]->data(), 16, checks, mismatches);
            std::array<float, 3> anchor{};
            if (format::PmmAttachedBonePoint(*session.Models()[modelIndex], world,
                                             static_cast<int>(index), anchor)) {
                const auto live = RuntimePoint(bone);
                Compare(report, "anchor", frame, slot, static_cast<int>(index), live.data(),
                        anchor.data(), 3, checks, mismatches);
            }
        }
        std::fprintf(report, "WORLD MODEL frame=%u slot=%u bones=%u path=%ls\n", frame, slot,
                     runtime.boneCount, runtime.path);
    }
    const auto& camera = session.Pose().camera;
    const auto* cameraKey = app->CameraKeys();
    const bool cameraStateMatches = app->CameraParentModel() == camera.parentModel &&
                                    app->CameraParentBone() == camera.parentBone;
    const bool cameraHeadMatches = frame == 0 && cameraKey &&
                                   cameraKey[0].parentModel == camera.parentModel &&
                                   cameraKey[0].parentBone == camera.parentBone;
    std::fprintf(report,
                 "WORLD CAMERA frame=%u portable=(%d,%d) state=(%d,%d) "
                 "head=(%d,%d)\n",
                 frame, camera.parentModel, camera.parentBone, app->CameraParentModel(),
                 app->CameraParentBone(), cameraKey ? cameraKey[0].parentModel : -1,
                 cameraKey ? cameraKey[0].parentBone : -1);
    if (camera.parentModel >= 0 && camera.parentModel < kModelSlotCount &&
        (cameraStateMatches || cameraHeadMatches)) {
        const auto* raw = app->ModelSlot(camera.parentModel);
        if (snapshot.cameraAttachment && raw && camera.parentBone >= 0 &&
            static_cast<unsigned>(camera.parentBone) < raw->boneCount) {
            const auto live = RuntimePoint(raw->boneTable[camera.parentBone]);
            Compare(report, cameraStateMatches ? "camera-anchor" : "camera-key-anchor", frame,
                    camera.parentModel, camera.parentBone, live.data(),
                    snapshot.cameraAttachment->data(), 3, checks, mismatches);
            ++cameraCompared;
        }
    }
    for (const auto& accessory : session.Pose().accessories) {
        const auto* object = app->AccessorySlot(accessory.slot);
        if (!object)
            continue;
        format::PmmWorldMatrix portable{};
        const auto found =
            std::find_if(snapshot.accessories.begin(), snapshot.accessories.end(),
                         [&accessory](const auto& item) { return item.slot == accessory.slot; });
        if (found == snapshot.accessories.end() || !found->world)
            continue;
        portable = *found->world;
        auto& api = d3dx::Get();
        d3dx::D3DXMATRIXF live{}, part{}, current{};
        const float scale = object->scale * 10.0f;
        api.scaling(&live, scale, scale, scale);
        api.rotZ(&part, object->rotation[2]);
        api.multiply(&current, &live, &part);
        live = current;
        api.rotX(&part, object->rotation[0]);
        api.multiply(&current, &live, &part);
        live = current;
        api.rotY(&part, object->rotation[1]);
        api.multiply(&current, &live, &part);
        live = current;
        api.translation(&part, object->position[0], object->position[1], object->position[2]);
        api.multiply(&current, &live, &part);
        live = current;
        if (object->parentModel >= 0 && object->parentModel < kModelSlotCount) {
            const auto* raw = app->ModelSlot(object->parentModel);
            const int boneIndex = (std::max)(object->parentBone, 0);
            if (!raw || static_cast<unsigned>(boneIndex) >= raw->boneCount)
                continue;
            const auto& bone = raw->boneTable[boneIndex];
            api.translation(&part, bone.position[0], bone.position[1], bone.position[2]);
            api.multiply(&current, &live, &part);
            live = current;
            std::memcpy(&part, bone.matInit, sizeof(part));
            api.multiply(&current, &live, &part);
            live = current;
        }
        Compare(report, "accessory", frame, accessory.slot, accessory.parentBone, &live.m[0][0],
                portable.data(), 16, checks, mismatches);
        ++accessoriesCompared;
        if (object->parentModel >= 0)
            ++attachedAccessories;
    }
    std::fprintf(report,
                 "WORLD SUMMARY frame=%u supported=%u skipped=%u checks=%u "
                 "mismatches=%u camera=%u accessories=%u attachedAccessories=%u "
                 "leftBones=%u leftPoseMismatches=%u\n",
                 frame, supported, skipped, checks, mismatches, cameraCompared, accessoriesCompared,
                 attachedAccessories, leftBones, leftPoseMismatches);
    std::fclose(report);
}

void WritePmmWorldParityAtLoad(MMDApp* app) {
    if (!preparedSession || preparedPath != app->EnvFileName())
        return;
    struct Snapshot {
        mdl::ModelRecord* raw;
        std::vector<mdl::BoneRecord> bones;
        std::vector<mdl::IkChain> chains;
    };
    std::vector<Snapshot> snapshots;
    for (const auto& source : preparedSession->Document().models) {
        auto* raw = app->ModelSlot(source.slot);
        if (!raw)
            return;
        const auto& model = *raw;
        Snapshot snapshot{raw, {}, {}};
        if (model.boneCount)
            snapshot.bones.assign(model.boneTable, model.boneTable + model.boneCount);
        if (model.ikChainCount)
            snapshot.chains.assign(model.ikChains, model.ikChains + model.ikChainCount);
        snapshots.push_back(std::move(snapshot));
    }
    for (auto& snapshot : snapshots) {
        SetPhysicsMode(snapshot.raw, 0, app->ModelSlots(), 0);
        SetPhysicsMode(snapshot.raw, 1, app->ModelSlots(), 0);
    }
    forceAtLoad = true;
    WritePmmWorldParityReport(app);
    forceAtLoad = false;
    for (const auto& snapshot : snapshots) {
        auto& model = *snapshot.raw;
        if (!snapshot.bones.empty())
            std::copy(snapshot.bones.begin(), snapshot.bones.end(), model.boneTable);
        if (!snapshot.chains.empty())
            std::copy(snapshot.chains.begin(), snapshot.chains.end(), model.ikChains);
    }
}
}
