#include "mikudancestudio/vmd_format.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace mikudancestudio::format {
namespace {

constexpr std::uint32_t kMaxBoneKeys = 600000;
constexpr std::uint32_t kMaxMorphKeys = 20000;
constexpr std::uint32_t kMaxTrackKeys = 10000;
constexpr std::uint32_t kMaxIkKeys = 1000;
constexpr std::uint32_t kMaxIkEntries = 10000;

bool ReadBytes(std::FILE* stream, void* output, std::size_t count) {
    return std::fread(output, 1, count, stream) == count;
}

bool WriteBytes(std::FILE* stream, const void* input, std::size_t count) {
    return std::fwrite(input, 1, count, stream) == count;
}

bool ReadU32(std::FILE* stream, std::uint32_t& value) {
    std::uint8_t bytes[4];
    if (!ReadBytes(stream, bytes, sizeof bytes))
        return false;
    value = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
            (static_cast<std::uint32_t>(bytes[2]) << 16) |
            (static_cast<std::uint32_t>(bytes[3]) << 24);
    return true;
}

bool WriteU32(std::FILE* stream, std::uint32_t value) {
    const std::uint8_t bytes[4]{
        static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8),
        static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 24)};
    return WriteBytes(stream, bytes, sizeof bytes);
}

bool ReadF32(std::FILE* stream, float& value) {
    static_assert(sizeof(float) == 4, "VMD requires IEEE-754 binary32");
    std::uint32_t bits;
    if (!ReadU32(stream, bits))
        return false;
    std::memcpy(&value, &bits, sizeof bits);
    return true;
}

bool WriteF32(std::FILE* stream, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return WriteU32(stream, bits);
}

template <std::size_t N> bool ReadFloats(std::FILE* stream, float (&values)[N]) {
    for (float& value : values)
        if (!ReadF32(stream, value))
            return false;
    return true;
}

template <std::size_t N> bool WriteFloats(std::FILE* stream, const float (&values)[N]) {
    for (float value : values)
        if (!WriteF32(stream, value))
            return false;
    return true;
}

template <typename T>
bool ReadCount(std::FILE* stream, std::vector<T>& records, std::uint32_t limit) {
    std::uint32_t count;
    if (!ReadU32(stream, count) || count > limit)
        return false;
    records.resize(count);
    return true;
}

template <typename T>
bool WriteCount(std::FILE* stream, const std::vector<T>& records, std::uint32_t limit) {
    return records.size() <= limit && WriteU32(stream, static_cast<std::uint32_t>(records.size()));
}

// Some VMD2 files stop after light or shadow keys. A partial count is invalid.
bool ReadOptionalCount(std::FILE* stream, std::uint32_t& count, bool& present) {
    const int first = std::fgetc(stream);
    if (first == EOF) {
        present = false;
        return std::ferror(stream) == 0;
    }
    std::uint8_t rest[3];
    if (!ReadBytes(stream, rest, sizeof rest))
        return false;
    present = true;
    count = static_cast<std::uint32_t>(first) | (static_cast<std::uint32_t>(rest[0]) << 8) |
            (static_cast<std::uint32_t>(rest[1]) << 16) |
            (static_cast<std::uint32_t>(rest[2]) << 24);
    return true;
}

} // namespace

bool ReadVmd(std::FILE* stream, VmdDocument& document) {
    if (stream == nullptr)
        return false;
    VmdDocument parsed;
    char header[30];
    if (!ReadBytes(stream, header, sizeof header))
        return false;
    if (std::memcmp(header, "Vocaloid Motion Data 0002", 25) == 0)
        parsed.version = VmdVersion::V2;
    else if (std::memcmp(header, "Vocaloid Motion Data file", 25) == 0)
        parsed.version = VmdVersion::V1;
    else
        return false;
    const std::size_t nameLength = parsed.version == VmdVersion::V2 ? 20 : 10;
    if (!ReadBytes(stream, parsed.modelName.data(), nameLength) ||
        !ReadCount(stream, parsed.bones, kMaxBoneKeys))
        return false;
    for (VmdBoneKey& key : parsed.bones) {
        if (!ReadBytes(stream, key.name.data(), key.name.size()) || !ReadU32(stream, key.frame) ||
            !ReadFloats(stream, key.position) || !ReadFloats(stream, key.rotation) ||
            !ReadBytes(stream, key.interpolation.data(), key.interpolation.size()))
            return false;
    }
    if (!ReadCount(stream, parsed.morphs, kMaxMorphKeys))
        return false;
    for (VmdMorphKey& key : parsed.morphs) {
        if (!ReadBytes(stream, key.name.data(), key.name.size()) || !ReadU32(stream, key.frame) ||
            !ReadF32(stream, key.value))
            return false;
    }
    std::uint32_t count = 0;
    bool present = false;
    if (!ReadOptionalCount(stream, count, present))
        return false;
    if (!present) {
        parsed.hasCameraSection = false;
        document = std::move(parsed);
        return true;
    }
    if (count > kMaxTrackKeys)
        return false;
    parsed.cameras.resize(count);
    for (VmdCameraKey& key : parsed.cameras) {
        if (!ReadU32(stream, key.frame) || !ReadF32(stream, key.distance) ||
            !ReadFloats(stream, key.eye) || !ReadFloats(stream, key.target))
            return false;
        if (parsed.version == VmdVersion::V1) {
            std::uint8_t curve[4];
            if (!ReadBytes(stream, curve, sizeof curve))
                return false;
            for (auto& channel : key.interpolation)
                std::memcpy(channel, curve, sizeof curve);
        } else {
            if (!ReadBytes(stream, key.interpolation, sizeof key.interpolation) ||
                !ReadU32(stream, key.fov) || !ReadBytes(stream, &key.perspective, 1))
                return false;
        }
    }
    if (!ReadOptionalCount(stream, count, present))
        return false;
    if (!present) {
        document = std::move(parsed);
        return true;
    }
    if (count > kMaxTrackKeys)
        return false;
    parsed.lights.resize(count);
    for (VmdLightKey& key : parsed.lights) {
        if (!ReadU32(stream, key.frame) || !ReadFloats(stream, key.color) ||
            !ReadFloats(stream, key.direction))
            return false;
    }
    if (parsed.version == VmdVersion::V2) {
        if (!ReadOptionalCount(stream, count, present))
            return false;
        if (present) {
            if (count > kMaxTrackKeys)
                return false;
            parsed.shadows.resize(count);
            for (VmdShadowKey& key : parsed.shadows) {
                if (!ReadU32(stream, key.frame) || !ReadBytes(stream, &key.mode, 1) ||
                    !ReadF32(stream, key.distance))
                    return false;
            }
            if (!ReadOptionalCount(stream, count, present))
                return false;
            if (present) {
                if (count > kMaxIkKeys)
                    return false;
                parsed.ik.resize(count);
                for (VmdIkKey& key : parsed.ik) {
                    std::uint32_t entries;
                    if (!ReadU32(stream, key.frame) || !ReadBytes(stream, &key.visible, 1) ||
                        !ReadU32(stream, entries) || entries > kMaxIkEntries)
                        return false;
                    key.entries.resize(entries);
                    for (VmdIkEntry& entry : key.entries) {
                        if (!ReadBytes(stream, entry.name.data(), entry.name.size()) ||
                            !ReadBytes(stream, &entry.enabled, 1))
                            return false;
                    }
                }
            }
        }
    }
    document = std::move(parsed);
    return true;
}

