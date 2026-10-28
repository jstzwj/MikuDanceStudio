#include "mikudancestudio/model.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <vector>

using namespace mikudancestudio::mdl;
namespace {
int failures = 0;
struct Fixture {
    std::unique_ptr<ModelRecord> model = std::make_unique<ModelRecord>();
    std::vector<BoneKey> bones = std::vector<BoneKey>(kBoneKeyCapacity);
    std::vector<MorphKey> morphs = std::vector<MorphKey>(kMorphKeyCapacity);
    std::vector<DisplayKey> display = std::vector<DisplayKey>(kDisplayKeyCapacity);
    BoneRecord bone[3]{};
    std::uint8_t ik[3][2]{};
    BoneReference selectors[3][2]{};

    Fixture() {
        model->boneTable = bone;
        model->boneKeys = bones.data();
        model->morphKeys = morphs.data();
        model->displayKeys = display.data();
        Reset();
    }
    void Reset() {
        std::fill(bones.begin(), bones.end(), BoneKey{});
        std::fill(morphs.begin(), morphs.end(), MorphKey{});
        std::fill(display.begin(), display.end(), DisplayKey{});
        std::memset(bone, 0, sizeof(bone));
        std::memset(ik, 0, sizeof(ik));
        std::memset(selectors, 0, sizeof(selectors));
        model->boneCount = 1;
        model->morphCount = 1;
        model->ikChainCount = 2;
        model->boneOrderCount = 2;
        // An interior key near each pool's end exercises the complete capacity.
        const auto b = static_cast<std::uint32_t>(bones.size() - 2);
        bones[0].next = b; bones[b].next = b + 1; bones[b + 1].previous = b;
        const auto m = static_cast<std::uint32_t>(morphs.size() - 2);
        morphs[0].next = m; morphs[m].next = m + 1; morphs[m + 1].previous = m;
        const auto d = static_cast<std::uint32_t>(display.size() - 2);
        display[0].next = d; display[d].next = d + 1; display[d + 1].previous = d;
        for (int i = 0; i < 3; ++i) {
            DisplayKey& key = display[i == 0 ? 0 : d + i - 1];
            key.visible = 1;
            key.ikStates = ik[i];
            key.selectorStates = selectors[i];
            selectors[i][1] = {-1, 0x12345678};
        }
        // Pre-existing marks must be cleared across all pools, including tails.
        for (auto& key : bones) key.allocated = 0xA5;
        for (auto& key : morphs) key.allocated = 0xA5;
        for (auto& key : display) key.allocated = 0xA5;
    }

    template<class Key>
    void CheckPool(const char* name, const std::vector<Key>& actual,
                   std::vector<Key>& before, std::initializer_list<std::size_t> marked) {
        for (auto& key : before) key.allocated = 0;
        for (auto index : marked) before[index].allocated = 1;
        if (std::memcmp(actual.data(), before.data(), actual.size() * sizeof(Key)) != 0) {
            std::fprintf(stderr, "%s: wrong marks or modified key data/links\n", name);
            ++failures;
        }
    }
    void Check(const char* name, bool markBone = true, bool markMorph = true,
               bool markDisplay = true) {
        auto beforeBones = bones;
        auto beforeMorphs = morphs;
        auto beforeDisplay = display;
        const auto count = MarkRedundantModelKeys(*model);
        if (count != int(markBone) + int(markMorph) + int(markDisplay)) {
            std::fprintf(stderr, "%s: incorrect marked count %d\n", name, count);
            ++failures;
        }
        CheckPool(name, bones, beforeBones, markBone ?
            std::initializer_list<std::size_t>{bones.size() - 2} : std::initializer_list<std::size_t>{});
        CheckPool(name, morphs, beforeMorphs, markMorph ?
            std::initializer_list<std::size_t>{morphs.size() - 2} : std::initializer_list<std::size_t>{});
        CheckPool(name, display, beforeDisplay, markDisplay ?
            std::initializer_list<std::size_t>{display.size() - 2} : std::initializer_list<std::size_t>{});
        std::printf("%s: %d\n", name, count);
    }
};
}

