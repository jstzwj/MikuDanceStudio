#include "mikudancestudio/model_sections.hpp"
#include <cstring>
#include <type_traits>
#include <utility>
namespace mikudancestudio::format {
namespace {
struct Reader {
    ReadModelBytes read;
    void* context;
    bool bytes(void* destination, std::size_t size) const {
        return read && read(context, destination, size);
    }
    template <class T> bool value(T& result) const {
        static_assert(std::is_integral_v<T> || std::is_same_v<T, float>);
        std::uint8_t data[sizeof(T)]{};
        if (!bytes(data, sizeof data))
            return false;
        std::uint32_t bits = 0;
        for (std::size_t index = 0; index < sizeof data; ++index)
            bits |= std::uint32_t(data[index]) << (index * 8);
        if constexpr (std::is_same_v<T, float>) {
            std::memcpy(&result, &bits, sizeof result);
        } else if constexpr (std::is_signed_v<T>) {
            const std::uint32_t sign = 1u << (sizeof(T) * 8 - 1);
            const std::int64_t signedValue =
                (bits & sign) ? std::int64_t(bits) - (std::int64_t(1) << (sizeof(T) * 8)) : bits;
            result = static_cast<T>(signedValue);
        } else {
            result = static_cast<T>(bits);
        }
        return true;
    }
    template <class T, std::size_t N> bool array(T (&values)[N]) const {
        for (auto& item : values)
            if (!value(item))
                return false;
        return true;
    }
    bool floats(float* values, std::size_t count) const {
        for (std::size_t index = 0; index < count; ++index)
            if (!value(values[index]))
                return false;
        return true;
    }
    bool index(std::uint8_t size, std::int32_t& result) const {
        if (size == 1) {
            std::int8_t value = 0;
            if (!this->value(value))
                return false;
            result = value;
        } else if (size == 2) {
            std::int16_t value = 0;
            if (!this->value(value))
                return false;
            result = value;
        } else if (size == 4)
            return value(result);
        else
            return false;
        return true;
    }
    bool vertexIndex(std::uint8_t size, std::int32_t& result) const {
        if (size == 1) {
            std::uint8_t value = 0;
            if (!this->value(value))
                return false;
            result = value;
        } else if (size == 2) {
            std::uint16_t value = 0;
            if (!this->value(value))
                return false;
            result = value;
        } else if (size == 4)
            return value(result);
        else
            return false;
        return true;
    }
};
}
bool ReadPmdMaterial(ReadModelBytes read, void* context, MaterialData& output) {
    Reader r{read, context};
    MaterialData data;
    std::uint8_t sphereFlag = 0;
    if (!r.array(data.diffuse) || !r.value(data.specularPower) || !r.array(data.specular) ||
        !r.array(data.ambient) || !r.value(data.toonReference) || !r.value(sphereFlag) ||
        !r.value(data.faceVertexCount) || !r.bytes(data.pmdTexture, 20))
        return false;
    data.flags = sphereFlag;
    output = data;
    return true;
}
bool ReadPmxMaterial(ReadModelBytes read, void* context, std::uint8_t indexSize,
                     MaterialData& output) {
    Reader r{read, context};
    MaterialData data;
    if (!r.array(data.diffuse) || !r.array(data.specular) || !r.value(data.specularPower) ||
        !r.array(data.ambient) || !r.value(data.flags) || !r.array(data.edgeColor) ||
        !r.value(data.edgeSize) || !r.index(indexSize, data.textureIndex) ||
        !r.index(indexSize, data.sphereIndex) || !r.value(data.sphereMode) ||
        !r.value(data.sharedToon))
        return false;
    if (data.sharedToon) {
        if (!r.value(data.toonReference))
            return false;
    } else if (!r.index(indexSize, data.toonIndex))
        return false;
    if (ReadPmxText(read, context, data.memo) != ModelHeaderStatus::Ok ||
        !r.value(data.faceVertexCount))
        return false;
    data.toonReference = data.sharedToon ? data.toonReference : 0xFE;
    output = data;
    return true;
}
bool ReadPmdBone(ReadModelBytes read, void* context, BoneData& output) {
    Reader r{read, context};
    BoneData data;
    std::int16_t parent = 0, tail = 0, tailIndex = 0;
    if (!r.bytes(data.pmdName, 20) || !r.value(parent) || !r.value(tail) || !r.value(data.type) ||
        !r.value(tailIndex) || !r.array(data.position))
        return false;
    data.parent = parent;
    data.tailBone = tail;
    data.tailIdx = tailIndex;
    output = std::move(data);
    return true;
}
bool ReadPmdIk(ReadModelBytes read, void* context, PmdIkData& output) {
    Reader r{read, context};
    PmdIkData data;
    std::uint8_t count = 0;
    if (!r.value(data.boneIndex) || !r.value(data.targetBone) || !r.value(count) ||
        !r.value(data.iterations) || !r.value(data.maxAngle))
        return false;
    data.links.resize(count);
    for (auto& link : data.links)
        if (!r.value(link))
            return false;
    output = std::move(data);
    return true;
}
bool ReadPmxBone(ReadModelBytes read, void* context, std::uint8_t indexSize, BoneData& output) {
    Reader r{read, context};
    BoneData data;
    if (!r.array(data.position) || !r.index(indexSize, data.parent) || !r.value(data.layer) ||
        !r.value(data.flags))
        return false;
    if (data.flags & 0x0001) {
        if (!r.index(indexSize, data.tailBone))
            return false;
    } else if (!r.array(data.tailOffset))
        return false;
    if (data.flags & 0x0300)
        if (!r.index(indexSize, data.tailIdx) || !r.value(data.inheritRatio))
            return false;
    if (data.flags & 0x0400)
        if (!r.array(data.axis))
            return false;
    if (data.flags & 0x0800)
        if (!r.array(data.localAxes))
            return false;
    if (data.flags & 0x2000)
        if (!r.value(data.extParent))
            return false;
    if (data.flags & 0x0020) {
        std::int32_t count = 0;
        if (!r.index(indexSize, data.ikTarget) || !r.value(data.ikLoop) || !r.value(data.ikAngle) ||
            !r.value(count) || count < 0 || count > 65535)
            return false;
        data.ikLinks.resize(count);
        for (auto& link : data.ikLinks) {
            if (!r.index(indexSize, link.boneIndex) || !r.value(link.hasLimits))
                return false;
            if (link.hasLimits == 1 && (!r.array(link.minimum) || !r.array(link.maximum)))
                return false;
        }
    }
    output = std::move(data);
    return true;
}
bool ReadPmdRigid(ReadModelBytes read, void* context, RigidData& output) {
    Reader r{read, context};
    RigidData data;
    std::int16_t boneIndex = -1;
    if (!r.bytes(data.pmdName, 20) || !r.value(boneIndex) || !r.value(data.group) ||
        !r.value(data.noCollapse) || !r.value(data.shape) || !r.array(data.size) ||
        !r.array(data.position) || !r.array(data.rotation) || !r.value(data.mass) ||
        !r.value(data.linearDamping) || !r.value(data.angularDamping) ||
        !r.value(data.restitution) || !r.value(data.friction) || !r.value(data.mode))
        return false;
    data.boneIndex = boneIndex;
    output = data;
    return true;
}
bool ReadPmxRigid(ReadModelBytes read, void* context, std::uint8_t boneIndexSize,
                  RigidData& output) {
    Reader r{read, context};
    RigidData data;
    if (!r.index(boneIndexSize, data.boneIndex) || !r.value(data.group) ||
        !r.value(data.noCollapse) || !r.value(data.shape) || !r.array(data.size) ||
        !r.array(data.position) || !r.array(data.rotation) || !r.value(data.mass) ||
        !r.value(data.linearDamping) || !r.value(data.angularDamping) ||
        !r.value(data.restitution) || !r.value(data.friction) || !r.value(data.mode))
        return false;
    output = data;
    return true;
}
bool ReadPmdJoint(ReadModelBytes read, void* context, JointData& output) {
    Reader r{read, context};
    JointData data;
    if (!r.bytes(data.pmdName, 20) || !r.value(data.rigidA) || !r.value(data.rigidB) ||
        !r.array(data.position) || !r.array(data.rotation) || !r.floats(data.limits + 3, 3) ||
        !r.floats(data.limits, 3) || !r.floats(data.limits + 9, 3) ||
        !r.floats(data.limits + 6, 3) || !r.array(data.springs))
        return false;
    output = data;
    return true;
}
bool ReadPmxJoint(ReadModelBytes read, void* context, std::uint8_t indexSize, JointData& output) {
    Reader r{read, context};
    JointData data;
    std::uint8_t type = 0;
    if (!r.value(type) || !r.index(indexSize, data.rigidA) || !r.index(indexSize, data.rigidB) ||
        !r.array(data.position) || !r.array(data.rotation) || !r.floats(data.limits + 3, 3) ||
        !r.floats(data.limits, 3) || !r.floats(data.limits + 9, 3) ||
        !r.floats(data.limits + 6, 3) || !r.array(data.springs))
        return false;
    output = data;
    return true;
}
bool ReadPmxMorphHeader(ReadModelBytes read, void* context, PmxMorphHeaderData& output) {
    Reader r{read, context};
    PmxMorphHeaderData data;
    if (!r.value(data.panel) || !r.value(data.type) || !r.value(data.offsetCount) ||
        data.offsetCount < 0)
        return false;
    output = data;
    return true;
}
bool ReadPmxMorphOffset(ReadModelBytes read, void* context, std::uint8_t type,
                        PmxMorphIndexSizes sizes, PmxMorphOffsetData& output) {
    Reader r{read, context};
    PmxMorphOffsetData data;
    if (type > 8)
        return false;
    const bool vertex = type == 1 || (type >= 3 && type <= 7);
    const auto indexSize = type == 0   ? sizes.morph
                           : vertex    ? sizes.vertex
                           : type == 2 ? sizes.bone
                                       : sizes.material;
    if (vertex ? !r.vertexIndex(indexSize, data.index) : !r.index(indexSize, data.index))
        return false;
    if (type == 8 && !r.value(data.operation))
        return false;
    const std::size_t count = type == 0 ? 1 : type == 1 ? 3 : type == 2 ? 7 : type == 8 ? 28 : 4;
    if (!r.floats(data.values, count))
        return false;
    output = data;
    return true;
}
bool ReadPmxDisplayHeader(ReadModelBytes read, void* context, std::uint8_t& special,
                          std::int32_t& count) {
    Reader r{read, context};
    std::uint8_t parsedSpecial = 0;
    std::int32_t parsedCount = 0;
    if (!r.value(parsedSpecial) || !r.value(parsedCount) || parsedCount < 0)
        return false;
    special = parsedSpecial;
    count = parsedCount;
    return true;
}
bool ReadPmxDisplayEntry(ReadModelBytes read, void* context, std::uint8_t boneIndexSize,
                         std::uint8_t morphIndexSize, PmxDisplayEntryData& output) {
    Reader r{read, context};
    PmxDisplayEntryData data;
    if (!r.value(data.type) || !r.index(data.type ? morphIndexSize : boneIndexSize, data.index))
        return false;
    output = data;
    return true;
}
bool ReadPmdMorphHeader(ReadModelBytes read, void* context, PmdMorphHeaderData& output) {
    Reader r{read, context};
    PmdMorphHeaderData data;
    if (!r.bytes(data.name, 20) || !r.value(data.offsetCount) || !r.value(data.panel) ||
        data.offsetCount < 0)
        return false;
    output = data;
    return true;
}
bool ReadPmdMorphOffset(ReadModelBytes read, void* context, PmdMorphOffsetData& output) {
    Reader r{read, context};
    PmdMorphOffsetData data;
    if (!r.value(data.vertexIndex) || !r.array(data.offset))
        return false;
    output = data;
    return true;
}
bool ReadPmdDisplayTarget(ReadModelBytes read, void* context, std::uint16_t& output) {
    return Reader{read, context}.value(output);
}
bool ReadPmdGroupName(ReadModelBytes read, void* context, char (&output)[50]) {
    char data[50]{};
    if (!Reader{read, context}.bytes(data, sizeof data))
        return false;
    std::memcpy(output, data, sizeof data);
    return true;
}
bool ReadPmdBoneFrame(ReadModelBytes read, void* context, std::uint16_t& bone,
                      std::uint8_t& group) {
    Reader r{read, context};
    std::uint16_t parsedBone = 0;
    std::uint8_t parsedGroup = 0;
    if (!r.value(parsedBone) || !r.value(parsedGroup))
        return false;
    bone = parsedBone;
    group = parsedGroup;
    return true;
}
bool ReadModelSectionCount(ReadModelBytes read, void* context, std::uint8_t width,
                           std::uint32_t& count) {
    Reader reader{read, context};
    if (width == 1) {
        std::uint8_t value = 0;
        if (!reader.value(value))
            return false;
        count = value;
    } else if (width == 2) {
        std::uint16_t value = 0;
        if (!reader.value(value))
            return false;
        count = value;
    } else if (width == 4) {
        std::uint32_t value = 0;
        if (!reader.value(value))
            return false;
        count = value;
    } else
        return false;
    return true;
}
PmdOptionalRead ReadPmdOptionalCount(ReadModelBytesResult read, void* context,
                                     std::uint32_t& count) {
    if (!read)
        return PmdOptionalRead::ReadError;
    std::uint8_t bytes[4]{};
    const int result = read(context, bytes, sizeof bytes);
    if (result == -1)
        return PmdOptionalRead::ReadError;
    if (result == 0) {
        count = 0;
        return PmdOptionalRead::EndOfFile;
    }
    if (result != 4)
        return PmdOptionalRead::Truncated;
    count = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
            (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
    return PmdOptionalRead::Value;
}
bool ReadPmdEnglishPresence(ReadModelBytesResult read, void* context, PmdEnglishPresence& output) {
    if (!read)
        return false;
    std::uint8_t flag = 0;
    const int result = read(context, &flag, 1);
    if (result != -1 && result != 0 && result != 1)
        return false;
    output.hasEnglish = result == 1 && flag != 0;
    output.hasToonFields = result != 0;
    return true;
}
bool ReadPmdEnglishModel(ReadModelBytes read, void* context, PmdEnglishModelData& output) {
    Reader reader{read, context};
    PmdEnglishModelData data;
    if (!reader.bytes(data.name, sizeof data.name) ||
        !reader.bytes(data.comment, sizeof data.comment))
        return false;
    output = data;
    return true;
}
bool ReadPmdEnglishName(ReadModelBytes read, void* context, char (&output)[20]) {
    char data[20]{};
    if (!Reader{read, context}.bytes(data, sizeof data))
        return false;
    std::memcpy(output, data, sizeof data);
    return true;
}
bool ReadPmdEnglishGroupName(ReadModelBytes read, void* context, char (&output)[50]) {
    return ReadPmdGroupName(read, context, output);
}
bool ReadPmdToonName(ReadModelBytes read, void* context, char (&output)[100]) {
    char data[100]{};
    if (!Reader{read, context}.bytes(data, sizeof data))
        return false;
    std::memcpy(output, data, sizeof data);
    return true;
}
} // namespace mikudancestudio::format
