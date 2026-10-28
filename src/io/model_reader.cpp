#include "mikudancestudio/model_reader.hpp"
#include <cstring>
#include <utility>
namespace mikudancestudio::format {
namespace {
constexpr std::size_t kMaxFileBytes = 512u << 20;
constexpr std::uint32_t kMaxVertices = 1'000'000;
constexpr std::uint32_t kMaxIndices = 3'000'000;
constexpr std::uint32_t kMaxRecords = 100'000;
constexpr std::uint32_t kMaxOffsets = 1'000'000;
struct Cursor {
    ReadModelBytesResult source;
    void* context;
    std::size_t consumed = 0;
    static bool bytes(void* opaque, void* destination, std::size_t length) {
        auto& cursor = *static_cast<Cursor*>(opaque);
        if (length > kMaxFileBytes - cursor.consumed)
            return false;
        if (length == 0)
            return true;
        const int result = cursor.source(cursor.context, destination, length);
        if (result > 0)
            cursor.consumed += static_cast<std::size_t>(result);
        return result == static_cast<int>(length);
    }
    static int result(void* opaque, void* destination, std::size_t length) {
        auto& cursor = *static_cast<Cursor*>(opaque);
        if (length > kMaxFileBytes - cursor.consumed)
            return -2;
        const int received = cursor.source(cursor.context, destination, length);
        if (received > 0)
            cursor.consumed += static_cast<std::size_t>(received);
        return received;
    }
};
bool Count(Cursor& cursor, std::uint8_t width, std::uint32_t limit, std::uint32_t& count) {
    return ReadModelSectionCount(Cursor::bytes, &cursor, width, count) && count <= limit;
}
bool Names(Cursor& cursor, std::array<PmxText, 2>& names) {
    return ReadPmxText(Cursor::bytes, &cursor, names[0]) == ModelHeaderStatus::Ok &&
           ReadPmxText(Cursor::bytes, &cursor, names[1]) == ModelHeaderStatus::Ok;
}
bool ReadPmd(Cursor& cursor, PmdDocument& model) {
    if (ReadPmdHeader(Cursor::bytes, &cursor, model.header) != ModelHeaderStatus::Ok)
        return false;
    std::uint32_t count = 0;
    if (!Count(cursor, 4, kMaxVertices, count))
        return false;
    model.vertices.resize(count);
    for (auto& item : model.vertices)
        if (!ReadPmdVertex(Cursor::bytes, &cursor, item))
            return false;
    if (!Count(cursor, 4, kMaxIndices, count))
        return false;
    model.indices.resize(count);
    for (auto& item : model.indices)
        if (!ReadPmdIndex(Cursor::bytes, &cursor, item))
            return false;
    if (!Count(cursor, 4, kMaxRecords, count))
        return false;
    model.materials.resize(count);
    for (auto& item : model.materials)
        if (!ReadPmdMaterial(Cursor::bytes, &cursor, item))
            return false;
    if (!Count(cursor, 2, kMaxRecords, count))
        return false;
    model.bones.resize(count);
    for (auto& item : model.bones)
        if (!ReadPmdBone(Cursor::bytes, &cursor, item))
            return false;
    if (!Count(cursor, 2, kMaxRecords, count))
        return false;
    model.ikChains.resize(count);
    for (auto& item : model.ikChains)
        if (!ReadPmdIk(Cursor::bytes, &cursor, item))
            return false;
    if (!Count(cursor, 2, kMaxRecords, count))
        return false;
    model.morphs.resize(count);
    std::uint32_t totalOffsets = 0;
    for (auto& morph : model.morphs) {
        if (!ReadPmdMorphHeader(Cursor::bytes, &cursor, morph.header) ||
            static_cast<std::uint32_t>(morph.header.offsetCount) > kMaxOffsets - totalOffsets)
            return false;
        totalOffsets += static_cast<std::uint32_t>(morph.header.offsetCount);
        morph.offsets.resize(morph.header.offsetCount);
        for (auto& item : morph.offsets)
            if (!ReadPmdMorphOffset(Cursor::bytes, &cursor, item))
                return false;
    }
    if (!Count(cursor, 1, 255, count))
        return false;
    model.facialTargets.resize(count);
    for (auto& target : model.facialTargets)
        if (!ReadPmdDisplayTarget(Cursor::bytes, &cursor, target))
            return false;
    if (!Count(cursor, 1, 255, count))
        return false;
    model.groupNames.resize(count);
    for (auto& name : model.groupNames) {
        char raw[50];
        if (!ReadPmdGroupName(Cursor::bytes, &cursor, raw))
            return false;
        std::memcpy(name.data(), raw, sizeof raw);
    }
    if (!Count(cursor, 4, kMaxRecords, count))
        return false;
    model.boneFrames.resize(count);
    for (auto& frame : model.boneFrames)
        if (!ReadPmdBoneFrame(Cursor::bytes, &cursor, frame.bone, frame.group))
            return false;
    if (!ReadPmdEnglishPresence(Cursor::result, &cursor, model.englishPresence))
        return false;
    if (model.englishPresence.hasEnglish) {
        if (!ReadPmdEnglishModel(Cursor::bytes, &cursor, model.englishModel))
            return false;
        model.englishBoneNames.resize(model.bones.size());
        for (auto& name : model.englishBoneNames) {
            char raw[20];
            if (!ReadPmdEnglishName(Cursor::bytes, &cursor, raw))
                return false;
            std::memcpy(name.data(), raw, sizeof raw);
        }
        model.englishMorphNames.resize(model.morphs.empty() ? 0 : model.morphs.size() - 1);
        for (auto& name : model.englishMorphNames) {
            char raw[20];
            if (!ReadPmdEnglishName(Cursor::bytes, &cursor, raw))
                return false;
            std::memcpy(name.data(), raw, sizeof raw);
        }
        model.englishGroupNames.resize(model.groupNames.size());
        for (auto& name : model.englishGroupNames) {
            char raw[50];
            if (!ReadPmdEnglishGroupName(Cursor::bytes, &cursor, raw))
                return false;
            std::memcpy(name.data(), raw, sizeof raw);
        }
    }
    if (model.englishPresence.hasToonFields)
        for (auto& name : model.toonNames) {
            char raw[100];
            if (!ReadPmdToonName(Cursor::bytes, &cursor, raw))
                return false;
            std::memcpy(name.data(), raw, sizeof raw);
        }
    auto status = ReadPmdOptionalCount(Cursor::result, &cursor, count);
    if (status == PmdOptionalRead::Truncated)
        return false;
    model.hasPhysicsSection = status != PmdOptionalRead::ReadError;
    if (!model.hasPhysicsSection)
        return true;
    if (count > kMaxRecords)
        return false;
    model.rigids.resize(count);
    for (auto& rigid : model.rigids)
        if (!ReadPmdRigid(Cursor::bytes, &cursor, rigid))
            return false;
    count = 0;
    status = ReadPmdOptionalCount(Cursor::result, &cursor, count);
    if (status == PmdOptionalRead::Truncated || count > kMaxRecords)
        return false;
    model.joints.resize(count);
    for (auto& joint : model.joints)
        if (!ReadPmdJoint(Cursor::bytes, &cursor, joint))
            return false;
    return true;
}
bool ReadPmx(Cursor& cursor, PmxDocument& model) {
    if (ReadPmxHeader(Cursor::bytes, &cursor, model.header) != ModelHeaderStatus::Ok)
        return false;
    if (model.header.additionalUvCount > 4)
        return false;
    const auto validIndex = [](std::uint8_t width) {
        return width == 1 || width == 2 || width == 4;
    };
    if (!validIndex(model.header.vertexIndexSize) || !validIndex(model.header.textureIndexSize) ||
        !validIndex(model.header.materialIndexSize) || !validIndex(model.header.boneIndexSize) ||
        !validIndex(model.header.morphIndexSize) || !validIndex(model.header.rigidIndexSize))
        return false;
    if (ReadPmxTexts(Cursor::bytes, &cursor, model.texts) != ModelHeaderStatus::Ok)
        return false;
    std::uint32_t count = 0;
    if (!Count(cursor, 4, kMaxVertices, count))
        return false;
    model.vertices.resize(count);
    for (auto& vertex : model.vertices)
        if (!ReadPmxVertex(Cursor::bytes, &cursor, model.header.additionalUvCount,
                           model.header.boneIndexSize, vertex))
            return false;
    if (!Count(cursor, 4, kMaxIndices, count))
        return false;
    model.indices.resize(count);
    for (auto& index : model.indices)
        if (!ReadPmxVertexIndex(Cursor::bytes, &cursor, model.header.vertexIndexSize, index))
            return false;
    if (!Count(cursor, 4, kMaxRecords, count))
        return false;
    model.textures.resize(count);
    for (auto& text : model.textures)
        if (ReadPmxText(Cursor::bytes, &cursor, text) != ModelHeaderStatus::Ok)
            return false;
    if (!Count(cursor, 4, kMaxRecords, count))
        return false;
    model.materials.resize(count);
    for (auto& material : model.materials)
        if (!Names(cursor, material.names) ||
            !ReadPmxMaterial(Cursor::bytes, &cursor, model.header.textureIndexSize,
                             material.fields))
            return false;
    if (!Count(cursor, 4, kMaxRecords, count))
        return false;
    model.bones.resize(count);
    for (auto& bone : model.bones)
        if (!Names(cursor, bone.names) ||
            !ReadPmxBone(Cursor::bytes, &cursor, model.header.boneIndexSize, bone.fields))
            return false;
    if (!Count(cursor, 4, kMaxRecords, count))
        return false;
    model.morphs.resize(count);
    const PmxMorphIndexSizes sizes{model.header.vertexIndexSize, model.header.boneIndexSize,
                                   model.header.morphIndexSize, model.header.materialIndexSize};
    std::uint32_t totalOffsets = 0;
    for (auto& morph : model.morphs) {
        if (!Names(cursor, morph.names) ||
            !ReadPmxMorphHeader(Cursor::bytes, &cursor, morph.header) ||
            static_cast<std::uint32_t>(morph.header.offsetCount) > kMaxOffsets - totalOffsets)
            return false;
        totalOffsets += static_cast<std::uint32_t>(morph.header.offsetCount);
        if (morph.header.type > 8 && morph.header.offsetCount != 0)
            return false;
        morph.offsets.resize(morph.header.offsetCount);
        for (auto& offset : morph.offsets)
            if (!ReadPmxMorphOffset(Cursor::bytes, &cursor, morph.header.type, sizes, offset))
                return false;
    }
    if (!Count(cursor, 4, kMaxRecords, count))
        return false;
    model.frames.resize(count);
    std::uint32_t totalEntries = 0;
    for (auto& frame : model.frames) {
        std::int32_t entryCount = 0;
        if (!Names(cursor, frame.names) ||
            !ReadPmxDisplayHeader(Cursor::bytes, &cursor, frame.special, entryCount) ||
            static_cast<std::uint32_t>(entryCount) > kMaxOffsets - totalEntries)
            return false;
        totalEntries += static_cast<std::uint32_t>(entryCount);
        frame.entries.resize(entryCount);
        for (auto& entry : frame.entries)
            if (!ReadPmxDisplayEntry(Cursor::bytes, &cursor, model.header.boneIndexSize,
                                     model.header.morphIndexSize, entry))
                return false;
    }
    if (!Count(cursor, 4, kMaxRecords, count))
        return false;
    model.rigids.resize(count);
    for (auto& rigid : model.rigids)
        if (!Names(cursor, rigid.names) ||
            !ReadPmxRigid(Cursor::bytes, &cursor, model.header.boneIndexSize, rigid.fields))
            return false;
    if (model.rigids.empty())
        return true;
    if (!Count(cursor, 4, kMaxRecords, count))
        return false;
    model.joints.resize(count);
    for (auto& joint : model.joints)
        if (!Names(cursor, joint.names) ||
            !ReadPmxJoint(Cursor::bytes, &cursor, model.header.rigidIndexSize, joint.fields))
            return false;
    return true;
}
}
bool ReadModelDocument(ReadModelBytesResult read, void* context, ModelDocument& document) {
    if (!read)
        return false;
    Cursor cursor{read, context};
    std::array<char, 3> signature{};
    if (!Cursor::bytes(&cursor, signature.data(), signature.size()))
        return false;
    ModelDocument parsed;
    parsed.kind = IdentifyModel(signature);
    if (parsed.kind == ModelKind::Unknown)
        return false;
    const bool valid =
        parsed.kind == ModelKind::Pmd ? ReadPmd(cursor, parsed.pmd) : ReadPmx(cursor, parsed.pmx);
    if (!valid)
        return false;
    document = std::move(parsed);
    return true;
}
}
