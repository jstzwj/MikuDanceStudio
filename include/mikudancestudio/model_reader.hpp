#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "mikudancestudio/model_geometry.hpp"
#include "mikudancestudio/model_sections.hpp"
namespace mikudancestudio::format {
struct PmdMorphDocument {
    PmdMorphHeaderData header;
    std::vector<PmdMorphOffsetData> offsets;
};
struct PmdBoneFrameDocument {
    std::uint16_t bone = 0;
    std::uint8_t group = 0;
};
struct PmdDocument {
    PmdHeader header;
    std::vector<PmdVertexData> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<MaterialData> materials;
    std::vector<BoneData> bones;
    std::vector<PmdIkData> ikChains;
    std::vector<PmdMorphDocument> morphs;
    std::vector<std::uint16_t> facialTargets;
    std::vector<std::array<char, 50>> groupNames;
    std::vector<PmdBoneFrameDocument> boneFrames;
    PmdEnglishPresence englishPresence;
    PmdEnglishModelData englishModel;
    std::vector<std::array<char, 20>> englishBoneNames;
    std::vector<std::array<char, 20>> englishMorphNames;
    std::vector<std::array<char, 50>> englishGroupNames;
    std::array<std::array<char, 100>, 10> toonNames{};
    std::vector<RigidData> rigids;
    std::vector<JointData> joints;
    bool hasPhysicsSection = false;
};
using PmxText = std::vector<std::uint8_t>;
struct PmxMaterialDocument {
    std::array<PmxText, 2> names;
    MaterialData fields;
};
struct PmxBoneDocument {
    std::array<PmxText, 2> names;
    BoneData fields;
};
struct PmxMorphDocument {
    std::array<PmxText, 2> names;
    PmxMorphHeaderData header;
    std::vector<PmxMorphOffsetData> offsets;
};
struct PmxDisplayDocument {
    std::array<PmxText, 2> names;
    std::uint8_t special = 0;
    std::vector<PmxDisplayEntryData> entries;
};
struct PmxRigidDocument {
    std::array<PmxText, 2> names;
    RigidData fields;
};
struct PmxJointDocument {
    std::array<PmxText, 2> names;
    JointData fields;
};
struct PmxDocument {
    PmxHeader header;
    PmxTexts texts;
    std::vector<PmxVertexData> vertices;
    std::vector<std::int32_t> indices;
    std::vector<PmxText> textures;
    std::vector<PmxMaterialDocument> materials;
    std::vector<PmxBoneDocument> bones;
    std::vector<PmxMorphDocument> morphs;
    std::vector<PmxDisplayDocument> frames;
    std::vector<PmxRigidDocument> rigids;
    std::vector<PmxJointDocument> joints;
};
struct ModelDocument {
    ModelKind kind = ModelKind::Unknown;
    PmdDocument pmd;
    PmxDocument pmx;
};
bool ReadModelDocument(ReadModelBytesResult read, void* context, ModelDocument& document);
}
