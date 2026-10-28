#pragma once
#include "runtime_cpu_accessory.hpp"
#include "render_scene_frame.hpp"
#include "scene_resources.hpp"
namespace mikudancestudio {
// Scene-order traversal and CPU resources; no window, device or submission.
// Unsupported passes invalidate the complete frame before SceneRenderer sees it.
class RuntimeCpuSceneProducer {
public:
    bool Load(const RuntimeCpuScene&, format::SceneResourceResolver&, bool scenePasses,
              const rhi::AssetSource&, std::string& reason);
    bool Build(RuntimeCpuScene&, render::ModelFrameInputs, const rhi::HistoryDesc&,
               rhi::ModelReplayStream&, std::string& reason);

private:
    bool scenePasses_ = false;
    std::map<std::uint8_t, std::unique_ptr<RuntimeCpuAccessory>> accessories_;
    std::map<std::uint8_t, std::array<char, 256>> accessoryPaths_;
    rhi::AtlasAsset helper_;
    render::CpuSceneFrameAssembler assembler_;
};
}
