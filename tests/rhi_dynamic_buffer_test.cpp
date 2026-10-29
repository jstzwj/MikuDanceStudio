#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "material_sampling_fixture.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace mikudancestudio::rhi;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)

RenderFrame Pose(unsigned version, unsigned size = 64) {
    auto frame = sampling_fixture::Fixture(0, SamplerFilter::Linear, 1, SamplerAddress::Clamp, 1,
                                           true, true);
    for (auto& target : frame.targets)
        if (target.id != 3)
            target.description.width = target.description.height = size;
    auto& vertices = frame.resources[0];
    vertices.key.version = version;
    const auto vertexKey = vertices.key;
    for (std::size_t n = 0; n < vertices.bytes.size(); n += sizeof(sampling_fixture::Vertex)) {
        sampling_fixture::Vertex vertex;
        std::memcpy(&vertex, vertices.bytes.data() + n, sizeof(vertex));
        vertex.x += .12f * std::sin(version * .1f);
        std::memcpy(vertices.bytes.data() + n, &vertex, sizeof(vertex));
    }
    ModelReplayResource indices;
    indices.kind = ReplayResourceKind::Indices;
    indices.key = {3, version};
    const std::uint16_t order[] = {0, 1, 2, 3, 4, 5};
    const std::uint16_t alternate[] = {3, 4, 5, 0, 1, 2};
    const auto* source = version % 2 ? order : alternate;
    indices.bytes.resize(sizeof(order));
    std::memcpy(indices.bytes.data(), source, sizeof(order));
    frame.resources.push_back(std::move(indices));
    auto& draw = frame.passes.back().draws[0];
    draw.vertices = vertexKey;
    auto nonIndexed = draw;
    draw.indexed = true;
    draw.indices = {3, version};
    draw.indexCount = draw.vertexCount = 6;
    draw.drawVertexCount = 0;
    frame.passes.back().draws.push_back(nonIndexed);
    return frame;
}

// Several updates queued before a submit, including partial writes, must keep
// independent upload sources even when the retained staging allocation exists.
void VerifyQueuedUpdates(Device& device) {
    auto fixture = sampling_fixture::Fixture(0, SamplerFilter::Linear, 1, SamplerAddress::Clamp, 1,
                                             true, false);
    auto bytes = fixture.resources[0].bytes;
    const auto vb = device.CreateBuffer({BufferUsage::Vertex, bytes.size(), true}, bytes.data());
    const auto tex = device.CreateModelTexture(fixture.resources[1].texture);
    CHECK(vb && tex);
    const auto draw = [&](Handle vertices) {
        CHECK(device.BeginFrame({1, 1, 1, 1}));
        CHECK(device.SetModelPipeline(fixture.passes.back().draws[0].pipeline));
        CHECK(device.BindTexture(0, tex));
        NonIndexedDraw command{vertices, 0, 6};
        command.layout = VertexLayout::PositionNormalUv;
        CHECK(device.Draw(device.Record(command)) && device.EndFrame());
    };
    draw(vb);
    CHECK(device.WaitCompletion(device.SignalCompletion(), 5000));
    for (unsigned round = 0; round < 3; ++round) {
        // The first full write can borrow the retained staging allocation;
        // subsequent queued full/partial writes must not overwrite that source.
        for (unsigned update = 0; update < 3; ++update) {
            const float x = -.8f + .07f * (round * 3 + update);
            std::memcpy(bytes.data(), &x, sizeof(x));
            CHECK(device.Upload(vb, 0, bytes.data(), bytes.size()));
            const float y = .6f + .03f * update;
            std::memcpy(bytes.data() + sizeof(float), &y, sizeof(y));
            if (device.BackendType() == Backend::D3D11) {
                // DX11's WRITE_DISCARD contract requires full replacement.
                CHECK(!device.Upload(vb, sizeof(float), &y, sizeof(y)));
                CHECK(device.Upload(vb, 0, bytes.data(), bytes.size()));
            } else
                CHECK(device.Upload(vb, sizeof(float), &y, sizeof(y)));
        }
        draw(vb);
        RgbaImage actual, expected;
        CHECK(device.ReadModelTarget(actual));
        const auto reference =
            device.CreateBuffer({BufferUsage::Vertex, bytes.size()}, bytes.data());
        CHECK(reference);
        draw(reference);
        CHECK(device.ReadModelTarget(expected) && actual.pixels == expected.pixels);
        CHECK(device.Destroy(reference));
    }
    CHECK(device.Destroy(vb) && device.Destroy(tex));
}

