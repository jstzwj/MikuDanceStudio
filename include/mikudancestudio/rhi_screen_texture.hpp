#pragma once
#include "rhi_post_process.hpp"
#include "screen_capture.hpp"
namespace mikudancestudio::rhi {
struct ScreenTextureConsumer {
    std::size_t pass = 0, draw = 0;
};
// CPU history cursor; GPU resources belong exclusively to SceneRenderer.
// Apply reads the previous successful capture. Commit only after submission
// succeeds; rebuilding or repeating a prepared frame never advances history.
class ScreenTextureHistory {
public:
    static constexpr std::uint64_t FirstTarget = 0xffff000000000001ull;
    bool Apply(RenderFrame& frame, const std::vector<ScreenTextureConsumer>& consumers,
               render::ScreenCaptureKind mode, std::uint64_t finalColor,
               RenderTargetReference background = {}, unsigned captureSize = 1024) {
        if (unsigned(mode) > 3 || captureSize < 2 || captureSize > 4096)
            return false;
        auto next = frame;
        const auto source = std::find_if(next.targets.begin(), next.targets.end(),
                                         [&](const auto& t) { return t.id == finalColor; });
        if (source == next.targets.end())
            return false;
        const auto width = source->description.width, height = source->description.height;
        for (unsigned n = 0; n < 2; ++n) {
            for (const auto& t : next.targets)
                if (t.id == FirstTarget + n)
                    return false;
            next.targets.push_back({FirstTarget + n,
                                    {captureSize, captureSize, TextureFormat::Rgba8,
                                     TextureAccess::ColorAttachment | TextureAccess::Sampled |
                                         TextureAccess::CopySource},
                                    ClearColor{0, 0, 0, 1}});
        }
        RenderTargetReference sampled;
        if (mode == render::ScreenCaptureKind::BackgroundRefresh)
            sampled = background;
        else if (captured_)
            sampled = {FirstTarget + read_, captureSize, captureSize};
        for (const auto& consumer : consumers) {
            if (consumer.pass >= next.passes.size() ||
                consumer.draw >= next.passes[consumer.pass].draws.size())
                return false;
            auto& pass = next.passes[consumer.pass];
            auto& draw = pass.draws[consumer.draw];
            if (draw.pipeline.shader.layout.stages[0].role != MaterialTextureRole::None ||
                draw.textures[0].id || draw.targetTextures[0].target)
                return false;
            if (sampled.target) {
                auto& stage = draw.pipeline.shader.layout.stages[0];
                stage.role = MaterialTextureRole::Base;
                stage.coordinates = MaterialCoordinates::MeshUv0;
                stage.color = MaterialCombine::Modulate;
                stage.colorArg2 = MaterialColorSource::LitDiffuse;
                draw.pipeline.shader.textureTransforms[0] = {1, 0, 0, 0, 0, 1, 0, 0,
                                                             0, 0, 1, 0, 0, 0, 0, 1};
                draw.pipeline.shader.samplers[0].u = draw.pipeline.shader.samplers[0].v =
                    SamplerAddress::Wrap;
                draw.targetTextures[0] = sampled;
                if (std::find(pass.sampled.begin(), pass.sampled.end(), sampled.target) ==
                    pass.sampled.end())
                    pass.sampled.push_back(sampled.target);
            }
        }
        const bool capture = mode == render::ScreenCaptureKind::FullFrame ||
                             mode == render::ScreenCaptureKind::CropFourByThree;
        if (capture) {
            const auto rect = render::ScreenSourceRect({0, 0, int(width), int(height)}, mode);
            PostProcessState copy{PostProcessMode::LegacyScreenCapture};
            copy.linear = true;
            copy.uvRect = {float(rect.right - rect.left) / width,
                           float(rect.bottom - rect.top) / height, float(rect.left) / width,
                           float(rect.top) / height};
            if (!AppendPostProcess(next, finalColor, FirstTarget + (1 - read_), captureSize,
                                   captureSize, copy))
                return false;
        }
        next.finalColor = finalColor;
        if (!ValidateRenderFrame(next))
            return false;
        frame = std::move(next);
        pending_ = capture;
        return true;
    }
    void Commit() {
        if (pending_) {
            read_ = 1 - read_;
            captured_ = true;
        }
        pending_ = false;
    }
    void Reset() {
        read_ = 0;
        captured_ = pending_ = false;
    }
    std::uint64_t ReadTarget() const { return FirstTarget + read_; }

private:
    unsigned read_ = 0;
    bool captured_ = false, pending_ = false;
};
}
