#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "material_sampling_fixture.hpp"
#include <cstdio>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (0)
int main(int argc, char** argv) try {
    const std::string api = argc > 1 ? argv[1] : "vulkan";
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = api.find("debug") != std::string::npos;
    if (api.find("measured") != std::string::npos)
        options.model.compatibility = ModelCompatibility::Dx9Measured;
    auto native = CreateOffscreenBackendDevice(96, 64, options);
    if (!native && options.debugLayer &&
        (native.reason == "validation_layer_unavailable" ||
         native.reason == "validation_extensions_unavailable"))
        return 77;
    CHECK(native);
    SceneRenderer renderer(*native.device);
    CompletionToken token;
    PreparedModelFrame prepared;
    for (unsigned size : {64u, 80u, 64u}) {
        auto frame = sampling_fixture::Fixture(2, SamplerFilter::Linear, 1, SamplerAddress::Clamp,
                                               1, true, true);
        for (auto& t : frame.targets)
            if (t.id == 1 || t.id == 2)
                t.description.width = t.description.height = size;
        CHECK(AppendPostProcess(frame, 1, 5, size, size));
        PostProcessState color{PostProcessMode::ColorTransform};
        color.multiply = {0, 1, 0, 1};
        color.add = {.25f, 0, .75f, 0};
        CHECK(AppendPostProcess(frame, 5, 6, size, size, color));
        CHECK(AppendPostProcess(frame, 6, 7, size, size));
        {
            const auto r = renderer.Prepare(1, frame, prepared);
            if (!r)
                throw std::runtime_error(r.reason);
            const auto submitted = renderer.Submit(prepared, token);
            if (!submitted)
                throw std::runtime_error(submitted.reason);
        }
        AttachmentReadback source, copy, transformed, final;
        CHECK(renderer.ReadTarget(1, 1, source) && renderer.ReadTarget(1, 5, copy) &&
              renderer.ReadTarget(1, 6, transformed) && renderer.ReadTarget(1, 7, final));
        CHECK(source.bytes == copy.bytes && transformed.bytes == final.bytes);
        for (std::size_t n = 0; n < final.bytes.size(); n += 4)
            CHECK(final.bytes[n] == 64 && final.bytes[n + 1] == source.bytes[n + 1] &&
                  final.bytes[n + 2] == 191 && final.bytes[n + 3] == source.bytes[n + 3]);
        RgbaImage image;
        CHECK(native.device->ReadModelTarget(image) && image.pixels == final.bytes);
        for (unsigned n = 0; n < 20; ++n)
            CHECK(renderer.Submit(prepared, token));
        CHECK(renderer.ReadTarget(1, 7, final) && image.pixels == final.bytes);
        // Exercise normalized UV resizing and back-to-back target reuse.
        CHECK(AppendPostProcess(frame, 7, 8, 17, 13));
        {
            const auto r = renderer.Prepare(1, frame, prepared);
            if (!r)
                throw std::runtime_error(r.reason);
            const auto submitted = renderer.Submit(prepared, token);
            if (!submitted)
                throw std::runtime_error(submitted.reason);
        }
        AttachmentReadback small;
        CHECK(renderer.ReadTarget(1, 8, small));
        for (unsigned y = 0; y < 13; ++y)
            for (unsigned x = 0; x < 17; ++x) {
                const unsigned sx = unsigned((x + .5) * size / 17),
                               sy = unsigned((y + .5) * size / 13);
                for (unsigned c = 0; c < 4; ++c)
                    CHECK(small.bytes[(y * 17 + x) * 4 + c] ==
                          final.bytes[(sy * size + sx) * 4 + c]);
            }
        PostProcessState graded{PostProcessMode::ColorTransform};
        graded.multiply = {.5f, .75f, .25f, .5f};
        graded.add = {.1f, .05f, .2f, .125f};
        CHECK(AppendPostProcess(frame, 8, 9, 17, 13, graded));
        CHECK(renderer.Prepare(1, frame, prepared) && renderer.Submit(prepared, token));
        AttachmentReadback rgba;
        CHECK(renderer.ReadTarget(1, 9, rgba));
        for (std::size_t n = 0; n < rgba.bytes.size(); ++n) {
            const auto c = n % 4;
            const float value = small.bytes[n] / 255.f * graded.multiply[c] + graded.add[c];
            const int expected = int(std::floor(std::clamp(value, 0.f, 1.f) * 255 + .5f));
            CHECK(std::abs(int(rgba.bytes[n]) - expected) <= 1);
        }
        CHECK(native.device->ReadModelTarget(image) && image.pixels == rgba.bytes);
        CHECK(renderer.Unload(1));
    }
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan)
        CHECK(QueryVulkanStats(*native.device).validationErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (options.backend == Backend::D3D12)
        CHECK(QueryD3D12Stats(*native.device).debugErrors == 0);
#endif
    CHECK(renderer.Detach());
    std::printf(
        "%s postprocess: exact copy, RGBA transform, orientation, resize and repeat passed\n",
        api.c_str());
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
