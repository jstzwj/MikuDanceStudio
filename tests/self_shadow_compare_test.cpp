#include "self_shadow_compare.hpp"
#include <cstdio>
#include <limits>
using namespace mikudancestudio::rhi;
using namespace shadow_acceptance;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main() {
    AttachmentReadback color{{2, 1, TextureFormat::Rgba8}, {80, 100, 120, 255, 255, 255, 255, 0}};
    auto changed = color;
    CHECK(Compare(color, changed).Exact() && Compare(color, changed).Accepted());
    changed.bytes[0]++;
    CHECK(!Compare(color, changed).Exact() && Compare(color, changed).Accepted());
    changed.bytes[0]++;
    CHECK(!Compare(color, changed).Accepted());
    changed = color;
    changed.bytes[3]--;
    CHECK(!Compare(color, changed).Accepted());
    // One byte at a background edge must not hide changed raster coverage.
    changed = color;
    changed.bytes[4]--;
    CHECK(Compare(color, changed).outsideTolerance == 0 &&
          Compare(color, changed).coverageDifferences == 1 && !Compare(color, changed).Accepted());
    AttachmentReadback depth{{2, 1, TextureFormat::R32Float}, std::vector<std::uint8_t>(8)};
    const auto put = [](AttachmentReadback& image, unsigned index, float value) {
        std::memcpy(image.bytes.data() + index * 4, &value, 4);
    };
    put(depth, 0, .5f);
    put(depth, 1, 1);
    changed = depth;
    put(changed, 0, std::nextafter(.5f, 1.f));
    CHECK(!Compare(depth, changed).Exact() && Compare(depth, changed).Accepted());
    put(changed, 0, .500002f);
    CHECK(!Compare(depth, changed).Accepted());
    changed = depth;
    put(changed, 1, std::nextafter(1.f, 0.f));
    CHECK(Compare(depth, changed).outsideTolerance == 0 && !Compare(depth, changed).Accepted());
    bool rejected = false;
    try {
        put(changed, 0, std::numeric_limits<float>::quiet_NaN());
        Compare(depth, changed);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);
    rejected = false;
    try {
        changed = color;
        changed.description.width = 1;
        changed.description.height = 2;
        Compare(color, changed);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);
    std::puts(
        "self-shadow policy: bounded RGB/depth, exact alpha/coverage, finite matching images passed");
}