int main() {
    Fixture f;
    const std::size_t b = f.bones.size() - 2, m = f.morphs.size() - 2;
    const std::size_t d = f.display.size() - 2;
    f.Check("identical neighbours at pool tails");
    for (int component = 0; component < 7; ++component) {
        for (int neighbour = 0; neighbour < 3; ++neighbour) {
            f.Reset();
            auto& key = f.bones[neighbour == 0 ? 0 : b + neighbour - 1];
            float& value = component < 3 ? key.position[component] : key.rotation[component - 3];
            value = std::numeric_limits<float>::quiet_NaN();
            f.Check("NaN in any bone component/neighbour", false);
            value = std::numeric_limits<float>::denorm_min();
            f.Check("one differing bone component/neighbour", false);
        }
    }
    for (int neighbour = 0; neighbour < 3; ++neighbour) {
        f.Reset();
        f.morphs[neighbour == 0 ? 0 : m + neighbour - 1].value =
            std::numeric_limits<float>::quiet_NaN();
        f.Check("NaN morph", true, false);
    }
    f.Reset();
    f.bones[b].position[0] = -0.0f; f.morphs[m].value = -0.0f;
    f.Check("signed zeros compare numerically");
    f.Reset();
    for (auto index : {std::size_t(0), b, b + 1})
        f.bones[index].rotation[3] = std::numeric_limits<float>::infinity();
    for (auto index : {std::size_t(0), m, m + 1})
        f.morphs[index].value = -std::numeric_limits<float>::infinity();
    f.Check("matching infinities");
    f.Reset();
    f.bones[b].rotation[3] = -1.0f;
    f.Check("quaternion sign is not normalised", false);
    for (auto type : {BoneType::InertTip, BoneType::Effector}) {
        f.Reset(); f.bone[0].type = type;
        f.Check("excluded bone type", false);
    }
    f.Reset();
    f.bones[b].interpolation[7] = 127; f.bones[b].physicsDisabled = 1;
    f.bones[b].frame = 12345; f.morphs[m].frame = 54321;
    f.Check("only pose/value determines redundancy");
    f.Reset(); f.display[d].visible = 0;
    f.Check("display visibility differs", true, true, false);
    for (int neighbour = 0; neighbour < 3; ++neighbour) {
        f.Reset(); f.ik[neighbour][1] = 0xFF;
        f.Check("IK state differs", true, true, false);
        for (int member = 0; member < 2; ++member) {
            f.Reset();
            if (member == 0) f.selectors[neighbour][1].modelIndex = 0x7FFFFFFF;
            else f.selectors[neighbour][1].boneIndex = -123;
            f.Check("selector state differs", true, true, false);
        }
    }
    f.Reset(); f.model->ikChainCount = f.model->boneOrderCount = 0;
    f.display[0].ikStates = f.display[d].ikStates = f.display[d + 1].ikStates = nullptr;
    f.display[0].selectorStates = f.display[d].selectorStates = f.display[d + 1].selectorStates = nullptr;
    f.Check("empty state arrays need no pointers");
    f.Reset(); f.bones[0].next = f.morphs[0].next = f.display[0].next = 0;
    f.Check("empty chains", false, false, false);
    f.Reset(); f.bones[b].next = f.morphs[m].next = f.display[d].next = 0;
    f.Check("final key is retained", false, false, false);
    f.Reset(); f.model->boneCount = f.model->morphCount = 0;
    f.Check("zero bone and morph counts", false, false);

    // Adjacent redundant keys are all marked before any links are changed.
    f.Reset(); f.model->morphCount = f.model->boneCount = 0;
    f.display[0].next = 0;
    f.model->boneCount = 1;
    f.bones[0].next = b - 1; f.bones[b - 1].next = b;
    f.bones[b].previous = b - 1;
    auto before = f.bones;
    const auto count = MarkRedundantModelKeys(*f.model);
    f.CheckPool("adjacent candidates", f.bones, before, {b - 1, b});
    if (count != 2) { ++failures; std::fprintf(stderr, "Adjacent key count\n"); }
    std::printf("adjacent candidates: %d\n", count);
    return failures ? 1 : 0;
}
