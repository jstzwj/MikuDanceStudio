#include "mikudancestudio/rhi_frame_passes.hpp"
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
    auto bad = f;
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
    bad=f;bad.passes[0].color.reset();bad.passes[0].depthStencil.reset();
    CHECK(!ValidateModelReplay(FlattenRenderFrame(bad)));
    std::puts(
        "frame graph: ordered dependencies, attachments, discard and unsupported ranges passed");
}
