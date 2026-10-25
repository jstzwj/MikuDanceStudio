// ===========================================================================
// VA 0x00490070 - InitBoneSortOrder  (original: sub_490070, 175 bytes)
// ===========================================================================
// Builds the bone transform-evaluation order table after a PMD/PMX parse.
//
//   1. slotCount (model+314600) = 1 + number of bones whose type byte
//      (&bone->type) is Move or Ik (translate controls / IK bones).
//   2. Frees any previous table (model+314596) and allocates
//      20 B per slot: { int boneIndex; int +4=0; int +8=0; int +12=-1;
//      int +16=0 }.
//   3. Slot 0 is the root dummy { -1, 0, 0, -1, 0 }; every matching bone
//      gets slot `n` with its bone index, and the slot number is written
//      back to &bone->slotIndex.
//
// The original returns the last written dword (never used by callers);
// the port keeps the existing void signature.
// =========================================================================//
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

void InitBoneSortOrder(mdl::ModelRecord* m) {
    mdl::ModelRecord& model = *m;
    // ---- pass 1: count slots ------------------------------------------------
    model.boneOrderCount = 1;
    if (model.boneCount > 0) {
        mikudancestudio::mdl::BoneRecord* bones = model.boneTable;
        for (std::uint32_t i = 0; i < model.boneCount; ++i) {
            const mdl::BoneType t = bones[i].type;
            if (t == mdl::BoneType::Move || t == mdl::BoneType::Ik)
                ++model.boneOrderCount;
        }
    }

    // ---- (re)allocate the slot table ---------------------------------------
    if (model.boneOrderTable != nullptr) {
        delete[] model.boneOrderTable;
        model.boneOrderTable = nullptr;
    }
    const std::uint32_t slots = model.boneOrderCount;
    auto* tbl = new mdl::BoneOrderEntry[slots]{};
    model.boneOrderTable = tbl;
    tbl[0].boneIndex = -1;
    tbl[0].linkedModel = -1;

    // ---- pass 2: assign slots ----------------------------------------------
    model.boneOrderCount = 1;
    if (model.boneCount > 0) {
        mikudancestudio::mdl::BoneRecord* bones = model.boneTable;
        const std::uint32_t n = model.boneCount;
        for (std::uint32_t boneIdx = 0; boneIdx < n; ++boneIdx) {
            mikudancestudio::mdl::BoneRecord* bone = &bones[boneIdx];
            const mdl::BoneType t = bone->type;
            if (t == mdl::BoneType::Move || t == mdl::BoneType::Ik) {
                auto& entry = tbl[model.boneOrderCount];
                entry.boneIndex = boneIdx;
                entry.linkedModel = -1;
                bone->slotIndex = model.boneOrderCount++;
            }
        }
    }
}

} // namespace mikudancestudio
