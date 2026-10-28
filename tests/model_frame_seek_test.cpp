#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
std::uint64_t hash = 14695981039346656037ULL;
void Capture(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
}
template <typename T> void Capture(const T& value) {
    Capture(&value, sizeof(value));
}
} // namespace

// Isolate seeking from Bullet and curve evaluation. Record notification order
// and the pose at each notification, including calls before working-pose copies.
namespace mikudancestudio {
void SeekPortableSceneModel(mdl::ModelRecord*, int) {}
void NotifyBonePhysicsMode(mdl::ModelRecord* model, int index, unsigned char mode) {
    Capture(index);
    Capture(mode);
    Capture(model->boneTable[index]);
}
float BoneEase(const mdl::ModelRecord*, int channel, int key, float fraction) {
    Capture(channel);
    Capture(key);
    Capture(fraction);
    return fraction * (channel == 3 ? 0.875f : 0.75f);
}
} // namespace mikudancestudio

static bool CheckPositionRounding() {
    using namespace mikudancestudio;
    struct Case {
        float previous, next;
        std::uint32_t x64Bits, x86Bits;
    };
    // Expected stages follow x64 RVA 0xCD5E8/0xCD5EC (mulss then addss).
    // x86 keeps the existing double multiply/add, with one final float store.
    const Case cases[] = {
        {0.1f, 0.2f, 0x3E051EB8, 0x3E051EB9},  {-0.1f, 0.2f, 0xBC23D708, 0xBC23D704},
        {1.1f, -2.3f, 0x3DA3D700, 0x3DA3D704}, {100000.0f, -100000.0f, 0x471C3FFF, 0x471C3FFF},
        {-0.0f, 0.0f, 0x80000000, 0x80000000},
    };
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bone{};
    mdl::BoneKey keys[2]{};
    mdl::DisplayKey display{};
    model->boneCount = 1;
    model->boneTable = &bone;
    model->boneKeys = keys;
    model->displayKeys = &display;
    bone.slotIndex = -1;
    keys[0].next = 1;
    keys[1].frame = 10;
    keys[0].rotation[3] = keys[1].rotation[3] = 1.0f;
    for (const auto& test : cases) {
        for (int axis = 0; axis < 3; ++axis) {
            keys[0].position[axis] = test.previous;
            keys[1].position[axis] = test.next;
        }
        // The curve substitute returns (4/10)*0.75 == 0.3f for each axis.
        SeekModelFrame(*model, 4, 0);
        for (float position : bone.trans) {
            std::uint32_t bits;
            std::memcpy(&bits, &position, sizeof(bits));
#if defined(_M_X64)
            const auto expected = test.x64Bits;
#else
            const auto expected = test.x86Bits;
#endif
            if (bits != expected) {
                std::fprintf(stderr, "position rounding: %08X != %08X\n", bits, expected);
                return false;
            }
        }
    }
    return true;
}

int main() {
    using namespace mikudancestudio;
    const int frames[] = {-1, 0, 1, 7, 10, 11, 19, 29, 30, 31, 50};
    auto model = std::make_unique<mdl::ModelRecord>();
    int checks = 0;
    for (int variant = 0; variant < 16; ++variant)
        for (int mode = 0; mode <= 4; ++mode)
            for (int frame : frames) {
                *model = {};
                mdl::BoneRecord bones[3]{};
                mdl::BoneKey boneKeys[9]{};
                mdl::MorphRecord morphs[2]{};
                mdl::MorphKey morphKeys[6]{};
                mdl::DisplayKey display[3]{};
                mdl::IkChain chains[2]{};
                mdl::BoneOrderEntry selectors[2]{};
                std::uint8_t flags[3][2] = {{1, 0}, {0, 1}, {1, 1}};
                mdl::BoneReference pairs[3][2] = {
                    {{-1, -1}, {-1, -1}}, {{2, 5}, {-1, -1}}, {{2, 5}, {3, 9}}};
                model->boneCount = 3;
                model->boneTable = bones;
                model->boneKeys = boneKeys;
                model->morphCount = variant == 15 ? 0 : 2;
                model->morphs = morphs;
                model->morphKeys = morphKeys;
                model->displayKeys = display;
                model->ikChains = chains;
                model->ikChainCount = 2;
                model->boneOrderTable = selectors;
                model->boneOrderCount = 2;
                for (int k = 0; k < 3; ++k) {
                    display[k].frame = k == 2 ? 30 : k * 10;
                    display[k].previous = k ? k - 1 : 0;
                    display[k].next = k < 2 ? k + 1 : 0;
                    display[k].visible = k != 1;
                    display[k].ikStates = flags[k];
                    display[k].selectorStates = pairs[k];
                    if (variant & 1)
                        pairs[k][0] = pairs[0][0];
                }
                for (int b = 0; b < 3; ++b) {
                    bones[b].type = static_cast<mdl::BoneType>(b == 2 ? (variant & 1 ? 7 : 255)
                                                                      : (variant & 2 ? 8 : 0));
                    bones[b].slotIndex = variant & 4 ? -1 : b % 2;
                    bones[b].hasRigidBody = variant & 8 ? 0 : 1;
                    bones[b].physicsDisabled = variant % 2;
                    bones[b].rigidIdx = variant % 3 == 0 ? 40 : 3;
                    for (int c = 0; c < 7; ++c)
                        bones[b].ikBackup[c] = (c + 1) * 0.125f;
                    for (int k = 0; k < 3; ++k) {
                        auto& key = boneKeys[b + k * 3];
                        key.frame = k == 2 ? 30 : k * 10;
                        key.previous = k ? b + (k - 1) * 3 : b;
                        key.next = k < 2 ? b + (k + 1) * 3 : 0;
                        key.physicsDisabled = (k + variant) % 2;
                        for (int c = 0; c < 3; ++c)
                            key.position[c] = (c - 1.125f) * (k + 0.625f);
                        key.rotation[1] = k == 1 ? 0.6f : 0.0f;
                        key.rotation[3] = k == 1 ? 0.8f : 1.0f;
                        if (variant & 2)
                            key.rotation[3] *= -1.0f;
                        if (variant == 14)
                            key.rotation[3] = 1.00001f;
                    }
                }
                for (int m = 0; m < 2; ++m)
                    for (int k = 0; k < 3; ++k) {
                        auto& key = morphKeys[m + k * 2];
                        key.frame = k == 2 ? 30 : k * 10;
                        key.previous = k ? m + (k - 1) * 2 : m;
                        key.next = k < 2 ? m + (k + 1) * 2 : 0;
                        key.value = (k - 0.3125f) * (m + 0.1f);
                    }
                const int result = SeekModelFrame(*model, frame, mode);
                Capture(result);
                Capture(model->loadComplete);
                Capture(bones);
                Capture(morphs);
                Capture(selectors);
                Capture(chains[0].enabled);
                Capture(chains[1].enabled);
                ++checks;
            }
    std::printf("%d seek cases: %016llX\n", checks, static_cast<unsigned long long>(hash));
    // x86: unmodified 24d8844. x64: the same unsplit implementation, with
    // only position interpolation corrected to the original mulss/addss stages.
    // Includes float bits and signed zero; do not regenerate from production.
#if defined(_M_X64)
    constexpr std::uint64_t expected = 0x76B2F2F248828717ULL;
#else
    constexpr std::uint64_t expected = 0x705C9B6BD795F475ULL;
#endif
    const bool stateMatches = hash == expected;
    return CheckPositionRounding() && stateMatches ? 0 : 1;
}
