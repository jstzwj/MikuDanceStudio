#include "mikudancestudio/model_format_header.hpp"
#include <cstring>
#include <utility>
namespace mikudancestudio::format {
namespace {
bool Read(ReadModelBytes read, void* context, void* destination, std::size_t size) {
    return read != nullptr && (size == 0 || read(context, destination, size));
}
bool ReadU32(ReadModelBytes read, void* context, std::uint32_t& result) {
    std::uint8_t bytes[4];
    if (!Read(read, context, bytes, sizeof bytes))
        return false;
    result = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
             (static_cast<std::uint32_t>(bytes[2]) << 16) |
             (static_cast<std::uint32_t>(bytes[3]) << 24);
    return true;
}
bool ReadFloat(ReadModelBytes read, void* context, float& result) {
    static_assert(sizeof(float) == 4);
    std::uint32_t bits;
    if (!ReadU32(read, context, bits))
        return false;
    std::memcpy(&result, &bits, sizeof bits);
    return true;
}
} // namespace
ModelKind IdentifyModel(const std::array<char, 3>& signature) {
    if (signature[0] == 'P' && signature[1] == 'm' && signature[2] == 'd')
        return ModelKind::Pmd;
    if (signature[0] == 'P' && signature[1] == 'M' && signature[2] == 'X')
        return ModelKind::Pmx;
    return ModelKind::Unknown;
}
ModelHeaderStatus ReadPmdHeader(ReadModelBytes read, void* context, PmdHeader& header) {
    PmdHeader parsed;
    if (!ReadFloat(read, context, parsed.version) ||
        !Read(read, context, parsed.name.data(), parsed.name.size()) ||
        !Read(read, context, parsed.comment.data(), parsed.comment.size()))
        return ModelHeaderStatus::Truncated;
    header = parsed;
    return ModelHeaderStatus::Ok;
}
ModelHeaderStatus ReadPmxHeader(ReadModelBytes read, void* context, PmxHeader& header) {
    PmxHeader parsed;
    std::uint8_t prefix;
    if (!Read(read, context, &prefix, 1) || !ReadFloat(read, context, parsed.version))
        return ModelHeaderStatus::Truncated;
    if (parsed.version != 2.0f)
        return ModelHeaderStatus::InvalidVersion;
    std::uint8_t settings[9];
    if (!Read(read, context, settings, sizeof settings))
        return ModelHeaderStatus::Truncated;
    if (settings[0] != 8)
        return ModelHeaderStatus::InvalidSettings;
    parsed.textEncoding = settings[1];
    parsed.additionalUvCount = settings[2];
    parsed.vertexIndexSize = settings[3];
    parsed.textureIndexSize = settings[4];
    parsed.materialIndexSize = settings[5];
    parsed.boneIndexSize = settings[6];
    parsed.morphIndexSize = settings[7];
    parsed.rigidIndexSize = settings[8];
    header = parsed;
    return parsed.textEncoding == 0 ? ModelHeaderStatus::Ok
                                    : ModelHeaderStatus::UnsupportedEncoding;
}
ModelHeaderStatus ReadPmxTexts(ReadModelBytes read, void* context, PmxTexts& texts) {
    constexpr std::uint32_t kMaxTextBytes = 1 << 20;
    PmxTexts parsed;
    for (auto& field : parsed.fields) {
        std::uint32_t length;
        if (!ReadU32(read, context, length))
            return ModelHeaderStatus::Truncated;
        if (length > kMaxTextBytes || (length & 1) != 0)
            return ModelHeaderStatus::InvalidSettings;
        field.resize(length);
        if (!Read(read, context, field.data(), field.size()))
            return ModelHeaderStatus::Truncated;
    }
    texts = std::move(parsed);
    return ModelHeaderStatus::Ok;
}
ModelHeaderStatus ReadPmxText(ReadModelBytes read, void* context,
                              std::vector<std::uint8_t>& output) {
    std::uint32_t length = 0;
    if (!ReadU32(read, context, length))
        return ModelHeaderStatus::Truncated;
    if (length > (1u << 20) || (length & 1))
        return ModelHeaderStatus::InvalidSettings;
    std::vector<std::uint8_t> parsed(length);
    if (!Read(read, context, parsed.data(), length))
        return ModelHeaderStatus::Truncated;
    output = std::move(parsed);
    return ModelHeaderStatus::Ok;
}
ModelHeaderStatus SkipPmxText(ReadModelBytes read, void* context) {
    std::uint32_t length = 0;
    if (!ReadU32(read, context, length))
        return ModelHeaderStatus::Truncated;
    if (length > (1u << 20) || (length & 1))
        return ModelHeaderStatus::InvalidSettings;
    std::uint8_t buffer[256];
    while (length) {
        const auto count = length > sizeof buffer ? sizeof buffer : length;
        if (!Read(read, context, buffer, count))
            return ModelHeaderStatus::Truncated;
        length -= static_cast<std::uint32_t>(count);
    }
    return ModelHeaderStatus::Ok;
}
} // namespace mikudancestudio::format