bool WriteVmd(std::FILE* stream, const VmdDocument& document) {
    if (stream == nullptr)
        return false;
    char header[30]{};
    const char* text = document.version == VmdVersion::V2 ? "Vocaloid Motion Data 0002"
                                                          : "Vocaloid Motion Data file";
    std::memcpy(header, text, std::strlen(text));
    const std::size_t nameLength = document.version == VmdVersion::V2 ? 20 : 10;
    if (!WriteBytes(stream, header, sizeof header) ||
        !WriteBytes(stream, document.modelName.data(), nameLength) ||
        !WriteCount(stream, document.bones, kMaxBoneKeys))
        return false;
    for (const VmdBoneKey& key : document.bones) {
        if (!WriteBytes(stream, key.name.data(), key.name.size()) || !WriteU32(stream, key.frame) ||
            !WriteFloats(stream, key.position) || !WriteFloats(stream, key.rotation) ||
            !WriteBytes(stream, key.interpolation.data(), key.interpolation.size()))
            return false;
    }
    if (!WriteCount(stream, document.morphs, kMaxMorphKeys))
        return false;
    for (const VmdMorphKey& key : document.morphs) {
        if (!WriteBytes(stream, key.name.data(), key.name.size()) || !WriteU32(stream, key.frame) ||
            !WriteF32(stream, key.value))
            return false;
    }
    if (!WriteCount(stream, document.cameras, kMaxTrackKeys))
        return false;
    for (const VmdCameraKey& key : document.cameras) {
        if (!WriteU32(stream, key.frame) || !WriteF32(stream, key.distance) ||
            !WriteFloats(stream, key.eye) || !WriteFloats(stream, key.target))
            return false;
        if (document.version == VmdVersion::V1) {
            if (!WriteBytes(stream, key.interpolation[0], 4))
                return false;
        } else if (!WriteBytes(stream, key.interpolation, sizeof key.interpolation) ||
                   !WriteU32(stream, key.fov) || !WriteBytes(stream, &key.perspective, 1))
            return false;
    }
    if (!WriteCount(stream, document.lights, kMaxTrackKeys))
        return false;
    for (const VmdLightKey& key : document.lights) {
        if (!WriteU32(stream, key.frame) || !WriteFloats(stream, key.color) ||
            !WriteFloats(stream, key.direction))
            return false;
    }
    if (document.version == VmdVersion::V2) {
        if (!WriteCount(stream, document.shadows, kMaxTrackKeys))
            return false;
        for (const VmdShadowKey& key : document.shadows) {
            if (!WriteU32(stream, key.frame) || !WriteBytes(stream, &key.mode, 1) ||
                !WriteF32(stream, key.distance))
                return false;
        }
        if (!WriteCount(stream, document.ik, kMaxIkKeys))
            return false;
        for (const VmdIkKey& key : document.ik) {
            if (key.entries.size() > kMaxIkEntries || !WriteU32(stream, key.frame) ||
                !WriteBytes(stream, &key.visible, 1) ||
                !WriteU32(stream, static_cast<std::uint32_t>(key.entries.size())))
                return false;
            for (const VmdIkEntry& entry : key.entries) {
                if (!WriteBytes(stream, entry.name.data(), entry.name.size()) ||
                    !WriteBytes(stream, &entry.enabled, 1))
                    return false;
            }
        }
    }
    return std::ferror(stream) == 0;
}

} // namespace mikudancestudio::format
