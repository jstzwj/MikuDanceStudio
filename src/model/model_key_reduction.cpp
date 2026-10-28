#include "mikudancestudio/model.hpp"

namespace mikudancestudio::mdl {
namespace {
bool SamePose(const BoneKey& a, const BoneKey& b) {
    // Numeric equality is intentional: signed zeros match, NaNs do not.
    return a.position[0] == b.position[0] && a.position[1] == b.position[1] &&
           a.position[2] == b.position[2] && a.rotation[0] == b.rotation[0] &&
           a.rotation[1] == b.rotation[1] && a.rotation[2] == b.rotation[2] &&
           a.rotation[3] == b.rotation[3];
}
}

// Menu 222, original x86 0x48835D..0x488AE7. Mark first and purge later:
// adjacent candidates must still be compared with their original neighbours.
std::int32_t MarkRedundantModelKeys(ModelRecord& model) {
    for (std::size_t i = 0; i < kBoneKeyCapacity; ++i)
        model.boneKeys[i].allocated = 0;
    for (std::size_t i = 0; i < kMorphKeyCapacity; ++i)
        model.morphKeys[i].allocated = 0;
    for (std::size_t i = 0; i < kDisplayKeyCapacity; ++i)
        model.displayKeys[i].allocated = 0;

    std::int32_t deleted = 0;
    for (std::uint16_t bone = 0;
         static_cast<std::int32_t>(bone) < static_cast<std::int32_t>(model.boneCount); ++bone) {
        const BoneType type = model.boneTable[bone].type;
        if (type == BoneType::InertTip || type == BoneType::Effector)
            continue;
        BoneKey* keys = model.boneKeys;
        auto current = static_cast<std::int32_t>(keys[bone].next);
        if (current == 0)
            continue;
        auto next = static_cast<std::int32_t>(keys[current].next);
        while (next != 0) {
            const auto previous = static_cast<std::int32_t>(keys[current].previous);
            if (SamePose(keys[current], keys[previous]) && SamePose(keys[current], keys[next])) {
                ++deleted;
                keys[current].allocated = 1;
            }
            current = static_cast<std::int32_t>(keys[current].next);
            next = static_cast<std::int32_t>(keys[current].next);
        }
    }

    for (std::uint16_t morph = 0;
         static_cast<std::int32_t>(morph) < static_cast<std::int32_t>(model.morphCount); ++morph) {
        MorphKey* keys = model.morphKeys;
        auto current = static_cast<std::int32_t>(keys[morph].next);
        if (current == 0)
            continue;
        auto next = static_cast<std::int32_t>(keys[current].next);
        while (next != 0) {
            const auto previous = static_cast<std::int32_t>(keys[current].previous);
            if (keys[current].value == keys[previous].value &&
                keys[current].value == keys[next].value) {
                ++deleted;
                keys[current].allocated = 1;
            }
            current = static_cast<std::int32_t>(keys[current].next);
            next = static_cast<std::int32_t>(keys[current].next);
        }
    }

    DisplayKey* keys = model.displayKeys;
    auto current = static_cast<std::int32_t>(keys[0].next);
    if (current != 0) {
        auto next = static_cast<std::int32_t>(keys[current].next);
        while (next != 0) {
            const auto previous = static_cast<std::int32_t>(keys[current].previous);
            const DisplayKey& key = keys[current];
            const DisplayKey& before = keys[previous];
            const DisplayKey& after = keys[next];
            bool same = before.visible == key.visible && after.visible == key.visible;
            if (same) {
                for (std::int32_t i = 0; i < static_cast<std::int32_t>(model.ikChainCount); ++i) {
                    if (before.ikStates[i] != key.ikStates[i] ||
                        after.ikStates[i] != key.ikStates[i]) {
                        same = false;
                        break;
                    }
                }
            }
            if (same) {
                for (std::int32_t i = 0; i < static_cast<std::int32_t>(model.boneOrderCount); ++i) {
                    const BoneReference& state = key.selectorStates[i];
                    if (after.selectorStates[i].modelIndex != state.modelIndex ||
                        after.selectorStates[i].boneIndex != state.boneIndex ||
                        before.selectorStates[i].boneIndex != state.boneIndex ||
                        before.selectorStates[i].modelIndex != state.modelIndex) {
                        same = false;
                        break;
                    }
                }
            }
            if (same) {
                ++deleted;
                keys[current].allocated = 1;
            }
            current = static_cast<std::int32_t>(keys[current].next);
            next = static_cast<std::int32_t>(keys[current].next);
        }
    }
    return deleted;
}

} // namespace mikudancestudio::mdl
