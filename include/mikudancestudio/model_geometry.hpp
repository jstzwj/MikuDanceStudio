#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "mikudancestudio/model_format_header.hpp"

namespace mikudancestudio::format {
struct PmdVertexData {
    float position[3]{};
    float normal[3]{};
    float uv[2]{};
    std::uint16_t bone[2]{};
    std::uint8_t weightPercent = 0;
    std::uint8_t edgeDisabled = 0;
};
struct PmxVertexData {
    float position[3]{};
    float normal[3]{};
    float uv[2]{};
    float additionalUv[4][4]{};
    std::uint8_t weightType = 0;
    std::int32_t bone[4]{};
    float weight[4]{};
    float center[3]{};
    float r0[3]{};
    float r1[3]{};
    float edgeScale = 0;
};

bool ReadGeometryCount(ReadModelBytes, void*, std::uint32_t&);
bool ReadPmdVertex(ReadModelBytes, void*, PmdVertexData&);
bool ReadPmdIndex(ReadModelBytes, void*, std::uint16_t&);
bool ReadPmxVertex(ReadModelBytes, void*, std::uint8_t additionalUvCount,
                   std::uint8_t boneIndexSize, PmxVertexData&);
bool ReadPmxVertexIndex(ReadModelBytes, void*, std::uint8_t indexSize, std::int32_t&);
} // namespace mikudancestudio::format
