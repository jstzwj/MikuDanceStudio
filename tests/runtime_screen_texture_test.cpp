#include "mikudancestudio/runtime_cpu_scene_producer.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_material_sampling.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "material_sampling_fixture.hpp"
#include "screen_texture_fixture.hpp"
#include <fstream>
#include <iostream>
using namespace mikudancestudio;
using namespace mikudancestudio::rhi;
using namespace mikudancestudio::render;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " + #x); \
    } while (0)
namespace {
using screen_acceptance::Require;
using screen_acceptance::Image;
using screen_acceptance::CompareCapture;
int Read(void* p, void* out, std::size_t count) {
    auto& in = *static_cast<std::istream*>(p);
    in.read(static_cast<char*>(out), count);
    return int(in.gcount());
}
ModelFrameInputs Inputs(unsigned width = 128) {
    ModelFrameInputs f;
    f.world = f.lightRotation = sampling_fixture::identity;
    f.view.viewport = {0, 0, width, 96, 0, 1};
    f.view.aspect = float(width) / 96;
    return f;
}
void Pmm(const std::filesystem::path& root, const FileAssetSource& assets, Device& device,
         const char* name = "bound-v2.pmm") {
    format::SceneResourceResolver resolver(root / name);
    format::SceneSession session;
    std::ifstream input(root / name, std::ios::binary);
    CHECK(format::SceneSession::Load(Read, &input, format::SceneResourceResolver::Resolve,
                                     &resolver, session));
    RuntimeCpuScene scene;
    std::string reason;
    CHECK(scene.Load(session, resolver.Resolved(), reason));
    ToonTextureResolver textures(root, assets);
    for (auto slot : scene.Order())
        CHECK(scene.Model(slot)->PrepareMaterials(textures, reason));
    RuntimeCpuSceneProducer producer;
    CHECK(producer.Load(scene, resolver, true, assets, reason));
    CHECK(producer.RequiresFrame());
    ModelReplayStream bare;
    CHECK(!producer.Build(scene, Inputs(), HistoryDesc{128, 96}, bare, reason));
    if (reason != "screen_texture_requires_frame")
        throw std::runtime_error(reason);
    SceneRenderer renderer(device);
    PreparedModelFrame prepared;
    CompletionToken token;
    AttachmentReadback previous;
    bool captured = false;
    for (unsigned capture : {1u, 1u, 0u, 2u, 3u, 1u}) {
        CHECK(scene.Edit([&](auto& d) { d.view.captureMode = std::uint8_t(capture); }, reason));
        SelfShadowSettings shadow;
        shadow.mode = 1;
        shadow.mapSize = 64;
        RenderFrame frame;
        CHECK(producer.BuildFrame(scene, Inputs(), HistoryDesc{128, 96}, shadow, frame, reason));
        CHECK(ApplyMaterialSampling(frame, MaterialSamplingPolicy::PortableTrilinear));
        std::uint64_t sampled = 0;
        for (const auto& p : frame.passes)
            for (const auto& d : p.draws)
                if (d.targetTextures[0].target >= ScreenTextureHistory::FirstTarget)
                    sampled = d.targetTextures[0].target;
        Require(renderer.Prepare(1, frame, prepared));
        Require(renderer.Submit(prepared, token));
        AttachmentReadback source, final;
        CHECK(renderer.ReadTarget(1, 1, source) && renderer.ReadTarget(1, 5, final) &&
              source.bytes == final.bytes);
        if (captured && sampled) {
            AttachmentReadback old;
            CHECK(renderer.ReadTarget(1, sampled, old) && old.bytes == previous.bytes);
        }
        producer.CommitFrame();
        if (capture == 1 || capture == 2) {
            const auto target = frame.passes.back().color->target;
            CHECK(renderer.ReadTarget(1, target, previous));
            captured = true;
        }
    }
    CHECK(renderer.Unload(1) && renderer.Detach());
}
void RealModel(const std::filesystem::path& model, const std::filesystem::path& root,
               const FileAssetSource& assets, Device& device) {
    std::string reason;
    RuntimeCpuScene scene;
    CHECK(scene.LoadModel(model, reason));
    ToonTextureResolver textures(model.parent_path(), assets);
    for (auto slot : scene.Order())
        CHECK(scene.Model(slot)->PrepareMaterials(textures, reason));
    format::SceneResourceResolver resolver(model);
    RuntimeCpuSceneProducer producer;
    CHECK(producer.Load(scene, resolver, true, assets, reason));
    RuntimeCpuAccessory accessory;
    CHECK(accessory.Load(root / "bound.x", 1, reason) && accessory.HasScreenTexture());
    SceneRenderer renderer(device);
    ScreenTextureHistory history;
    screen_reference::Capture dx9;
    PreparedModelFrame prepared;
    CompletionToken token;
    AttachmentReadback previous;
    for (unsigned n = 0; n < 3; ++n) {
        SelfShadowSettings settings;
        settings.mode = 1;
        settings.mapSize = 128;
        RenderFrame frame;
        auto input = Inputs();
        CHECK(producer.BuildFrame(scene, input, HistoryDesc{128, 96}, settings, frame, reason));
        CHECK(scene.FrameInputs(input, reason));
        format::PmmEvaluatedAccessory pose;
        pose.visible = 1;
        pose.opacity = 1;
        std::vector<SceneDrawReplacement> fragments;
        CHECK(accessory.Build(
            input, sampling_fixture::identity, pose, false, true, HistoryDesc{128, 96},
            {1, 1, 1, 0}, [&](auto d) { fragments.push_back(std::move(d)); }, reason));
        FramePass overlay;
        overlay.name = "screen_accessory";
        overlay.color = FrameAttachment{1, AttachmentLoad::Load};
        overlay.depthStencil = FrameAttachment{2, AttachmentLoad::Load};
        std::map<ReplayResourceKey, ReplayResourceKey> remap;
        for (const auto& fragment : fragments)
            for (auto resource : fragment.cpu.resources) {
                if (remap.count(resource.key))
                    continue;
                const auto old = resource.key;
                resource.key.id = 0xfffe000000000000ull + remap.size();
                remap.emplace(old, resource.key);
                frame.resources.push_back(std::move(resource));
            }
        std::vector<ScreenTextureConsumer> consumers;
        for (const auto& fragment : fragments)
            for (const auto& e : fragment.cpu.events)
                if (e.op == ModelReplayOp::Draw) {
                    auto draw = e.draw;
                    draw.vertices = remap.at(draw.vertices);
                    draw.indices = remap.at(draw.indices);
                    for (auto& t : draw.textures)
                        if (t.id)
                            t = remap.at(t);
                    if (fragment.screenTexture)
                        consumers.push_back({frame.passes.size(), overlay.draws.size()});
                    overlay.draws.push_back(std::move(draw));
                }
        CHECK(!consumers.empty());
        frame.passes.push_back(std::move(overlay));
        CHECK(AppendPostProcess(frame, 1, 5, 128, 96));
        const auto mode =
            n == 2 ? ScreenCaptureKind::CropFourByThree : ScreenCaptureKind::FullFrame;
        const auto read = history.ReadTarget();
        CHECK(history.Apply(frame, consumers, mode, 5));
        CHECK(ApplyMaterialSampling(frame, MaterialSamplingPolicy::PortableTrilinear));
        Require(renderer.Prepare(1, frame, prepared));
        Require(renderer.Submit(prepared, token));
        AttachmentReadback main, final, old;
        CHECK(renderer.ReadTarget(1, 1, main) && renderer.ReadTarget(1, 5, final) &&
              main.bytes == final.bytes);
        if (n)
            CHECK(renderer.ReadTarget(1, read, old) && old.bytes == previous.bytes);
        history.Commit();
        CHECK(renderer.ReadTarget(1, history.ReadTarget(), previous));
        CompareCapture(previous, dx9.Copy(Image(final), mode));
    }
    CHECK(renderer.Unload(1) && renderer.Detach());
    std::cout << "real model + CPU screen accessory + self-shadow: " << model.filename().u8string()
              << "\n";
}
}
int main(int argc, char** argv) try {
    if (argc != 4 && argc != 5)
        return 2;
    const std::string api = argv[3];
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = api.find("debug") != std::string::npos;
    auto native = CreateOffscreenBackendDevice(96, 64, options);
    if (!native && options.debugLayer &&
        (native.reason == "validation_layer_unavailable" ||
         native.reason == "validation_extensions_unavailable"))
        return 77;
    CHECK(native);
    FileAssetSource assets(argv[2]);
    if (argc == 5)
        RealModel(std::filesystem::u8path(argv[4]), std::filesystem::u8path(argv[1]), assets,
                  *native.device);
    else {
        Pmm(std::filesystem::u8path(argv[1]), assets, *native.device);
        Pmm(std::filesystem::u8path(argv[1]), assets, *native.device, "bound-pmx.pmm");
    }
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan)
        CHECK(QueryVulkanStats(*native.device).validationErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (options.backend == Backend::D3D12)
        CHECK(QueryD3D12Stats(*native.device).debugErrors == 0);
#endif
    std::cout
        << api
        << " screen history, capture modes, native DX9 resize, CPU X/PMM, self-shadow, unload and replacement passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
}
