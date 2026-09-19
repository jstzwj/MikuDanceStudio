#include <cstdio>
#include <cstring>
#include <vector>

#include "mikudancestudio/model.hpp"

namespace mikudancestudio { void ModelInitDefaults(unsigned char*); }

int main() {
    using namespace mikudancestudio;
    std::vector<unsigned char> storage(mdl::kSize, 0);
    auto* bytes = storage.data();
    auto& model = *mdl::Mdl(bytes);
    ModelInitDefaults(bytes);
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition) ++failures;
    };
    check(model.boneKeyCursors == nullptr && model.morphKeyCursors == nullptr,
          "failed-load cleanup receives null animation cursors");
#if defined(_M_X64)
    constexpr std::size_t quaternionOffset = 120;
    constexpr std::size_t historyOffset = 392;
    constexpr std::size_t currentJointOffset = 15216;
#else
    constexpr std::size_t quaternionOffset = 64;
    constexpr std::size_t historyOffset = 336;
    constexpr std::size_t currentJointOffset = 14280;
#endif
    const auto readFloat = [&](std::size_t offset) {
        float value;
        std::memcpy(&value, bytes + offset, sizeof value);
        return value;
    };
    bool identity = true;
    for (int joint = 0; joint < 17; ++joint)
        for (int component = 0; component < 4; ++component)
            identity &= readFloat(quaternionOffset + (joint * 4 + component) * 4)
                     == (component == 3 ? 1.0f : 0.0f);
    check(identity, "all 17 standard-pose quaternions are identity");
    bool history = true;
    for (int sample = 0; sample < 23 * 30; ++sample)
        for (int component = 0; component < 3; ++component)
            history &= readFloat(historyOffset + (sample * 3 + component) * 4)
                    == (component == 1 ? -999.0f : 0.0f);
    check(history, "all 690 tracking history samples carry the missing-joint sentinel");
    bool current = true;
    for (int joint = 0; joint < 23; ++joint)
        current &= readFloat(currentJointOffset + joint * 12 + 4) == -999.0f;
    check(current, "all 23 current joints carry the missing-joint sentinel");
    bool toonUntouched = true;
    for (const auto& filename : model.pmdToonFileNames)
        for (char c : filename) toonUntouched &= c == 0;
    check(toonUntouched, "tracking initialization does not overwrite toon filenames");
    model.pmxTextEncoding = 0;
    model.pmxAdditionalUvCount = 0;
    model.pmxVertexIndexSize = 2;
    model.pmxTextureIndexSize = 1;
    model.pmxMaterialIndexSize = 1;
    model.pmxBoneIndexSize = 2;
    model.pmxMorphIndexSize = 1;
    model.pmxRigidIndexSize = 1;
    check(mdl::PoseTraceBuffer(bytes) == nullptr,
          "a valid PMX header does not create a pose-trace allocation");
    return failures == 0 ? 0 : 1;
}
