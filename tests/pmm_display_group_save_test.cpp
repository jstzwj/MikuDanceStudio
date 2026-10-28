#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "pmm_model_save.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <memory>

int main() {
    using namespace mikudancestudio;
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "%s\n", message);
            ++failures;
        }
    };
    auto model = std::make_unique<mdl::ModelRecord>();
    auto boneKeys = std::make_unique<mdl::BoneKey[]>(mdl::kBoneKeyCapacity);
    auto morphKeys = std::make_unique<mdl::MorphKey[]>(mdl::kMorphKeyCapacity);
    auto displayKeys = std::make_unique<mdl::DisplayKey[]>(mdl::kDisplayKeyCapacity);
    model->boneKeys = boneKeys.get();
    model->morphKeys = morphKeys.get();
    model->displayKeys = displayKeys.get();
    model->boneListPos = 0x12345678;
    model->maxFrame = 0x23456789;
    std::array<mdl::DisplayGroup, 255> groups{};
    std::array<mdl::ModelRecord*, kModelSlotCount> slots{};
    slots.back() = model.get();

    FILE* rawFile = nullptr;
    if (tmpfile_s(&rawFile) != 0 || !rawFile)
        return 1;
    const std::unique_ptr<FILE, decltype(&std::fclose)> file(rawFile, &std::fclose);
    const int fd = _fileno(file.get());

    // PMM 0002 stream positions for empty model names and zero bone/morph/
    // IK/selector counts. These are disk widths, independent of the host ABI:
    // slot + two name lengths + path[256] + group count + four 32-bit counts
    // + order/load bytes + selected bone + four selected morphs.
    constexpr long groupCountOffset = 1 + 2 + 256 + 1 + 4 * 4 + 2 + 4 + 4 * 4;
    constexpr long flagsOffset = groupCountOffset + 1;

    for (unsigned count : {0u, 1u, 255u}) {
        model->groupCount = static_cast<unsigned char>(count);
        model->displayGroups = count == 0 ? nullptr : groups.data();
        for (unsigned shift : {0u, 1u}) {
            for (bool englishNames : {false, true}) {
                for (unsigned i = 0; i < count; ++i) {
                    groups[i] = {};
                    groups[i].name[0] = 'J';
                    groups[i].nameEn[0] = (englishNames != (i % 2 != 0)) ? 'E' : 0;
                    groups[i].flags = static_cast<unsigned char>(i + shift);
                }
                const auto originalGroups = groups;
                check(_chsize(fd, 0) == 0 && _lseek(fd, 0, SEEK_SET) == 0,
                      "reset model-block stream");
                char text[256]{};
                pmm_io::WritePmmModelBlocks(fd, nullptr, slots.data(), text);

                // x64 reference RVA 0x75886 cmp [group+0x64], 0 / setne:
                // every nonzero flag (including 2..255) is stored as one.
                // Both English-name states are tested for each flag value.
                check(_lseek(fd, groupCountOffset, SEEK_SET) == groupCountOffset,
                      "seek to the serialized display-group count");
                unsigned char savedCount = 0;
                check(_read(fd, &savedCount, 1) == 1 && savedCount == count,
                      "write the display-group count at its PMM stream position");
                std::array<unsigned char, 255> savedFlags{};
                check(_read(fd, savedFlags.data(), count) == static_cast<int>(count),
                      "read exactly one saved byte per group");
                bool correct = true;
                for (unsigned i = 0; i < count; ++i) {
                    const unsigned flag = i + shift;
                    correct &= savedFlags[i] == (flag == 0 ? 0 : 1);
                }
                check(correct, "PMM flags match the reference regardless of English names");
                check(_tell(fd) == flagsOffset + count,
                      "group flags occupy exactly the specified stream bytes");

                std::int32_t following[2]{};
                check(_read(fd, following, sizeof following) == sizeof following &&
                          following[0] == model->boneListPos &&
                          following[1] == static_cast<std::int32_t>(model->maxFrame),
                      "following model fields retain their stream positions");
                check(std::memcmp(groups.data(), originalGroups.data(), sizeof groups) == 0,
                      "saving preserves the runtime names and raw group flags");
            }
        }
    }
    return failures == 0 ? 0 : 1;
}
