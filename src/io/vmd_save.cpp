// Windows adapter for VMD2 saving. The file layout lives in vmd_format.cpp.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <utility>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/vmd_format.hpp"

namespace mikudancestudio {
namespace {

const char kCameraLightName[] = "\x83\x4a\x83\x81\x83\x89\x81\x45\x8f\xc6\x96\xbe";
const char kJpCannotSave[] = "\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xaa\x95\xdb\x91\xb6\x82\xc5\x82"
                             "\xab\x82\xdc\x82\xb9\x82\xf1:%d";

format::VmdDocument BuildVmdDocument(MMDApp* app) {
    format::VmdDocument document;
    const bool cameraMode = app->state.optflag[0] != 0;
    std::uint32_t minFrame = UINT32_MAX;
    if (cameraMode) {
        std::memcpy(document.modelName.data(), kCameraLightName, sizeof kCameraLightName);
        const mdl::CameraKey* cameras = app->CameraKeys();
        for (unsigned i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
            const mdl::CameraKey& source = cameras[i];
            if (source.selected == 0)
                continue;
            format::VmdCameraKey key;
            key.frame = source.frame;
            key.distance = source.distance;
            std::memcpy(key.eye, source.eye, sizeof key.eye);
            std::memcpy(key.target, source.target, sizeof key.target);
            for (int channel = 0; channel < 6; ++channel) {
                key.interpolation[channel][0] = source.interpolation[0][channel];
                key.interpolation[channel][1] = source.interpolation[2][channel];
                key.interpolation[channel][2] = source.interpolation[1][channel];
                key.interpolation[channel][3] = source.interpolation[3][channel];
            }
            key.fov = static_cast<std::uint32_t>(source.fov);
            key.perspective = source.perspective != 0 ? 1 : 0;
            minFrame = std::min(minFrame, key.frame);
            document.cameras.push_back(key);
        }
        const mdl::LightKey* lights = app->LightKeys();
        for (unsigned i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
            const mdl::LightKey& source = lights[i];
            if (source.selected == 0)
                continue;
            format::VmdLightKey key;
            key.frame = source.frame;
            std::memcpy(key.color, source.color, sizeof key.color);
            std::memcpy(key.direction, source.direction, sizeof key.direction);
            minFrame = std::min(minFrame, key.frame);
            document.lights.push_back(key);
        }
        const mdl::SelfShadowKey* shadows = app->ShadowKeys();
        for (unsigned i = 0; i < mdl::kTimelineKeyCapacity; ++i) {
            const mdl::SelfShadowKey& source = shadows[i];
            if (source.selected == 0)
                continue;
            format::VmdShadowKey key;
            key.frame = source.frame;
            key.mode = source.mode;
            key.distance = source.distance;
            minFrame = std::min(minFrame, key.frame);
            document.shadows.push_back(key);
        }
    } else {
        mdl::ModelRecord* model = app->SelectedModel();
        const mdl::ModelRecord* record = model;
        std::memcpy(document.modelName.data(), record->name, document.modelName.size());
        const mdl::BoneRecord* bones = (model)->boneTable;
        const mdl::MorphRecord* morphs = (model)->morphs;
        const mdl::IkChain* chains = (model)->ikChains;
        const mdl::BoneKey* boneKeys = (model)->boneKeys;
        for (unsigned i = 0; i < mdl::kBoneKeyCapacity; ++i) {
            const mdl::BoneKey& source = boneKeys[i];
            if (source.allocated == 0)
                continue;
            unsigned index = i;
            while (index >= static_cast<unsigned>(record->boneCount))
                index = boneKeys[index].previous;
            format::VmdBoneKey key;
            std::memcpy(key.name.data(), bones[index].name, key.name.size());
            key.frame = source.frame;
            std::memcpy(key.position, source.position, sizeof key.position);
            std::memcpy(key.rotation, source.rotation, sizeof key.rotation);
            // The legacy writer's overlapping reads extend into position.
            const auto* raw = reinterpret_cast<const std::uint8_t*>(&source);
            const std::size_t base = offsetof(mdl::BoneKey, interpolation);
            for (int channel = 0; channel < 4; ++channel) {
                std::uint8_t* out = key.interpolation.data() + channel * 16;
                if (channel == 0) {
                    std::memcpy(out, raw + base, 2);
                    const std::uint16_t marker = source.physicsDisabled != 0 ? 0x0F63 : 0;
                    out[2] = static_cast<std::uint8_t>(marker);
                    out[3] = static_cast<std::uint8_t>(marker >> 8);
                } else {
                    std::memcpy(out, raw + base + channel, 4);
                }
                for (int row = 1; row < 4; ++row)
                    std::memcpy(out + row * 4, raw + base + row * 4 + channel, 4);
            }
            minFrame = std::min(minFrame, key.frame);
            document.bones.push_back(key);
        }
        const mdl::MorphKey* morphKeys = (model)->morphKeys;
        for (unsigned i = 0; i < mdl::kMorphKeyCapacity; ++i) {
            const mdl::MorphKey& source = morphKeys[i];
            if (source.allocated == 0)
                continue;
            unsigned index = i;
            while (index >= static_cast<unsigned>(record->morphCount))
                index = morphKeys[index].previous;
            format::VmdMorphKey key;
            std::memcpy(key.name.data(), morphs[index].name, key.name.size());
            key.frame = source.frame;
            key.value = source.value;
            minFrame = std::min(minFrame, key.frame);
            document.morphs.push_back(key);
        }
        const mdl::DisplayKey* displayKeys = (model)->displayKeys;
        for (unsigned i = 0; i < mdl::kDisplayKeyCapacity; ++i) {
            const mdl::DisplayKey& source = displayKeys[i];
            if (source.allocated == 0)
                continue;
            format::VmdIkKey key;
            key.frame = source.frame;
            key.visible = source.visible != 0 ? 1 : 0;
            for (unsigned j = 0; j < record->ikChainCount; ++j) {
                format::VmdIkEntry entry;
                std::memcpy(entry.name.data(), bones[chains[j].boneIndex].name, entry.name.size());
                entry.enabled = mdl::IkStates(source)[j] != 0 ? 1 : 0;
                key.entries.push_back(entry);
            }
            minFrame = std::min(minFrame, key.frame);
            document.ik.push_back(std::move(key));
        }
    }
    if (minFrame != UINT32_MAX) {
        for (auto& key : document.bones)
            key.frame -= minFrame;
        for (auto& key : document.morphs)
            key.frame -= minFrame;
        for (auto& key : document.cameras)
            key.frame -= minFrame;
        for (auto& key : document.lights)
            key.frame -= minFrame;
        for (auto& key : document.shadows)
            key.frame -= minFrame;
        for (auto& key : document.ik)
            key.frame -= minFrame;
    }
    return document;
}

} // namespace

void SaveVmdFile(const wchar_t* path) {
    MMDApp* app = g_Block;
    if (app == nullptr)
        return;
    format::VmdDocument document = BuildVmdDocument(app);
    int fd;
    const errno_t error = _wsopen_s(&fd, path, 0x8301, 0x40, 0x80);
    if (error != 0) {
        char text[256];
        if (app->state.englishUI != 0)
            sprintf_s(text, sizeof text, "Cannot save file:%d", error);
        else
            sprintf_s(text, sizeof text, kJpCannotSave, error);
        MessageBoxA(static_cast<HWND>(app->Hwnd()), text, "", 0);
        return;
    }
    std::FILE* stream = _fdopen(fd, "wb");
    if (stream == nullptr) {
        _close(fd);
        return;
    }
    format::WriteVmd(stream, document);
    std::fclose(stream);
}

} // namespace mikudancestudio
