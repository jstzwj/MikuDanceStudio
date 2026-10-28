#pragma once
#include <cstdint>
#include <vector>
#include "mikudancestudio/model_format_header.hpp"
namespace mikudancestudio::format {
struct MaterialData {
    float diffuse[4]{}, specular[3]{}, ambient[3]{};
    float specularPower = 0, edgeColor[4]{}, edgeSize = 0;
    std::uint8_t flags = 0, sphereMode = 0, toonReference = 0, sharedToon = 0;
    std::int32_t faceVertexCount = 0, textureIndex = -1, sphereIndex = -1, toonIndex = -1;
    char pmdTexture[21]{};
    std::vector<std::uint8_t> memo;
};
struct IkLinkData {
    std::int32_t boneIndex = -1;
    std::uint8_t hasLimits = 0;
    float minimum[3]{}, maximum[3]{};
};
struct BoneData {
    char pmdName[20]{};
    float position[3]{}, tailOffset[3]{}, axis[3]{}, localAxes[6]{};
    std::int32_t parent = -1, tailBone = -1, tailIdx = -1;
    std::int32_t layer = 0, extParent = 0, ikTarget = -1, ikLoop = 0;
    float inheritRatio = 0, ikAngle = 0;
    std::uint16_t flags = 0;
    std::uint8_t type = 0;
    std::vector<IkLinkData> ikLinks;
};
struct PmdIkData {
    std::uint16_t boneIndex = 0, targetBone = 0, iterations = 0;
    float maxAngle = 0;
    std::vector<std::uint16_t> links;
};
struct RigidData {
    char pmdName[20]{};
    std::int32_t boneIndex = -1;
    std::uint8_t group = 0, shape = 0, mode = 0;
    std::uint16_t noCollapse = 0;
    float size[3]{}, position[3]{}, rotation[3]{};
    float mass = 0, linearDamping = 0, angularDamping = 0;
    float restitution = 0, friction = 0;
};
struct JointData {
    char pmdName[20]{};
    std::int32_t rigidA = -1, rigidB = -1;
    float position[3]{}, rotation[3]{}, limits[12]{}, springs[6]{};
};
struct PmxMorphHeaderData {
    std::uint8_t panel = 0, type = 0;
    std::int32_t offsetCount = 0;
};
struct PmxMorphIndexSizes {
    std::uint8_t vertex = 0, bone = 0, morph = 0, material = 0;
};
struct PmxMorphOffsetData {
    std::int32_t index = -1;
    std::uint8_t operation = 0;
    float values[28]{};
};
struct PmxDisplayEntryData {
    std::uint8_t type = 0;
    std::int32_t index = -1;
};
struct PmdMorphHeaderData {
    char name[20]{};
    std::int32_t offsetCount = 0;
    std::uint8_t panel = 0;
};
struct PmdMorphOffsetData {
    std::uint32_t vertexIndex = 0;
    float offset[3]{};
};
struct PmdEnglishModelData {
    char name[20]{};
    char comment[256]{};
};
using ReadModelBytesResult = int (*)(void*, void*, std::size_t);
enum class PmdOptionalRead {
    Value,
    EndOfFile,
    ReadError,
    Truncated
};
struct PmdEnglishPresence {
    bool hasEnglish = false;
    bool hasToonFields = false;
};
PmdOptionalRead ReadPmdOptionalCount(ReadModelBytesResult, void*, std::uint32_t& count);
bool ReadPmdEnglishPresence(ReadModelBytesResult, void*, PmdEnglishPresence&);
bool ReadModelSectionCount(ReadModelBytes, void*, std::uint8_t width, std::uint32_t& count);
bool ReadPmdEnglishModel(ReadModelBytes, void*, PmdEnglishModelData&);
bool ReadPmdEnglishName(ReadModelBytes, void*, char (&name)[20]);
bool ReadPmdEnglishGroupName(ReadModelBytes, void*, char (&name)[50]);
bool ReadPmdToonName(ReadModelBytes, void*, char (&name)[100]);
bool ReadPmdMorphHeader(ReadModelBytes, void*, PmdMorphHeaderData&);
bool ReadPmdMorphOffset(ReadModelBytes, void*, PmdMorphOffsetData&);
bool ReadPmdDisplayTarget(ReadModelBytes, void*, std::uint16_t&);
bool ReadPmdGroupName(ReadModelBytes, void*, char (&name)[50]);
bool ReadPmdBoneFrame(ReadModelBytes, void*, std::uint16_t& bone, std::uint8_t& group);
bool ReadPmxMorphHeader(ReadModelBytes, void*, PmxMorphHeaderData&);
bool ReadPmxMorphOffset(ReadModelBytes, void*, std::uint8_t type, PmxMorphIndexSizes,
                        PmxMorphOffsetData&);
bool ReadPmxDisplayHeader(ReadModelBytes, void*, std::uint8_t& special, std::int32_t& count);
bool ReadPmxDisplayEntry(ReadModelBytes, void*, std::uint8_t boneIndexSize,
                         std::uint8_t morphIndexSize, PmxDisplayEntryData&);
bool ReadPmdMaterial(ReadModelBytes, void*, MaterialData&);
bool ReadPmxMaterial(ReadModelBytes, void*, std::uint8_t textureIndexSize, MaterialData&);
bool ReadPmdBone(ReadModelBytes, void*, BoneData&);
bool ReadPmxBone(ReadModelBytes, void*, std::uint8_t boneIndexSize, BoneData&);
bool ReadPmdIk(ReadModelBytes, void*, PmdIkData&);
bool ReadPmdRigid(ReadModelBytes, void*, RigidData&);
bool ReadPmxRigid(ReadModelBytes, void*, std::uint8_t boneIndexSize, RigidData&);
bool ReadPmdJoint(ReadModelBytes, void*, JointData&);
bool ReadPmxJoint(ReadModelBytes, void*, std::uint8_t rigidIndexSize, JointData&);
} // namespace mikudancestudio::format
