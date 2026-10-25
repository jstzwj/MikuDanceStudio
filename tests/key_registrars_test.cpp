#include "mikudancestudio/clipboard_layout.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/bone_binding.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace mdl = mikudancestudio::mdl;
namespace {
int failures = 0;
std::vector<int> undoRecords;
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}
}
namespace mikudancestudio {
void AppendBoneKeyToUndo(mdl::ModelRecord*, int index) {
    undoRecords.push_back(index);
}
}
int main() {
    using namespace mikudancestudio;
    auto model = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord, 2> bones{};
    std::array<mdl::BoneKey, 8> boneKeys{};
    model->boneTable = bones.data();
    model->boneCount = 2;
    model->boneKeys = boneKeys.data();
    strcpy_s(bones[0].name, "other");
    strcpy_s(bones[1].name, "target");
    bones[1].type = mdl::BoneType::RotateMove;
    mdl::BoneClipboardRecord bone{};
    strcpy_s(bone.name, "target");
    bone.frame = 7;
    bone.position[0] = 2;
    bone.position[1] = 3;
    bone.position[2] = 4;
    bone.rotation[0] = .25f;
    bone.rotation[1] = .5f;
    bone.rotation[2] = .75f;
    bone.rotation[3] = 1;
    bone.physicsDisabled = 1;
    for (unsigned char i = 0; i < 16; ++i)
        bone.interpolation[i] = i + 10;
    ResetBoneKeyCursor(model.get());
    Check(RegisterBoneKey(model.get(), bone, 3, 0), "register named bone");
    Check(boneKeys[1].next == 2 && boneKeys[2].frame == 10 && boneKeys[2].previous == 1,
          "bone lookup and relative frame preserve links");
    Check(std::memcmp(boneKeys[2].position, bone.position, 12) == 0 &&
              std::memcmp(boneKeys[2].rotation, bone.rotation, 16) == 0 &&
              std::memcmp(boneKeys[2].interpolation, bone.interpolation, 16) == 0 &&
              boneKeys[2].physicsDisabled == 1,
          "bone payload fields retain their byte order");
    Check(undoRecords == std::vector<int>({1, 2}), "bone append records both changed links");
    bone.frame = 0;
    undoRecords.clear();
    Check(RegisterBoneKey(model.get(), bone, 0, 0) && boneKeys[1].next == 2 &&
              undoRecords == std::vector<int>({1}),
          "frame zero overwrites the head with one undo entry");
    bone.frame = 20;
    Check(RegisterMirroredBoneKey(model.get(), bone, 0), "register reflected key");
    const auto& mirrored = boneKeys[boneKeys[2].next];
    Check(mirrored.position[0] == -2 && mirrored.position[1] == 3 && mirrored.position[2] == 4 &&
              mirrored.rotation[0] == .25f && mirrored.rotation[1] == -.5f &&
              mirrored.rotation[2] == -.75f && mirrored.rotation[3] == 1,
          "reflection changes position X and quaternion Y/Z");

    // Guard the old +=136 typed-pointer bug without relying on an access violation.
    // A distant decoy gives that invalid walk a deterministic, incorrect result.
    std::array<mdl::MorphRecord, 137> morphs{};
    std::array<mdl::MorphKey, 8> morphKeys{};
    model->morphs = morphs.data();
    model->morphCount = 2;
    model->morphKeys = morphKeys.data();
    strcpy_s(morphs[0].name, "other");
    strcpy_s(morphs[1].name, "smile");
    strcpy_s(morphs[136].name, "decoy");
    mdl::MorphClipboardRecord morph{};
    strcpy_s(morph.name, "smile");
    morph.frame = 9;
    morph.value = .625f;
    ResetMorphKeyCursor(model.get());
    Check(RegisterMorphKeyFromRecord(model.get(), morph, 2), "register second morph");
    Check(morphKeys[1].next == 2 && morphKeys[2].frame == 11 && morphKeys[2].value == .625f,
          "morph lookup advances one native record, on both architectures");
    morph.value = .25f;
    Check(RegisterMorphKeyFromRecord(model.get(), morph, 2) && morphKeys[2].value == .25f &&
              morphKeys[2].next == 0,
          "morph overwrite allocates no extra key");
    strcpy_s(morph.name, "missing");
    Check(RegisterMorphKeyFromRecord(model.get(), morph, 2) && morphKeys[2].value == .25f,
          "unknown morph remains a successful no-op");
    std::array<mdl::DisplayKey, 8> displayKeys{};
    std::array<mdl::IkChain, 2> chains{};
    std::array<mdl::BoneOrderEntry, 2> relationships{};
    std::array<std::array<unsigned char, 2>, 8> flags{};
    std::array<std::array<mdl::BoneReference, 2>, 8> pairs{};
    model->displayKeys = displayKeys.data();
    model->ikChains = chains.data();
    model->ikChainCount = 2;
    model->boneOrderTable = relationships.data();
    model->boneOrderCount = 2;
    for (int i = 0; i < 8; ++i) {
        mdl::IkStates(displayKeys[i]) = flags[i].data();
        mdl::SelectorStates(displayKeys[i]) = pairs[i].data();
    }
    chains[0].boneIndex = relationships[0].boneIndex = 0;
    chains[1].boneIndex = relationships[1].boneIndex = 1;
    std::array<mdl::IkClipboardState, 2> ikEntries{};
    std::array<mdl::SelectorClipboardState, 2> selectorEntries{};
    strcpy_s(ikEntries[0].boneName, "target");
    ikEntries[0].enabled = 1;
    strcpy_s(ikEntries[1].boneName, "other");
    ikEntries[1].enabled = 0;
    strcpy_s(selectorEntries[0].boneName, "target");
    selectorEntries[0].modelIndex = 7;
    selectorEntries[0].boneIndex = 11;
    strcpy_s(selectorEntries[1].boneName, "other");
    selectorEntries[1].modelIndex = -1;
    selectorEntries[1].boneIndex = 5;
    model->searchCursor = 1;
    Check(RegisterDisplayKeyFromRecord(model.get(), 6, 1, 2, ikEntries.data(), 2,
                                       selectorEntries.data(), 2),
          "register display inputs");
    Check(displayKeys[0].next == 1 && displayKeys[1].frame == 8 && flags[1][0] == 0 &&
              flags[1][1] == 1,
          "IK names resolve independently of input order");
    Check(pairs[1][0].modelIndex == -1 && pairs[1][0].boneIndex == 5 &&
              pairs[1][1].modelIndex == 7 && pairs[1][1].boneIndex == 11,
          "selector payload uses the original base after the name cursor advances");
    return failures ? 1 : 0;
}
