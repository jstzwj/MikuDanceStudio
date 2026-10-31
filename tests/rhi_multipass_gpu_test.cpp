#include "rhi_test_platform.hpp"
#include "mikudancestudio/rhi_d3d11.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/rhi_render_pass.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include <algorithm>
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
const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
struct Vertex {
    float x, y, z, nx, ny, nz, u, v;
};
std::vector<Vertex> Quad(float size, float z) {
    return {{-size, size, z, 0, 0, -1, 0, 0},  {size, size, z, 0, 0, -1, 1, 0},
            {-size, -size, z, 0, 0, -1, 0, 1}, {-size, -size, z, 0, 0, -1, 0, 1},
            {size, size, z, 0, 0, -1, 1, 0},   {size, -size, z, 0, 0, -1, 1, 1}};
}
ModelPipeline Pipeline(BuiltinShadowMode mode) {
    ModelPipeline p;
    p.raster.alphaBlend = false;
    p.raster.cull = BatchCull::None;
    p.shader.world = p.shader.view = p.shader.projection = p.shader.normalView = identity;
    p.shader.nativeDiffuse = p.shader.diffuse = {1, 1, 1, 1};
    p.shadow.mode = mode;
    p.shadow.lightViewProjection = p.shadow.worldViewProjection = p.shadow.rotation = identity;
    p.shadow.lightViewProjection[8] = 1;
    p.shadow.color = {.75f, .25f, .125f, 1};
    p.shadow.toonColor = {.5f, .5f, .5f, 1};
    p.shadow.lightDirection = {0, 0, 1, 0};
    p.shadow.eye = {0, 0, -3, 1};
    p.shadow.specular = {0, 0, 0, 1};
    return p;
}
RenderFrame Fixture(unsigned size) {
    RenderFrame f;
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    f.targets = {{1, {size, size, TextureFormat::R32Float, access}},
                 {2, {size, size, TextureFormat::Rgba8, access}},
                 {3,
                  {size, size, TextureFormat::Depth24Stencil8,
                   std::uint32_t(TextureAccess::DepthStencilAttachment)}},
                 {4,
                  {size, size, TextureFormat::Depth24Stencil8,
                   std::uint32_t(TextureAccess::DepthStencilAttachment)}}};
    for (unsigned n = 0; n < 2; ++n) {
        auto vertices = Quad(n ? .15f : .95f, n ? .2f : .8f);
        ModelReplayResource r;
        r.key = {n + 1, 1};
        r.bytes.resize(vertices.size() * sizeof(Vertex));
        std::memcpy(r.bytes.data(), vertices.data(), r.bytes.size());
        f.resources.push_back(r);
    }
    FramePass shadow;
    shadow.color = FrameAttachment{1};
    shadow.color->clear = {1, 1, 1, 1};
    shadow.depthStencil = FrameAttachment{3};
    ModelReplayDraw cast;
    cast.indexed = false;
    cast.drawVertexCount = 6;
    cast.vertices = {2, 1};
    cast.pipeline = Pipeline(BuiltinShadowMode::Depth);
    shadow.draws.push_back(cast);
    FramePass main;
    main.color = FrameAttachment{2};
    main.color->clear = {0, 0, 0, 0};
    main.depthStencil = FrameAttachment{4};
    main.sampled = {1};
    ModelReplayDraw receive = cast;
    receive.vertices = {1, 1};
    receive.shadowTarget = 1;
    receive.pipeline = Pipeline(BuiltinShadowMode::Material);
    main.draws.push_back(receive);
    f.passes = {shadow, main};
    return f;
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string arg = argc > 1 ? argv[1] : "dx11-warp";
    const bool dx12 = arg.find("dx12") != std::string::npos,
               warp = arg.find("warp") != std::string::npos;
    const bool debug = arg.find("debug") != std::string::npos;
    const bool vulkan = arg.find("vulkan") != std::string::npos;
    BackendOptions options;
    options.backend = vulkan ? Backend::Vulkan : dx12 ? Backend::D3D12 : Backend::D3D11;
    options.driver = warp ? BackendDriver::Software : BackendDriver::Hardware;
    options.debugLayer = debug;
    ConfigureVulkanTest(options, arg);
    auto native = CreateOffscreenBackendDevice(64, 64, options);
    if (!native) {
        std::fprintf(stderr, "creation failed: %lld %s\n",
                     static_cast<long long>(native.nativeError), native.reason.c_str());
        return vulkan && debug &&
                       (native.reason == "validation_layer_unavailable" ||
                        native.reason == "validation_extensions_unavailable")
                   ? 77
                   : 1;
    }
    std::printf("adapter: %s\n", native.adapter.c_str());
    auto& d = *native.device;
    CHECK(d.ModelFeatures().offscreen && !d.ModelFeatures().presentation);
    auto floor = Quad(.95f, .8f), blocker = Quad(.15f, .2f);
    auto floorV =
        d.CreateBuffer({BufferUsage::Vertex, floor.size() * sizeof(Vertex)}, floor.data());
    auto blockerV =
        d.CreateBuffer({BufferUsage::Vertex, blocker.size() * sizeof(Vertex)}, blocker.data());
    CHECK(floorV && blockerV);
    std::vector<std::uint8_t> baseline;
    for (unsigned iteration = 0; iteration < 6; ++iteration) {
        const unsigned size = iteration % 2 ? 80 : 64;
        HistoryDesc target;
        target.width = target.height = size;
        CHECK(d.EnsureModelTarget(target));
        RenderTextureDesc color{size, size, TextureFormat::Rgba8,
                                TextureAccess::ColorAttachment | TextureAccess::Sampled |
                                    TextureAccess::CopySource};
        auto shadow = color;
        shadow.format = TextureFormat::R32Float;
        auto depth = color;
        depth.format = TextureFormat::Depth24Stencil8;
        depth.access = std::uint32_t(TextureAccess::DepthStencilAttachment);
        const auto map = d.CreateRenderTexture(shadow), mapDepth = d.CreateRenderTexture(depth);
        const auto main = d.CreateRenderTexture(color), mainDepth = d.CreateRenderTexture(depth);
        CHECK(map && mapDepth && main && mainDepth);
        RenderPassDesc generate;
        generate.color = PassAttachment{map};
        generate.color->clear = {1, 1, 1, 1};
        generate.depthStencil = PassAttachment{mapDepth};
        RenderPassDesc compose;
        compose.color = PassAttachment{main};
        compose.color->clear = {0, 0, 0, 0};
        compose.depthStencil = PassAttachment{mainDepth};
        compose.sampled = {map};
        NonIndexedDraw cast{blockerV, 0, 6};
        cast.layout = VertexLayout::PositionNormalUv;
        NonIndexedDraw receive{floorV, 0, 6};
        receive.layout = VertexLayout::PositionNormalUv;
        CHECK(d.BeginFrame({}));
        CHECK(!d.BeginRenderPass(compose) && d.BeginRenderPass(generate));
        CHECK(!d.EndFrame() && !d.Destroy(map));
        CHECK(d.SetModelPipeline(Pipeline(BuiltinShadowMode::Depth)) && d.Draw(d.Record(cast)) &&
              d.EndRenderPass());
        CHECK(d.BeginRenderPass(compose) &&
              d.SetModelPipeline(Pipeline(BuiltinShadowMode::Material)));
        CHECK(!d.Draw(d.Record(receive)) && d.BindShadowTexture(map) && d.Draw(d.Record(receive)));
        CHECK(d.EndRenderPass() && d.EndFrame());
        const auto complete = d.SignalCompletion();
        CHECK(complete && d.WaitCompletion(complete, 5000));
        AttachmentReadback mapImage, image;
        CHECK(d.ReadRenderTexture(map, mapImage) && d.ReadRenderTexture(main, image));
        CHECK(image.bytes.size() == std::size_t(size) * size * 4 &&
              mapImage.bytes.size() == image.bytes.size());
        std::size_t dark = 0, lit = 0, covered = 0;
        for (std::size_t n = 0; n < image.bytes.size(); n += 4) {
            float depthValue = 0;
            std::memcpy(&depthValue, mapImage.bytes.data() + n, 4);
            covered += depthValue < .5f;
            dark += image.bytes[n] == 96 && image.bytes[n + 1] == 32 && image.bytes[n + 2] == 16 &&
                    image.bytes[n + 3] == 255;
            lit += image.bytes[n] == 191 && image.bytes[n + 1] == 64 && image.bytes[n + 2] == 32 &&
                   image.bytes[n + 3] == 255;
        }
        std::printf("%s %u: shadow=%zu dark=%zu lit=%zu\n", arg.c_str(), size, covered, dark, lit);
        CHECK(covered > 20 && dark > 20 && lit > 500);
        if (iteration == 0)
            baseline = image.bytes;
        else if (size == 64)
            CHECK(image.bytes == baseline);
        // Resizing the legacy output invalidates explicit attachment contents
        // and prepared submission epochs, without changing CPU geometry.
        auto resize = target;
        resize.width += 8;
        const auto stale = d.Record(cast);
        CHECK(d.EnsureModelTarget(resize) && !d.ReadRenderTexture(map, mapImage));
        CHECK(d.BeginFrame({}) && d.BeginRenderPass(generate));
        CHECK(d.SetModelPipeline(Pipeline(BuiltinShadowMode::Depth)) && !d.Draw(stale));
        CHECK(d.Draw(d.Record(cast)) && d.EndRenderPass() && d.EndFrame());
        const auto token = d.SignalCompletion();
        CHECK(token && d.WaitCompletion(token, 5000));
        CHECK(d.Destroy(map) && d.Destroy(mapDepth) && d.Destroy(main) && d.Destroy(mainDepth));
        CHECK(!d.ReadRenderTexture(map, mapImage));
    }
    CHECK(d.Destroy(floorV) && d.Destroy(blockerV));
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (dx12)
        CHECK(QueryD3D12Stats(d).debugErrors == 0);
#endif
    OffscreenBackendDevice replacement, recovery;
    SceneRenderer renderer(d);
    PreparedModelFrame prepared;
    CompletionToken token;
    const auto frame = Fixture(64);
    CHECK(ValidateRenderFrame(frame));
    CHECK(renderer.Prepare(1, frame, prepared));
    for (unsigned n = 0; n < 100; ++n)
        CHECK(renderer.Submit(prepared, token));
    CHECK(d.WaitCompletion(token, 5000) && renderer.Stats().uploads == 2);
    AttachmentReadback rendered;
    CHECK(renderer.ReadTarget(1, 2, rendered) && rendered.bytes == baseline);
    auto invalid = frame;
    std::swap(invalid.passes[0], invalid.passes[1]);
    PreparedModelFrame rejected;
    CHECK(renderer.Prepare(1, invalid, rejected).reason == "sample_before_write" &&
          renderer.Submit(prepared, token));
    for (unsigned n = 0; n < 6; ++n) {
        PreparedModelFrame resized;
        CHECK(renderer.Prepare(1, Fixture(n % 2 ? 64 : 80), resized));
        CHECK(!renderer.Submit(prepared, token) && renderer.Submit(resized, token));
        prepared = std::move(resized);
    }
    CHECK(renderer.ReadTarget(1, 2, rendered) && rendered.bytes == baseline);
    CHECK(renderer.Stats().uploads == 2);
    replacement = CreateOffscreenBackendDevice(64, 64, options);
    CHECK(replacement);
    auto& replaced = *replacement.device;
    CHECK(renderer.Rebind(replaced) && !renderer.Submit(prepared, token));
    CHECK(renderer.Prepare(1, frame, prepared) && renderer.Submit(prepared, token));
    CHECK(renderer.ReadTarget(1, 2, rendered) && rendered.bytes == baseline &&
          renderer.Stats().uploads == 4);
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (dx12)
        CHECK(QueryD3D12Stats(d).debugErrors == 0 && QueryD3D12Stats(replaced).debugErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (vulkan)
        CHECK(QueryVulkanStats(d).validationErrors == 0 &&
              QueryVulkanStats(replaced).validationErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (dx12 && warp) {
        CHECK(RemoveD3D12DeviceForTest(replaced) &&
              replaced.PollCompletion(token) == CompletionState::DeviceLost);
        CHECK(renderer.Detach());
        replacement.device.reset();
        native.device.reset();
        recovery = CreateOffscreenBackendDevice(64, 64, options);
        CHECK(recovery);
        CHECK(renderer.Rebind(*recovery.device) && !renderer.Submit(prepared, token));
        CHECK(renderer.Prepare(1, frame, prepared) && renderer.Submit(prepared, token));
        CHECK(renderer.ReadTarget(1, 2, rendered) && rendered.bytes == baseline);
        CHECK(QueryD3D12Stats(*recovery.device).debugErrors == 0);
    }
#endif
    CHECK(renderer.Unload(1) && !renderer.ReadTarget(1, 2, rendered));
    CHECK(renderer.Detach() && renderer.Stats().residentResources == 0 &&
          renderer.Stats().retiringResources == 0);
    std::puts(
        "offscreen resident passes: 100 submissions, resize, unload, replacement and recovery passed");
    return 0;
}
