#pragma once
#include "runtime_cpu_accessory.hpp"
#include "render_scene_frame.hpp"
#include "scene_resources.hpp"
#include "render_self_shadow.hpp"
#include "rhi_frame_passes.hpp"
namespace mikudancestudio {
// Scene-order traversal and CPU resources; no window, device or submission.
// Unsupported passes invalidate the complete frame before SceneRenderer sees it.
class RuntimeCpuSceneProducer {
public:
    bool Load(const RuntimeCpuScene&, format::SceneResourceResolver&, bool scenePasses,
              const rhi::AssetSource&, std::string& reason);
    bool Build(RuntimeCpuScene&, render::ModelFrameInputs, const rhi::HistoryDesc&,
               rhi::ModelReplayStream&, std::string& reason);
    bool BuildFrame(RuntimeCpuScene&, render::ModelFrameInputs, const rhi::HistoryDesc&,
                    const render::SelfShadowSettings&, rhi::RenderFrame&, std::string& reason);

private:
    bool BuildScene(RuntimeCpuScene&, render::ModelFrameInputs, const rhi::HistoryDesc&,
                    rhi::ModelReplayStream&, std::string&, const rhi::BuiltinShadowState*);
    bool scenePasses_ = false;
    std::map<std::uint8_t, std::unique_ptr<RuntimeCpuAccessory>> accessories_;
    std::map<std::uint8_t, std::array<char, 256>> accessoryPaths_;
    rhi::AtlasAsset helper_;
    render::CpuSceneFrameAssembler assembler_;
};
}
