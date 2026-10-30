#include "mikudancestudio/rhi_shared_targets.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include <cstdio>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
using namespace mikudancestudio::rhi;
int main() {
    RenderFrame frame;
    SharedFrameTargets registry(frame);
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    RenderTextureDesc desc{16, 16, TextureFormat::Rgba8, access};
    SharedTargetLease producer, consumer, other;
    std::string reason;
    CHECK(registry.Declare(1, desc, producer, reason));
    CHECK(registry.Declare(1, desc, consumer, reason) && frame.targets.size() == 1);
    CHECK(registry.Declare(2, desc, other, reason) && frame.targets.size() == 2);
    auto wrong = desc;
    wrong.width = 32;
    SharedTargetLease invalid;
    CHECK(!registry.Declare(1, wrong, invalid, reason));
    CHECK(reason == "conflicting_shared_target_declaration");
    wrong = desc;
    wrong.mips = 2;
    CHECK(!registry.Declare(1, wrong, invalid, reason));
    CHECK(reason == "invalid_shared_target_declaration");
    wrong = desc;
    wrong.layers = 2;
    CHECK(!registry.Declare(3, wrong, invalid, reason));
    RenderTargetReference reference;
    CHECK(registry.Resolve(consumer, reference, reason));
    frame.targets.front().history = ClearColor{};
    CHECK(!registry.Resolve(consumer, reference, reason));
    frame.targets.front().history.reset();
    CHECK(registry.Resolve(consumer, reference, reason));
    const auto shared = reference.target, output = SharedFrameTargetId(2);
    FramePass write;
    write.color = FrameAttachment{shared};
    FramePass read;
    read.color = FrameAttachment{output};
    read.sampled = {shared};
    frame.passes = {write, read};
    frame.finalColor = output;
    CHECK(ValidateRenderFrame(frame));
    auto native = CreateNullDevice();
    const auto source = native->CreateRenderTexture(desc),
               destination = native->CreateRenderTexture(desc);
    CHECK(source && destination && native->BeginFrame({}));
    RenderPassDesc nativeWrite;
    nativeWrite.color = PassAttachment{source};
    RenderPassDesc nativeRead;
    nativeRead.color = PassAttachment{destination};
    nativeRead.sampled = {source};
    CHECK(!native->BeginRenderPass(nativeRead));
    CHECK(native->BeginRenderPass(nativeWrite) && native->EndRenderPass());
    CHECK(native->BeginRenderPass(nativeRead) && native->EndRenderPass());
    CHECK(native->EndFrame() && native->Present());
    auto bad = frame;
    bad.passes.erase(bad.passes.begin());
    CHECK(!ValidateRenderFrame(bad)); // no prior-frame contents
    bad = frame;
    bad.passes[0].sampled = {shared};
    CHECK(!ValidateRenderFrame(bad)); // alias feedback
    registry.Reset();
    CHECK(!registry.Resolve(consumer, reference, reason));
    CHECK(reason == "stale_shared_target_reference");
    CHECK(registry.Declare(1, desc, consumer, reason));
    CHECK(registry.Resolve(consumer, reference, reason));
    CHECK(native->Destroy(source));
    CHECK(native->BeginFrame({}));
    CHECK(!native->BeginRenderPass(nativeRead));
    CHECK(native->EndFrame() && native->Present());
    CHECK(native->Destroy(destination));
    std::puts(
        "PASS shared identity, conflicts, feedback, initialization, stale leases and native handles");
    return 0;
}
