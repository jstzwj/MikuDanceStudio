#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace mikudancestudio::format {
using ReadModelBytes = bool (*)(void*, void*, std::size_t);
enum class ModelHeaderStatus {
    Ok,
    Truncated,
    InvalidVersion,
    InvalidSettings,
    UnsupportedEncoding
};
enum class ModelKind {
    Pmd,
    Pmx,
    Unknown
};
ModelKind IdentifyModel(const std::array<char, 3>& signature);
struct PmdHeader {
    float version = 0;
    std::array<char, 20> name{};
    std::array<char, 256> comment{};
};
struct PmxHeader {
    float version = 0;
    std::uint8_t textEncoding = 0;
    std::uint8_t additionalUvCount = 0;
    std::uint8_t vertexIndexSize = 0;
    std::uint8_t textureIndexSize = 0;
    std::uint8_t materialIndexSize = 0;
    std::uint8_t boneIndexSize = 0;
    std::uint8_t morphIndexSize = 0;
    std::uint8_t rigidIndexSize = 0;
};
struct PmxTexts {
    std::array<std::vector<std::uint8_t>, 4> fields;
};
ModelHeaderStatus ReadPmdHeader(ReadModelBytes, void*, PmdHeader&);
ModelHeaderStatus ReadPmxHeader(ReadModelBytes, void*, PmxHeader&);
ModelHeaderStatus ReadPmxTexts(ReadModelBytes, void*, PmxTexts&);
ModelHeaderStatus ReadPmxText(ReadModelBytes, void*, std::vector<std::uint8_t>&);
ModelHeaderStatus SkipPmxText(ReadModelBytes, void*);
} // namespace mikudancestudio::format
