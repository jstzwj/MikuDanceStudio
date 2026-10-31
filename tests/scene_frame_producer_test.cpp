#include "mikudancestudio/scene_frame_producer.hpp"
#include <fstream>
#include <cstdio>
#include <stdexcept>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + ": " + reason);                             \
    } while (0)
int Read(void* p, void* out, std::size_t n) {
    auto& file = *static_cast<std::ifstream*>(p);
    file.read(static_cast<char*>(out), n);
    return int(file.gcount());
}
std::map<std::uint64_t, std::uint64_t> Keys(const rhi::RenderFrame& frame) {
    std::map<std::uint64_t, std::uint64_t> result;
    for (const auto& r : frame.resources)
        result.emplace(r.key.id, r.key.version);
    return result;
}
int main(int argc, char** argv) try {
    CHECK(argc == 3);
    const auto root = std::filesystem::path(argv[1]);
    format::ModelDocument document;
    std::ifstream file(root / "triangle.pmx", std::ios::binary);
    CHECK(format::ReadModelDocument(Read, &file, document));
    runtime::SceneRuntime scene;
    CHECK(scene.LoadModel(document, reason));
    render::SceneRenderSources sources;
    sources.models.emplace(0, root / "triangle.pmx");
    rhi::FileAssetSource assets(argv[2]);
    render::SceneFrameProducer producer;
    CHECK(producer.Load(scene, sources, assets, reason));
    render::SceneRenderSettings settings;
    settings.width = 63;
    settings.height = 47;
    settings.shadow.mode = 1;
    settings.shadow.mapSize = 32;
    rhi::RenderFrame first, second;
    const auto updates = scene.Counters().frames;
    CHECK(producer.Build(scene, settings, first, reason) &&
          producer.Build(scene, settings, second, reason));
    CHECK(scene.Counters().frames == updates && Keys(first) == Keys(second));
    CHECK(rhi::ValidateRenderFrame(first));
    settings.width = 0;
    second.finalColor = 123;
    CHECK(!producer.Build(scene, settings, second, reason) && second.finalColor == 123 &&
          scene.Counters().frames == updates);
    settings.width = 63;
    format::VmdDocument motion;
    format::VmdBoneKey key;
    key.name[0] = 'B';
    key.frame = 10;
    key.position[0] = 4;
    key.rotation[3] = 1;
    motion.bones.push_back(key);
    std::shared_ptr<const runtime::MotionClip> clip;
    CHECK(runtime::MotionClip::Create(scene.Resource(0), motion, clip, reason));
    CHECK(scene.BindMotion(0, clip, reason) && scene.Seek(10, reason) &&
          producer.Build(scene, settings, second, reason));
    const auto oldKeys = Keys(first), newKeys = Keys(second);
    CHECK(oldKeys.size() == newKeys.size());
    bool changed = false;
    for (const auto& [id, version] : oldKeys) {
        CHECK(newKeys.count(id) && newKeys.at(id) >= version);
        changed = changed || newKeys.at(id) > version;
    }
    CHECK(changed);
    CHECK(rhi::ValidateRenderFrame(second));
    // A resource replacement cannot silently reuse a prior scene's geometry.
    CHECK(scene.LoadModel(document, reason));
    second.finalColor = 123;
    CHECK(!producer.Build(scene, settings, second, reason) && second.finalColor == 123 &&
          reason == "model_resource_changed_reload_required");
    auto invalid = sources;
    invalid.models.clear();
    CHECK(!producer.Load(scene, invalid, assets, reason));
    CHECK(producer.Load(scene, sources, assets, reason) &&
          producer.Build(scene, settings, second, reason));
    for (const auto& [id, version] : Keys(second))
        if (id != UINT64_MAX)
            CHECK(!oldKeys.count(id));
    CHECK(rhi::ValidateRenderFrame(second));
    CHECK(scene.UnloadModel(0, reason) && producer.Build(scene, settings, second, reason));
    CHECK(rhi::ValidateRenderFrame(second));
    std::puts(
        "P2 snapshot -> shared RHI: stable versions, deformation, atomic failure, replacement and unload passed");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
