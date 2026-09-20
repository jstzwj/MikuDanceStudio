#include <cstdio>
#include <memory>
#include <type_traits>

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/app_layout.hpp"

namespace mikudancestudio { void InitBoneSortOrder(unsigned char*); }

int main() {
    using namespace mikudancestudio;
    static_assert(std::is_same_v<decltype(mdl::ModelRecord::boneOrderTable),
                                 mdl::BoneOrderEntry*>);
    static_assert(std::is_same_v<decltype(MMDAppState::selectNavRecords),
                                 mdl::BoneOrderEntry*>);
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bones[5]{};
    bones[0].type = mdl::BoneType::Move;
    bones[1].type = mdl::BoneType::RotateMove;
    bones[2].type = mdl::BoneType::Ik;
    bones[3].type = mdl::BoneType::Move;
    bones[4].type = mdl::BoneType::RotateMove;
    model->boneTable = bones;
    model->boneCount = 5;
    auto* bytes = reinterpret_cast<unsigned char*>(model.get());
    InitBoneSortOrder(bytes);
    auto* entries = model->boneOrderTable;
    check(model->boneOrderCount == 4 && entries[0].boneIndex == -1 &&
          entries[1].boneIndex == 0 && entries[2].boneIndex == 2 &&
          entries[3].boneIndex == 3, "root and movable bones retain source order");
    check(bones[0].slotIndex == 1 && bones[2].slotIndex == 2 &&
          bones[3].slotIndex == 3 && entries[3].linkedModel == -1 &&
          entries[3].linkedBone == 0 && entries[3].windowStart == 0 &&
          entries[3].windowEnd == 0, "selectors and bone back references initialize together");
    entries[1] = {0, 9, 300, 42, 81};
    entries[2] = {2, 17, 999, -2, 0};
    entries[3] = {3, 99, 101, 7, 5};
    std::unique_ptr<mdl::BoneOrderEntry[]> copy(mdl::CopyBoneBindings(entries, 4));
    copy[1].linkedBone = 63;
    check(entries[1].linkedBone == 81 && copy[1].windowStart == 9 &&
          copy[1].windowEnd == 300 && copy[2].linkedModel == -2,
          "dialog copy preserves intervals and ground sentinel without mutating model");
    std::copy_n(copy.get(), 4, entries);
    check(entries[1].linkedBone == 63, "apply copies typed binding edits back");
    mdl::DetachBoneBindings(entries, 4, 42);
    check(entries[1].linkedModel == -1 && entries[1].linkedBone == 0 &&
          entries[1].boneIndex == 0 && entries[1].windowStart == 9 &&
          entries[1].windowEnd == 300 && entries[2].linkedModel == -2 &&
          entries[3].linkedModel == 7 && entries[3].linkedBone == 5,
          "model removal clears only matching external parents");
    model->boneCount = 0;
    InitBoneSortOrder(bytes);
    check(model->boneOrderCount == 1 && model->boneOrderTable[0].boneIndex == -1 &&
          model->boneOrderTable[0].linkedModel == -1,
          "rebuilding releases previous table and retains root for empty model");
    delete[] model->boneOrderTable;
    std::unique_ptr<mdl::BoneOrderEntry[]> empty(mdl::CopyBoneBindings(nullptr, 0));
    mdl::DetachBoneBindings(nullptr, 0, 42);
    return failures ? 1 : 0;
}
