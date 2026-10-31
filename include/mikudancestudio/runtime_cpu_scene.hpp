#pragma once
#include "mikudancestudio/runtime_cpu_model.hpp"
#include "mikudancestudio/scene_session.hpp"
#include "mikudancestudio/cpu_scene_simulation.hpp"
#include <map>

namespace mikudancestudio {
struct CpuSolvedBone {
    render::ModelMatrix skin{}, local{}, world{}, extra{};
    std::array<float, 7> pose{}, backup{}, working{};
    std::uint32_t cursor = 0;
    std::int32_t armedFrame = 0;
    std::uint8_t active = 0, physicsDisabled = 0;
};
struct CpuSolvedModel {
    std::uint8_t slot = 0, visible = 0, displayActive = 0;
    std::uint32_t displayCursor = 0;
    std::vector<CpuSolvedBone> bones;
    std::vector<float> morphs;
    std::vector<std::uint8_t> ikEnabled;
    // Transform, interpolation transform, linear/angular velocities, in model order.
    std::vector<std::array<float, 38>> bodies;
};
// Compatibility host for the existing CPU model solver. Owns a single world,
// slot table, resource namespace and playback clocks. Render devices consume
// solved geometry; replacing a device never resets or advances this world.
class RuntimeCpuScene {
public:
    RuntimeCpuScene();
    ~RuntimeCpuScene();
    RuntimeCpuScene(const RuntimeCpuScene&) = delete;
    RuntimeCpuScene& operator=(const RuntimeCpuScene&) = delete;
    bool Load(format::SceneSession, const std::map<std::uint8_t, std::filesystem::path>&,
              std::string& reason, int selectedSlot = -1);
    bool LoadModel(const std::filesystem::path&, std::string& reason);
    bool Seek(std::uint32_t frame, std::string& reason);
    bool SetPhysicsMode(unsigned mode,std::string& reason);
    bool StartPlayback(std::string& reason);
    void Pause();
    bool Advance(float absoluteSeconds, float deltaSeconds, std::string& reason);
    bool Tick(float deltaSeconds, std::string& reason);
    bool Edit(const std::function<void(format::PmmSceneDocument&)>&, std::string& reason);
    const format::SceneSession* Session() const;
    RuntimeCpuModel* Model(std::uint8_t slot);
    bool Visible(std::uint8_t slot) const;
    const std::vector<std::uint8_t>& Order() const;
    const CpuSimulationState& Simulation() const;
    bool Snapshot(std::uint8_t, CpuSolvedModel&) const;
    // Row-vector attachment transform T(rest position) * final skin matrix.
    // Available after IK/physics, including models with no draw geometry.
    bool BoneAttachment(std::int32_t model, std::int32_t bone, render::ModelMatrix&,
                        std::string& reason) const;
    bool AccessoryPlacement(std::uint8_t, render::ModelMatrix&, std::string& reason) const;
    bool FrameInputs(render::ModelFrameInputs&, std::string& reason) const;
    bool PrepareEdges(const render::ModelFrameInputs&, std::string& reason);
    std::size_t BodyCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
