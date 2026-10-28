#include "mikudancestudio/model_geometry.hpp"
#include <cstring>

namespace mikudancestudio::format {
namespace {
bool Read(ReadModelBytes read, void* context, void* destination, std::size_t size) {
    return read != nullptr && read(context, destination, size);
}
bool ReadUnsigned(ReadModelBytes read, void* context, std::uint8_t width, std::uint32_t& value) {
    if (width != 1 && width != 2 && width != 4)
        return false;
    std::uint8_t bytes[4]{};
    if (!Read(read, context, bytes, width))
        return false;
    value = 0;
    for (std::uint8_t index = 0; index < width; ++index)
        value |= static_cast<std::uint32_t>(bytes[index]) << (8 * index);
    return true;
}
bool ReadFloat(ReadModelBytes read, void* context, float& value) {
    std::uint32_t bits;
    if (!ReadUnsigned(read, context, 4, bits))
        return false;
    static_assert(sizeof value == sizeof bits);
    std::memcpy(&value, &bits, sizeof value);
    return true;
}
bool ReadFloats(ReadModelBytes read, void* context, float* values, int count) {
    for (int index = 0; index < count; ++index)
        if (!ReadFloat(read, context, values[index]))
            return false;
    return true;
}
bool ReadSignedIndex(ReadModelBytes read, void* context, std::uint8_t width, std::int32_t& value) {
    std::uint32_t bits;
    if (!ReadUnsigned(read, context, width, bits))
        return false;
    if (width == 4)
        std::memcpy(&value, &bits, sizeof value);
    else
        value = width == 1 ? static_cast<std::int32_t>(bits ^ 0x80u) - 0x80
                           : static_cast<std::int32_t>(bits ^ 0x8000u) - 0x8000;
    return true;
}
} // namespace

bool ReadGeometryCount(ReadModelBytes read, void* context, std::uint32_t& count) {
    return ReadUnsigned(read, context, 4, count);
}
bool ReadPmdVertex(ReadModelBytes read, void* context, PmdVertexData& vertex) {
    PmdVertexData parsed;
    std::uint32_t bone;
    if (!ReadFloats(read, context, parsed.position, 3) ||
        !ReadFloats(read, context, parsed.normal, 3) || !ReadFloats(read, context, parsed.uv, 2))
        return false;
    for (auto& index : parsed.bone) {
        if (!ReadUnsigned(read, context, 2, bone))
            return false;
        index = static_cast<std::uint16_t>(bone);
    }
    if (!Read(read, context, &parsed.weightPercent, 1) ||
        !Read(read, context, &parsed.edgeDisabled, 1))
        return false;
    vertex = parsed;
    return true;
}
bool ReadPmdIndex(ReadModelBytes read, void* context, std::uint16_t& index) {
    std::uint32_t value;
    if (!ReadUnsigned(read, context, 2, value))
        return false;
    index = static_cast<std::uint16_t>(value);
    return true;
}
bool ReadPmxVertex(ReadModelBytes read, void* context, std::uint8_t additionalUvCount,
                   std::uint8_t boneIndexSize, PmxVertexData& vertex) {
    if (additionalUvCount > 4 || (boneIndexSize != 1 && boneIndexSize != 2 && boneIndexSize != 4))
        return false;
    PmxVertexData parsed;
    if (!ReadFloats(read, context, parsed.position, 3) ||
        !ReadFloats(read, context, parsed.normal, 3) || !ReadFloats(read, context, parsed.uv, 2))
        return false;
    for (std::uint8_t uv = 0; uv < additionalUvCount; ++uv)
        for (int component = 0; component < 4; ++component)
            if (!ReadFloat(read, context, parsed.additionalUv[component][uv]))
                return false;
    if (!Read(read, context, &parsed.weightType, 1))
        return false;
    const int boneCount = parsed.weightType == 0
                              ? 1
                              : (parsed.weightType == 1 || parsed.weightType == 3 ? 2
                                 : parsed.weightType == 2                         ? 4
                                                                                  : 0);
    for (int bone = 0; bone < boneCount; ++bone)
        if (!ReadSignedIndex(read, context, boneIndexSize, parsed.bone[bone]))
            return false;
    const int weightCount = parsed.weightType == 1 || parsed.weightType == 3 ? 1
                            : parsed.weightType == 2                         ? 4
                                                                             : 0;
    if (!ReadFloats(read, context, parsed.weight, weightCount))
        return false;
    if (parsed.weightType == 3 &&
        (!ReadFloats(read, context, parsed.center, 3) || !ReadFloats(read, context, parsed.r0, 3) ||
         !ReadFloats(read, context, parsed.r1, 3)))
        return false;
    if (!ReadFloat(read, context, parsed.edgeScale))
        return false;
    vertex = parsed;
    return true;
}
bool ReadPmxVertexIndex(ReadModelBytes read, void* context, std::uint8_t indexSize,
                        std::int32_t& index) {
    std::uint32_t value;
    if (!ReadUnsigned(read, context, indexSize, value))
        return false;
    if (indexSize == 4)
        std::memcpy(&index, &value, sizeof index);
    else
        index = static_cast<std::int32_t>(value);
    return true;
}
} // namespace mikudancestudio::format
