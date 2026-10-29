#include "mikudancestudio/rhi_frame_passes.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
#include "material_sampling_fixture.hpp"
#include <cstdio>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main() {
    RenderFrame f;
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    f.targets = {{1, {32, 32, TextureFormat::R32Float, access}},
                 {2, {64, 64, TextureFormat::Rgba8, access}},
                 {3,
                  {32, 32, TextureFormat::Depth24Stencil8,
                   std::uint32_t(TextureAccess::DepthStencilAttachment)}},
                 {4,
                  {64, 64, TextureFormat::Depth24Stencil8,
                   std::uint32_t(TextureAccess::DepthStencilAttachment)}}};
    FramePass shadow;
    shadow.color = FrameAttachment{1};
    shadow.depthStencil = FrameAttachment{3};
    FramePass main;
    main.color = FrameAttachment{2};
    main.depthStencil = FrameAttachment{4};
    main.sampled = {1};
    f.passes = {shadow, main};
    CHECK(ValidateRenderFrame(f));
    f.finalColor = 2;
    CHECK(ValidateRenderFrame(f));
    auto bad = f;
    bad.finalColor = 77;
    CHECK(ValidateRenderFrame(bad).reason == "invalid_final_color");
    bad = f;
    bad.finalColor = 1;
    CHECK(ValidateRenderFrame(bad).reason == "invalid_final_color");
    bad = f;
    bad.finalColor = 4;
    CHECK(ValidateRenderFrame(bad).reason == "invalid_final_color");
    bad = f;
    bad.passes[1].color->store = AttachmentStore::Discard;
    CHECK(ValidateRenderFrame(bad).reason == "invalid_final_color");
    bad = f;
    bad.targets[1].description.access = unsigned(TextureAccess::ColorAttachment);
    CHECK(ValidateRenderFrame(bad).reason == "invalid_final_color");
    bad = f;
    std::swap(bad.passes[0], bad.passes[1]);
    CHECK(ValidateRenderFrame(bad).reason == "sample_before_write");
    bad = f;
    bad.passes[0].sampled = {1};
    CHECK(ValidateRenderFrame(bad).reason == "attachment_feedback");
    bad = f;
    bad.passes[0].color->store = AttachmentStore::Discard;
    CHECK(ValidateRenderFrame(bad).reason == "sample_before_write");
    bad = f;
    bad.passes[1].depthStencil->target = 3;
    CHECK(ValidateRenderFrame(bad).reason == "attachment_size_mismatch");
    bad = f;
    bad.passes[1].sampled = {77};
    CHECK(ValidateRenderFrame(bad).reason == "unknown_frame_sample");
    bad = f;
    bad.targets.push_back(bad.targets[0]);
    CHECK(ValidateRenderFrame(bad).reason == "invalid_or_duplicate_frame_target");
    bad = f;
    bad.passes[0].color->load = AttachmentLoad::Load;
    CHECK(ValidateRenderFrame(bad).reason == "load_before_write");
    bad = f;
    bad.targets[0].description.layers = 2;
    CHECK(!ValidateRenderFrame(bad));
    CHECK(ValidateModelReplay(FlattenRenderFrame(f)));
    bad = f;
    bad.passes[0].color.reset();
    bad.passes[0].depthStencil.reset();
    CHECK(!ValidateModelReplay(FlattenRenderFrame(bad)));
    auto sampled = sampling_fixture::Fixture(0, SamplerFilter::Linear, 1, SamplerAddress::Clamp, 1,
                                             false, false);
    sampled.resources.pop_back(); // no immutable material upload
    sampled.targets.push_back({4, {17, 13, TextureFormat::Rgba8, access}});
    sampled.passes[0].color = FrameAttachment{4};
    sampled.passes[1].sampled = {4};
    auto& draw = sampled.passes[1].draws[0];
    draw.textures[0] = {};
    draw.targetTextures[0] = {4, 17, 13};
    CHECK(ValidateRenderFrame(sampled)); // normalized UV sampling permits different extents
    CHECK(ValidateModelReplay(FlattenRenderFrame(sampled)).reason ==
          "render_target_requires_frame");
    auto rejected = sampled;
    rejected.passes[1].draws[0].targetTextures[0].width = 18;
    CHECK(ValidateRenderFrame(rejected).reason == "invalid_or_undeclared_target_texture");
    rejected = sampled;
    rejected.passes[1].draws[0].textures[0] = {2, 1};
    CHECK(ValidateRenderFrame(rejected).reason == "invalid_target_texture_binding");
    rejected = sampled;
    rejected.passes[1].draws[0].pipeline.shader.srgbSample[0] = true;
    CHECK(!ValidateRenderFrame(rejected));
    rejected = sampled;
    rejected.passes[1].sampled.clear();
    CHECK(ValidateRenderFrame(rejected).reason == "invalid_or_undeclared_target_texture");
    rejected = sampled;
    std::swap(rejected.passes[0], rejected.passes[1]);
    CHECK(ValidateRenderFrame(rejected).reason == "sample_before_write");
    rejected = sampled;
    rejected.passes[0].color->store = AttachmentStore::Discard;
    CHECK(ValidateRenderFrame(rejected).reason == "sample_before_write");
    rejected = sampled;
    rejected.passes[1].sampled = {1};
    CHECK(ValidateRenderFrame(rejected).reason == "attachment_feedback");
    auto post = f;
    CHECK(AppendPostProcess(post, 2, 10, 17, 13));
    CHECK(post.finalColor == 10 && ValidateRenderFrame(post));
    auto ordered = post;
    for (auto& pass : ordered.passes)
        if (pass.color) {
            pass.colors.push_back(*pass.color);
            pass.color.reset();
        }
    CHECK(ValidateRenderFrame(ordered)); // identical single-output spelling, including shadow/copy
    CHECK(!AppendPostProcess(post, 10, 10, 17, 13));
    CHECK(!AppendPostProcess(post, 1, 11, 17, 13)); // R32 is not an RGBA color input
    CHECK(!AppendPostProcess(post, 10, 11, 0, 13));
    PostProcessState invalid{PostProcessMode::ColorTransform};
    invalid.uvRect = {2, 1, 0, 0};
    CHECK(!AppendPostProcess(post, 10, 11, 17, 13, invalid));
    CHECK(post.finalColor == 10 && post.passes.size() == f.passes.size() + 1);
    post.passes.back().draws[0].pipeline.raster.alphaBlend = true;
    CHECK(ValidateRenderFrame(post).reason == "invalid_postprocess_pipeline");
    auto history = sampled;
    history.passes.erase(history.passes.begin());
    CHECK(ValidateRenderFrame(history).reason == "sample_before_write");
    history.targets.back().history = ClearColor{0, 0, 0, 1};
    CHECK(ValidateRenderFrame(history));
    history.targets.back().history->red = -1;
    CHECK(ValidateRenderFrame(history).reason == "invalid_history_clear");
    history.targets.back().history = ClearColor{};
    history.targets.back().description.format = TextureFormat::R32Float;
    CHECK(ValidateRenderFrame(history).reason == "invalid_history_target");
    std::puts(
        "frame graph: ordered dependencies, attachments, discard and unsupported ranges passed");
}
