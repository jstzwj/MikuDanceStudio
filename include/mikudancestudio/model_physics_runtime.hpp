#pragma once
#include "mikudancestudio/model_runtime.hpp"
#include "mikudancestudio/cpu_pose_schedule.hpp"
class btDiscreteDynamicsWorld;
class btTypedConstraint;
namespace mikudancestudio::runtime {
// The scene owns the world; this owner removes its constraints before bodies.
class ModelPhysics final {
public:
    ~ModelPhysics();
    ModelPhysics() = default;
    ModelPhysics(const ModelPhysics&) = delete;
    ModelPhysics& operator=(const ModelPhysics&) = delete;
    bool Create(std::shared_ptr<const ModelResource>, btDiscreteDynamicsWorld*, int& constraintId,
                std::string&);
    void Restore(ModelEvaluation&);
    void Attach(cpu_model::Model&);
    void Execute(ModelEvaluation&, CpuPhysicsModelPhase);
    std::vector<std::array<float, 38>> Snapshot() const;

private:
    btDiscreteDynamicsWorld* world_ = nullptr;
    std::shared_ptr<const ModelResource> resource_;
    std::vector<cpu_model::Rigid> rigids_;
    std::vector<cpu_model::Joint> joints_;
    std::vector<btTypedConstraint*> constraints_;
    std::vector<std::array<float, 7>> backup_;
    int centerBone_ = 0;
};
}
