#pragma once
#include "mikudancestudio/model_runtime.hpp"
#include "mikudancestudio/scene_session.hpp"
#include "mikudancestudio/cpu_scene_simulation.hpp"
#include "mikudancestudio/accessory_resource.hpp"
#include <map>
namespace mikudancestudio::runtime {
struct SceneCounters {
    std::uint64_t frames = 0, preparedModels = 0, publishedModels = 0;
};
class SceneRuntime final {
public:
    SceneRuntime();
    ~SceneRuntime();
    SceneRuntime(const SceneRuntime&) = delete;
    SceneRuntime& operator=(const SceneRuntime&) = delete;
    bool Load(format::SceneSession, const std::map<std::uint8_t, format::ModelDocument>&,
              std::string&);
    bool LoadModel(const format::ModelDocument&, std::string&);
    bool BindMotion(std::uint8_t, std::shared_ptr<const MotionClip>, std::string&);
    bool BindGlobalMotion(const format::VmdDocument&, std::string&);
    bool Seek(std::uint32_t, std::string&);
    bool SetPhysicsMode(unsigned mode, std::string& reason);
    bool Reset(std::string&);
    bool StartPlayback(std::string&);
    void Pause();
    bool Advance(float absoluteSeconds, float deltaSeconds, std::string&);
    bool Tick(float deltaSeconds, std::string&);
    bool Edit(const std::function<void(format::PmmSceneDocument&)>&, std::string&);
    bool UnloadModel(std::uint8_t, std::string&);
    bool AttachAccessory(std::uint8_t, std::shared_ptr<const AccessoryResource>, std::string&);
    bool UnloadAccessory(std::uint8_t, std::string&);
    std::shared_ptr<const AccessoryResource> Accessory(std::uint8_t) const;
    const ModelSnapshot* Snapshot(std::uint8_t) const;
    std::shared_ptr<const ModelResource> Resource(std::uint8_t) const;
    std::vector<std::array<float, 38>> Bodies(std::uint8_t) const;
    const format::SceneSession* Session() const;
    const CpuSimulationState& Simulation() const;
    const SceneCounters& Counters() const;
    bool BoneAttachment(int model, int bone, cpu_math::Matrix&, std::string&) const;
    bool AccessoryPlacement(std::uint8_t, cpu_math::Matrix&, std::string&) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
