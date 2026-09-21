#include "mikudancestudio/model.hpp"
#include <cstdio>
#include <memory>
#include <vector>

int main() {
    using namespace mikudancestudio::mdl;
    auto model = std::make_unique<ModelRecord>();
    std::vector<BoneKey> keys(kBoneKeyCapacity);
    std::vector<MorphKey> morphs(kMorphKeyCapacity);
    std::vector<DisplayKey> display(kDisplayKeyCapacity);
    BoneRecord bones[3]{};
    std::uint8_t ikStates[3] = {1, 0, 1};
    BoneReference selector{7, 19};
    model->boneTable = bones;
    model->boneCount = 3;
    model->boneKeys = keys.data();
    model->morphKeys = morphs.data();
    model->displayKeys = display.data();
    bones[0].hasRigidBody = 1;
    bones[2].hasRigidBody = 1;
    keys[0].next = 8;
    keys[8].next = 15;
    keys[8].physicsDisabled = 1;
    keys[2].physicsDisabled = 1;
    keys[2].next = 16;
    for (auto& key : keys) key.allocated = 1;
    for (auto& key : morphs) {
        key.frame = 42;
        key.value = 0.25f;
        key.allocated = 1;
    }
    for (auto& key : display) {
        key.frame = 91;
        key.visible = 1;
        key.ikStates = ikStates;
        key.selectorStates = &selector;
        key.allocated = 1;
    }
    SelectPhysicsOnBoneKeys(*model);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const bool selected = i == 0 || i == 15 || i == 16;
        if ((keys[i].allocated != 0) != selected) {
            std::fprintf(stderr, "Unexpected selection at key %zu\n", i);
            return 1;
        }
    }
    for (const auto& key : display) {
        if (key.allocated || key.frame != 91 || key.visible != 1 ||
            key.ikStates != ikStates || key.selectorStates != &selector) {
            std::fprintf(stderr, "Display selection damaged a key or pointer\n");
            return 1;
        }
    }
    for (const auto& key : morphs)
        if (key.allocated || key.frame != 42 || key.value != 0.25f) return 1;
    if (keys[0].next != 8 || keys[8].next != 15 || !keys[8].physicsDisabled)
        return 1;
    return 0;
}
