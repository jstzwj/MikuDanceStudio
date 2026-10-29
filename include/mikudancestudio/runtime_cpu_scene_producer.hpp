#pragma once
#include "runtime_cpu_accessory.hpp"
#include "render_scene_frame.hpp"
#include "scene_resources.hpp"
#include "render_self_shadow.hpp"
#include "rhi_frame_passes.hpp"
#include "rhi_screen_texture.hpp"
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
    bool RequiresFrame() const { return hasScreenTextures_; }
    // Advance screen history only after the generated frame was submitted.
    void CommitFrame() { screenHistory_.Commit(); }

private:
    bool BuildScene(RuntimeCpuScene&, render::ModelFrameInputs, const rhi::HistoryDesc&,
                    rhi::ModelReplayStream&, std::string&, const rhi::BuiltinShadowState*);
    bool scenePasses_ = false;
    bool hasScreenTextures_ = false;
    rhi::ScreenTextureHistory screenHistory_;
    std::vector<std::size_t> screenDraws_;
    std::map<std::uint8_t, std::unique_ptr<RuntimeCpuAccessory>> accessories_;
    std::map<std::uint8_t, std::array<char, 256>> accessoryPaths_;
    rhi::AtlasAsset helper_;
    render::CpuSceneFrameAssembler assembler_;
};
}
