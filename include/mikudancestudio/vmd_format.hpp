#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace mikudancestudio::format {

enum class VmdVersion {
    V1,
    V2
};

struct VmdBoneKey {
    std::array<char, 15> name{};
    std::uint32_t frame = 0;
    float position[3]{};
    float rotation[4]{};
    std::array<std::uint8_t, 64> interpolation{};
};

struct VmdMorphKey {
    std::array<char, 15> name{};
    std::uint32_t frame = 0;
    float value = 0.0f;
};

struct VmdCameraKey {
    std::uint32_t frame = 0;
    float distance = 0.0f;
    float eye[3]{};
    float target[3]{};
    // File order: x1, x2, y1, y2 for each of six channels.
    std::uint8_t interpolation[6][4]{};
    std::uint32_t fov = 45;
    std::uint8_t perspective = 0;
};

struct VmdLightKey {
    std::uint32_t frame = 0;
    float color[3]{};
    float direction[3]{};
};

struct VmdShadowKey {
    std::uint32_t frame = 0;
    std::uint8_t mode = 0;
    float distance = 0.0f;
};

struct VmdIkEntry {
    std::array<char, 20> name{};
    std::uint8_t enabled = 0;
};

struct VmdIkKey {
    std::uint32_t frame = 0;
    std::uint8_t visible = 0;
    std::vector<VmdIkEntry> entries;
};

// Disk data only: no path, window, model record, or registrar state.
struct VmdDocument {
    VmdVersion version = VmdVersion::V2;
    std::array<char, 20> modelName{};
    bool hasCameraSection = true;
    std::vector<VmdBoneKey> bones;
    std::vector<VmdMorphKey> morphs;
    std::vector<VmdCameraKey> cameras;
    std::vector<VmdLightKey> lights;
    std::vector<VmdShadowKey> shadows;
    std::vector<VmdIkKey> ik;
};

bool ReadVmd(std::FILE* stream, VmdDocument& document);
bool WriteVmd(std::FILE* stream, const VmdDocument& document);

} // namespace mikudancestudio::format
