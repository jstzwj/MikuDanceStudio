#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "self_shadow_compare.hpp"
#include "material_sampling_fixture.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (0)
namespace {
using sampling_fixture::Fixture;
AttachmentReadback Render(Device& device, const RenderFrame& frame) {
    SceneRenderer renderer(device);
    PreparedModelFrame prepared;
    CompletionToken token;
    AttachmentReadback out, repeated;
    CHECK(renderer.Prepare(1, frame, prepared));
    CHECK(renderer.Submit(prepared, token));
    CHECK(renderer.ReadTarget(1, 1, out));
    RgbaImage final;
    CHECK(device.ReadModelTarget(final));
    CHECK(final.width == out.description.width && final.height == out.description.height &&
          final.pixels == out.bytes);
    CHECK(renderer.Submit(prepared, token));
    CHECK(renderer.ReadTarget(1, 1, repeated));
    CHECK(out.bytes == repeated.bytes);
    CHECK(renderer.Unload(1) && renderer.Detach());
    return out;
}
}
int main(int argc, char** argv) try {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool measure = argc > 1 && std::strcmp(argv[1], "native-measure") == 0;
    const bool nativeGate = argc > 1 && std::strcmp(argv[1], "native-gate") == 0;
    CHECK(argc == 1 || (argc == 2 && (measure || nativeGate)));
    BackendOptions options;
    options.backend = Backend::D3D11;
    auto eleven = CreateOffscreenBackendDevice(96, 64, options);
    options.backend = Backend::D3D12;
    auto twelve = CreateOffscreenBackendDevice(96, 64, options);
    options.backend = Backend::Vulkan;
    auto vulkan = CreateOffscreenBackendDevice(96, 64, options);
    CHECK(eleven && twelve && vulkan);
    bool accepted = true;
    std::size_t cases = 0;
    for (float scale : {.37f, 7.37f, 37.37f})
        for (bool perspective : {false, true})
            for (bool shadow : {false, true})
                for (unsigned pattern : {0u, 1u, 2u})
                    for (bool mips : {false, true})
                        for (auto address : {SamplerAddress::Wrap, SamplerAddress::Clamp})
                            for (auto filter : {SamplerFilter::Point, SamplerFilter::Linear,
                                                SamplerFilter::Anisotropic})
                                for (unsigned anisotropy : {1u, 4u, 16u}) {
                                    if (filter != SamplerFilter::Anisotropic && anisotropy != 1)
                                        continue;
                                    auto frame = Fixture(pattern, filter, anisotropy, address,
                                                         scale, mips, shadow, perspective);
                                    if (filter == SamplerFilter::Anisotropic && !measure &&
                                        !nativeGate)
                                        CHECK(ApplyMaterialSampling(
                                            frame, MaterialSamplingPolicy::PortableTrilinear));
                                    frame.finalColor = 1;
                                    const auto a = Render(*eleven.device, frame),
                                               b = Render(*twelve.device, frame),
                                               c = Render(*vulkan.device, frame);
                                    const auto dx = shadow_acceptance::Compare(a, b),
                                               vk = shadow_acceptance::Compare(a, c);
                                    ++cases;
                                    if (measure || !dx.Accepted() || !vk.Accepted())
                                        std::printf(
                                            "scale=%.2f perspective=%d shadow=%d pattern=%u mips=%d address=%u filter=%u aniso=%u dx=%zu/%.0f vk=%zu/%.0f outside=%zu coverage=%zu\n",
                                            scale, perspective, shadow, pattern, mips,
                                            unsigned(address), unsigned(filter), anisotropy,
                                            dx.elements, dx.maximum, vk.elements, vk.maximum,
                                            vk.outsideTolerance, vk.coverageDifferences);
                                    accepted &= dx.Accepted() && vk.Accepted();
                                }
    std::printf("%s: %zu sampling cases, acceptance=%s\n",
                measure      ? "native measurement"
                : nativeGate ? "native gate"
                             : "portable gate",
                cases, accepted ? "pass" : "fail");
    return measure || accepted ? 0 : 1;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
