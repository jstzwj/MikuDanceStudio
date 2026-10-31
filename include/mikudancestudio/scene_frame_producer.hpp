#pragma once
#include "scene_runtime.hpp"
#include "rhi_frame_passes.hpp"
#include "render_self_shadow.hpp"
#include <filesystem>
namespace mikudancestudio::render {
struct SceneRenderSources {
    std::map<std::uint8_t, std::filesystem::path> models, accessories;
    std::filesystem::path sharedToons;
};
struct SceneRenderSettings {
    unsigned width = 640, height = 480;
    SelfShadowSettings shadow;
    bool postProcessCopy = true;
};
// Consumes an already published P2 scene snapshot. Never advances animation or
// physics and never borrows native GPU objects. GPU caches survive resize;
// changed CPU bytes retain resource IDs and receive increasing versions.
class SceneFrameProducer final {
public:
    SceneFrameProducer();
    ~SceneFrameProducer();
    bool Load(const runtime::SceneRuntime&, const SceneRenderSources&, const rhi::AssetSource&,
              std::string& reason);
    bool Build(const runtime::SceneRuntime&, const SceneRenderSettings&, rhi::RenderFrame&,
               std::string& reason);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint64_t nextId_ = 0x100000000ull;
};
}
