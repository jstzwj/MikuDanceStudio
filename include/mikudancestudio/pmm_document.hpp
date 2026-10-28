#pragma once
#include "mikudancestudio/pmm_format.hpp"
namespace mikudancestudio::format {
using ReadPmmBytesResult = int (*)(void*, void*, std::size_t);
using WritePmmBytes = bool (*)(void*, const void*, std::size_t);
struct PmmDocument {
    PmmHeader header;
    PmmSceneSettings settings;
    std::uint8_t selectedModelSlot = 0;
    std::uint8_t modelCount = 0;
    std::vector<std::array<char, 20>> v1ModelNames;
    std::vector<std::uint8_t> prefixBytes;
    std::vector<std::uint8_t> remainingBytes;
};
bool ReadPmmDocument(ReadPmmBytesResult read, void* context, PmmDocument& document);
bool WritePmmDocument(WritePmmBytes write, void* context, const PmmDocument& document);
}
