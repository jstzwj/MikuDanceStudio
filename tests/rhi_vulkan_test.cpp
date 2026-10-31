#include "rhi_test_platform.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/rhi_render_pass.hpp"
#include <array>
#include <cstdio>
#include <cstring>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(int argc, char** argv) {
    const bool validation = argc > 1 && std::strcmp(argv[1], "validation") == 0;
    CHECK(BackendAvailable(Backend::Vulkan));
    BackendOptions options;
    options.backend = Backend::Vulkan;
    options.debugLayer = validation;
    ConfigureVulkanTest(options, argc > 1 ? argv[1] : "");
    CHECK(!CreateBackendDevice({}, options));
    CHECK(!CreateOffscreenBackendDevice(0, 32, options));
    auto unsupported = options;
    unsupported.model.compatibility = ModelCompatibility::Dx9Measured;
    CHECK(CreateOffscreenBackendDevice(32, 32, unsupported).reason == "model_policy_not_supported");
    auto native = CreateOffscreenBackendDevice(32, 32, options);
    if (!native) {
        std::fprintf(stderr, "%s (%lld)\n", native.reason.c_str(),
                     static_cast<long long>(native.nativeError));
        return validation && (native.reason == "validation_layer_unavailable" ||
                              native.reason == "validation_extensions_unavailable")
                   ? 77
                   : 1;
    }
    std::printf("Vulkan %s, validation=%d\n", native.adapter.c_str(), validation);
    auto& d = *native.device;
    CHECK(d.BackendType() == Backend::Vulkan && d.ModelFeatures().offscreen &&
          !d.ModelFeatures().presentation && !d.Present());
    CHECK(!d.ModelFeatures().generateMips && !d.Features().compute);
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    struct Vertex {
        float x, y, z;
        std::uint32_t color;
    };
    const Vertex vertices[] = {{-1, 1, .5f, 0xffffffff},
                               {1, 1, .5f, 0xffffffff},
                               {-1, -1, .5f, 0xffffffff},
                               {1, -1, .5f, 0xffffffff}};
    const std::uint16_t indices[] = {0, 1, 2, 2, 1, 3};
    const auto vb = d.CreateBuffer({BufferUsage::Vertex, sizeof(vertices)}, vertices);
    auto ib = d.CreateBuffer({BufferUsage::Index, sizeof(indices)}, indices);
    CHECK(vb && ib);
    // A stale generation and a handle from another device must never name an allocation.
    auto stale = ib;
    CHECK(d.Destroy(ib));
    ib = d.CreateBuffer({BufferUsage::Index, sizeof(indices)}, indices);
    CHECK(ib && ib.index == stale.index && ib.generation != stale.generation && !d.Destroy(stale));
    auto alien = vb;
    alien.device += 1;
    CHECK(!d.Upload(alien, 0, vertices, sizeof(vertices)));
    ModelTextureData texture;
    texture.levels = {{2, 2, {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255}},
                      {1, 1, {255, 0, 0, 255}}};
    auto invalid = texture;
    invalid.levels[1].width = 2;
    CHECK(!d.CreateModelTexture(invalid));
    invalid = texture;
    invalid.generateMips = true;
    CHECK(!d.CreateModelTexture(invalid));
    const auto tex = d.CreateModelTexture(texture);
    CHECK(tex);
    CHECK(!d.UploadTexture(tex, {texture.levels[0].pixels.data(), 8, 16}));
    RenderTextureDesc description{32, 32, TextureFormat::Rgba8,
                                  TextureAccess::ColorAttachment | TextureAccess::Sampled |
                                      TextureAccess::CopySource};
    const auto color = d.CreateRenderTexture(description);
    CHECK(color);
    auto depthDescription = description;
    depthDescription.format = TextureFormat::Depth24Stencil8;
    depthDescription.access = unsigned(TextureAccess::DepthStencilAttachment);
    const auto depth = d.CreateRenderTexture(depthDescription);
    CHECK(depth);
    RenderPassDesc pass;
    pass.color = PassAttachment{color};
    pass.color->clear = {0, 0, 1, 1};
    pass.depthStencil = PassAttachment{depth};
    ModelPipeline p;
    p.vertexLayout = VertexLayout::PositionColor;
    p.raster.cull = BatchCull::None;
    p.raster.alphaBlend = false;
    p.shader.world = p.shader.view = p.shader.projection = p.shader.normalView = identity;
    p.shader.diffuse = p.shader.nativeDiffuse = {1, 1, 1, 1};
    p.shader.layout.stages[0].role = MaterialTextureRole::Base;
    p.shader.layout.stages[0].color = MaterialCombine::Modulate;
    p.shader.layout.stages[0].colorArg2 = MaterialColorSource::LitDiffuse;
    p.shader.textureTransforms[0] = identity;
    IndexedDraw draw{vb, ib, 0, 6, 4, VertexLayout::PositionColor};
    CHECK(d.BeginFrame({}));
    auto bad = pass;
    bad.color->load = AttachmentLoad::Load;
    CHECK(!d.BeginRenderPass(bad));
    bad = pass;
    bad.sampled = {color};
    CHECK(!d.BeginRenderPass(bad));
    CHECK(d.BeginRenderPass(pass) && !d.BeginRenderPass(pass) && !d.EndFrame());
    CHECK(!d.CreateRenderTexture(description) && !d.Destroy(tex));
    CHECK(d.SetModelPipeline(p));
    CHECK(!d.DrawIndexed(d.Record(draw)) && d.BindTexture(0, tex));
    auto range = draw;
    range.firstIndex = 1;
    CHECK(!d.DrawIndexed(d.Record(range)));
    CHECK(d.DrawIndexed(d.Record(draw)) && d.EndRenderPass() && d.EndFrame());
    auto token = d.SignalCompletion();
    CHECK(token && d.WaitCompletion(token, 5000) &&
          d.PollCompletion(token) == CompletionState::Complete);
    auto invalidToken = token;
    invalidToken.device += 1;
    CHECK(d.PollCompletion(invalidToken) == CompletionState::Invalid);
    AttachmentReadback image;
    CHECK(d.ReadRenderTexture(color, image));
    unsigned red = 0;
    for (std::size_t n = 0; n < image.bytes.size(); n += 4)
        red += image.bytes[n] == 255 && !image.bytes[n + 1] && !image.bytes[n + 2] &&
               image.bytes[n + 3] == 255;
    CHECK(red > 900);
    // Logical reset keeps immutable uploads but invalidates contents and recorded work.
    const auto old = d.Record(draw);
    CHECK(d.PrepareForReset() && d.Status().state == DeviceState::ResetPending &&
          !d.BeginFrame({}));
    CHECK(d.RestoreAfterReset() && !d.ReadRenderTexture(color, image));
    CHECK(d.BeginFrame({}) && d.BeginRenderPass(pass) && d.SetModelPipeline(p) &&
          d.BindTexture(0, tex));
    CHECK(!d.DrawIndexed(old) && d.DrawIndexed(d.Record(draw)) && d.EndRenderPass() &&
          d.EndFrame());
    CHECK(d.ReadRenderTexture(color, image));
    // Strong minification selects uploaded lower mips through the normal/UV program.
    struct UvVertex {
        float x, y, z, nx, ny, nz, u, v;
    };
    const UvVertex uvVertices[] = {{-1, 1, .5f, 0, 0, -1, 0, 0},
                                   {1, 1, .5f, 0, 0, -1, 32, 0},
                                   {-1, -1, .5f, 0, 0, -1, 0, 32},
                                   {1, -1, .5f, 0, 0, -1, 32, 32}};
    const auto uvb = d.CreateBuffer({BufferUsage::Vertex, sizeof(uvVertices)}, uvVertices);
    ModelTextureData chain;
    for (unsigned size = 256; size; size /= 2) {
        RgbaImage level{size, size, std::vector<std::uint8_t>(std::size_t(size) * size * 4)};
        for (std::size_t n = 0; n < level.pixels.size(); n += 4) {
            level.pixels[n + (size == 256 ? 0 : 2)] = 255;
            level.pixels[n + 3] = 255;
        }
        chain.levels.push_back(std::move(level));
    }
    const auto mipTexture = d.CreateModelTexture(chain);
    ModelTextureData encoded;
    encoded.srgb = true;
    encoded.levels = {{1, 1, {128, 128, 128, 255}}};
    const auto srgb = d.CreateModelTexture(encoded);
    CHECK(uvb && mipTexture && srgb);
    auto uvPipeline = p;
    uvPipeline.vertexLayout = VertexLayout::PositionNormalUv;
    auto uvDraw = draw;
    uvDraw.vertices = uvb;
    uvDraw.layout = VertexLayout::PositionNormalUv;
    pass.color->clear = {0, 1, 0, 1};
    CHECK(d.BeginFrame({}) && d.BeginRenderPass(pass) && d.SetModelPipeline(uvPipeline) &&
          d.BindTexture(0, mipTexture));
    CHECK(d.DrawIndexed(d.Record(uvDraw)) && d.EndRenderPass() && d.EndFrame() &&
          d.ReadRenderTexture(color, image));
    unsigned blue = 0;
    for (std::size_t n = 0; n < image.bytes.size(); n += 4)
        blue += !image.bytes[n] && !image.bytes[n + 1] && image.bytes[n + 2] == 255 &&
                image.bytes[n + 3] == 255;
    CHECK(blue > 900);
    CHECK(d.BeginFrame({}) && d.BeginRenderPass(pass) && d.SetModelPipeline(uvPipeline) &&
          d.BindTexture(0, srgb));
    CHECK(
        !d.DrawIndexed(d.Record(uvDraw))); // sRGB storage must match the declared sample contract.
    uvPipeline.shader.srgbSample[0] = true;
    CHECK(d.SetModelPipeline(uvPipeline) && d.BindTexture(0, srgb) &&
          d.DrawIndexed(d.Record(uvDraw)));
    CHECK(d.EndRenderPass() && d.EndFrame() && d.ReadRenderTexture(color, image));
    unsigned linearGray = 0;
    for (std::size_t n = 0; n < image.bytes.size(); n += 4)
        linearGray += image.bytes[n] == 55 && image.bytes[n + 1] == 55 &&
                      image.bytes[n + 2] == 55 && image.bytes[n + 3] == 255;
    CHECK(linearGray > 900);
    CHECK(d.Destroy(uvb) && d.Destroy(mipTexture) && d.Destroy(srgb));
    // Discarded stores cannot become readable or sampled by a later pass.
    pass.color->store = AttachmentStore::Discard;
    CHECK(d.BeginFrame({}) && d.BeginRenderPass(pass) && d.EndRenderPass() && d.EndFrame());
    CHECK(!d.ReadRenderTexture(color, image));
    // Host-controlled queue delay makes resource completion deterministic. An
    // unrelated pending submission must not turn Upload/Destroy into an idle wait.
    const auto unused = d.CreateBuffer({BufferUsage::Vertex, sizeof(vertices), true}, vertices);
    const auto overlay = d.CreateBuffer({BufferUsage::Vertex, sizeof(vertices), true}, vertices);
    NonIndexedDraw overlayDraw{overlay, 0, 3};
    overlayDraw.layout = VertexLayout::PositionColor;
    CHECK(unused && overlay && SetVulkanQueueBlockedForTest(d, true));
    pass.color->store = AttachmentStore::Store;
    CHECK(d.BeginFrame({}) && d.BeginRenderPass(pass) && d.SetModelPipeline(p) &&
          d.BindTexture(0, tex) && d.DrawIndexed(d.Record(draw)) && d.Draw(d.Record(overlayDraw)) &&
          d.EndRenderPass() && d.EndFrame());
    token = d.SignalCompletion();
    CHECK(d.PollCompletion(token) == CompletionState::Pending);
    const auto before = QueryVulkanStats(d);
    CHECK(!d.Upload(vb, 0, vertices, sizeof(vertices)));
    CHECK(!d.Upload(overlay, 0, vertices, sizeof(vertices)));
    CHECK(d.Upload(unused, 0, vertices, sizeof(vertices)) && d.Destroy(unused));
    CHECK(QueryVulkanStats(d).retiringResources == 0);
    CHECK(d.Destroy(color) && d.Destroy(depth) && d.Destroy(tex) && d.Destroy(vb) && d.Destroy(ib));
    CHECK(d.Destroy(overlay) && QueryVulkanStats(d).retiringResources == 6);
    CHECK(!d.Destroy(vb) && !d.Upload(vb, 0, vertices, sizeof(vertices)));
    const auto replacement = d.CreateBuffer({BufferUsage::Vertex, sizeof(vertices)}, vertices);
    CHECK(replacement &&
          (replacement.index != vb.index || replacement.generation != vb.generation) &&
          d.Destroy(replacement));
    const auto pending = QueryVulkanStats(d);
    CHECK(pending.idleWaits == before.idleWaits && pending.timelineWaits == before.timelineWaits &&
          pending.uploadBusyRejects == before.uploadBusyRejects + 2);
    CHECK(SetVulkanQueueBlockedForTest(d, false) && d.WaitCompletion(token, 5000));
    CHECK(QueryVulkanStats(d).retiringResources == 0);
    CHECK(QueryVulkanStats(d).validationErrors == 0);
    std::puts(
        "Vulkan upload, explicit mips, indexed material draw, readback, invalid dependencies and reset passed");
    return 0;
}