int main(int argc, char** argv) try {
    const std::string api = argc > 1 ? argv[1] : "vulkan";
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = api.find("debug") != std::string::npos;
    options.driver =
        api.find("warp") != std::string::npos ? BackendDriver::Software : BackendDriver::Hardware;
    auto native = CreateOffscreenBackendDevice(64, 64, options);
    if (!native && options.debugLayer &&
        (native.reason == "validation_layer_unavailable" ||
         native.reason == "validation_extensions_unavailable"))
        return 77;
    CHECK(native);
    std::printf("%s: %s; entirely offscreen\n", api.c_str(), native.adapter.c_str());
    // Declare the replacement before the renderer: the device must outlive its owner.
    auto replacement = CreateOffscreenBackendDevice(64, 64, options);
    CHECK(replacement);
    SceneRenderer renderer(*native.device);
    CompletionToken token;
    PreparedModelFrame prepared, stale;
    CHECK(renderer.Prepare(1, Pose(1), prepared) && renderer.Submit(prepared, token));
    AttachmentReadback first, actual, expected;
    CHECK(renderer.ReadTarget(1, 1, first));
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    const auto dxBefore = QueryD3D12Stats(*native.device);
#endif
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    const auto vkBefore = QueryVulkanStats(*native.device);
#endif
    for (unsigned n = 2; n <= 241; ++n) {
        CHECK(renderer.Prepare(1, Pose(n), prepared) && renderer.Submit(prepared, token));
        const auto stats = renderer.Stats();
        // Each resource begins immutable, then owns at most BufferSlots dynamic
        // allocations. The initial immutable allocation is retired once.
        CHECK(stats.bufferCreations <= 2 * (SceneRenderer::BufferSlots + 1));
        CHECK(stats.residentResources == 3 && stats.pooledBuffers + stats.retiringResources <= 6);
        if (n == 240)
            stale = std::move(prepared);
    }
    const auto stats = renderer.Stats();
    CHECK(stats.bufferReuses > 460);
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (options.backend == Backend::D3D12) {
        const auto dx = QueryD3D12Stats(*native.device);
        CHECK(dx.bufferAllocations - dxBefore.bufferAllocations <= 8);
        CHECK(dx.uploadBufferAllocations - dxBefore.uploadBufferAllocations <= 8);
        CHECK(dx.uploadBufferReuses > 460 && dx.debugErrors == 0);
    }
#endif
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan) {
        const auto vk = QueryVulkanStats(*native.device);
        CHECK(vk.bufferAllocations - vkBefore.bufferAllocations <= 8);
        CHECK(vk.idleWaits == vkBefore.idleWaits && vk.transferWaits == vkBefore.transferWaits);
        CHECK(vk.uploadBusyRejects == 0 && vk.validationErrors == 0);
    }
#endif
    CHECK(renderer.ReadTarget(1, 1, actual) && actual.bytes != first.bytes);
    CHECK(renderer.Submit(stale, token).reason == "superseded_prepared_frame");
    CHECK(renderer.Submit(prepared, token) && renderer.ReadTarget(1, 1, expected));
    CHECK(actual.bytes == expected.bytes);
    {
        SceneRenderer reference(*native.device);
        PreparedModelFrame fresh;
        CHECK(reference.Prepare(2, Pose(241), fresh) && reference.Submit(fresh, token));
        CHECK(reference.ReadTarget(2, 1, expected) && expected.bytes == actual.bytes);
    }
    CHECK(renderer.Rebind(*replacement.device));
    CHECK(renderer.Submit(prepared, token).reason == "superseded_prepared_frame");
    CHECK(renderer.Prepare(1, Pose(241), prepared) && renderer.Submit(prepared, token));
    CHECK(renderer.ReadTarget(1, 1, expected) && expected.bytes == actual.bytes);
    unsigned version = 242;
    for (unsigned size : {80u, 64u}) {
        CHECK(renderer.Prepare(1, Pose(version++, size), prepared));
        CHECK(renderer.Submit(prepared, token) && renderer.ReadTarget(1, 1, actual));
        CHECK(actual.description.width == size && actual.description.height == size);
    }
    CHECK(replacement.device->WaitCompletion(token, 5000) && renderer.Unload(1));
    CHECK(renderer.Stats().residentResources == 0 && renderer.Stats().pooledBuffers == 0 &&
          renderer.Stats().retiringResources == 0);
    VerifyQueuedUpdates(*replacement.device);
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (options.backend == Backend::D3D12)
        CHECK(QueryD3D12Stats(*replacement.device).debugErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan)
        CHECK(QueryVulkanStats(*replacement.device).validationErrors == 0);
#endif
    std::printf("240 animated frames: created=%llu reused=%llu slot_waits=%llu; strict pixels, "
                "resize, replacement, unload and queued/partial updates passed\n",
                stats.bufferCreations, stats.bufferReuses, stats.bufferWaits);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
