#include "mikudancestudio/model_physics_runtime.hpp"
#include "mikudancestudio/cpu_model_physics.hpp"
#include "mikudancestudio/cpu_physics_create.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace mikudancestudio::runtime {
namespace {
using namespace cpu_math;
struct Traits : cpu_model::Traits {
    using Rigid = cpu_model::Rigid;
    using Joint = cpu_model::Joint;
};
using Kernel = cpu_physics::Kernel<Traits>;
struct World {
    btDiscreteDynamicsWorld* world;
    int constraintId;
};
struct Result {
    int id = -1;
    btRigidBody* body = nullptr;
};
struct Ops {
    void QuatFromMatrix(float* q, const float* raw) {
        Matrix m;
        std::copy_n(raw, 16, m.begin());
        const auto value = QuaternionFromMatrix(m);
        std::copy(value.begin(), value.end(), q);
    }
    void SetRotation(float* raw, const float* q) {
        reinterpret_cast<btMatrix3x3*>(raw)->setRotation(btQuaternion(q[0], q[1], q[2], q[3]));
    }
    template <class... T> void DumpRigid(T&&...) {}
    template <class... T> void DumpConstraint(T&&...) {}
    template <class... T> void DumpJointFrames(T&&...) {}
};
using Creation = cpu_physics::Creation<World, Result, Ops>;
Matrix Rotation(const float* a) {
    return Multiply(Multiply(RotationZ(a[2]), RotationX(a[0])), RotationY(a[1]));
}
Matrix UndoRotation(Matrix m, const float* a) {
    m = Multiply(m, RotationY(-a[1]));
    m = Multiply(m, RotationX(-a[0]));
    return Multiply(m, RotationZ(-a[2]));
}
}
ModelPhysics::~ModelPhysics() {
    if (!world_)
        return;
    for (auto i = constraints_.rbegin(); i != constraints_.rend(); ++i) {
        world_->removeConstraint(*i);
        delete *i;
    }
    for (auto i = rigids_.rbegin(); i != rigids_.rend(); ++i)
        if (i->body) {
            auto* body = i->body;
            world_->removeRigidBody(body);
            delete body->getMotionState();
            delete body->getCollisionShape();
            delete body;
        }
}
bool ModelPhysics::Create(std::shared_ptr<const ModelResource> resource,
                          btDiscreteDynamicsWorld* world, int& id, std::string& reason) {
    if (world_ || !world || !resource) {
        reason = "invalid-physics-owner";
        return false;
    }
    world_ = world;
    resource_ = std::move(resource);
    const auto& model = resource_->Document();
    rigids_.resize(model.rigids.size());
    backup_.resize(model.bones.size());
    for (auto& backup : backup_)
        backup[6] = 1;
    bool operationCenter = false;
    for (std::size_t n = 0; n < resource_->BoneNames().size(); ++n) {
        if (resource_->BoneNames()[n] == u8"\u64cd\u4f5c\u4e2d\u5fc3") {
            centerBone_ = int(n);
            operationCenter = true;
        } else if (!operationCenter && resource_->BoneNames()[n] == u8"\u30bb\u30f3\u30bf\u30fc")
            centerBone_ = int(n);
    }
    World target{world, id};
    for (std::size_t n = 0; n < rigids_.size(); ++n) {
        const auto& source = model.rigids[n].fields;
        auto& rigid = rigids_[n];
        rigid.boneIndex = source.boneIndex;
        rigid.mode = source.mode;
        rigid.staticFlag = source.mode == 0;
        rigid.kinematicFlag = source.mode == 2;
        const auto& bone = model.bones[source.boneIndex < 0 ? 0 : source.boneIndex].fields;
        std::copy_n(source.rotation, 3, rigid.rotation);
        std::copy_n(source.position, 3, rigid.position);
        if (resource_->SourceKind() == format::ModelKind::Pmx)
            for (int c = 0; c < 3; ++c)
                rigid.position[c] -= bone.position[c];
        auto transform =
            Multiply(Rotation(rigid.rotation),
                     Translation({rigid.position[0], rigid.position[1], rigid.position[2]}));
        transform = Multiply(transform,
                             Translation({bone.position[0], bone.position[1], bone.position[2]}));
        Result out;
        Creation::CreateRigidBody(&target, out, source.shape, source.size[0], source.size[1],
                                  source.size[2], transform.data(), source.mode, source.mass,
                                  source.linearDamping, source.angularDamping, source.restitution,
                                  source.friction, char(source.group), source.noCollapse);
        rigid.body = out.body;
        const auto inverse = UndoRotation(
            Multiply(Translation({-bone.position[0], -bone.position[1], -bone.position[2]}),
                     Translation({-rigid.position[0], -rigid.position[1], -rigid.position[2]})),
            rigid.rotation);
        std::copy(inverse.begin(), inverse.end(), rigid.invTransform);
    }
    for (const auto& source : model.joints) {
        const auto& j = source.fields;
        const auto& a = rigids_[j.rigidA];
        const auto& b = rigids_[j.rigidB];
        Matrix ma, mb;
        std::copy_n(a.invTransform, 16, ma.begin());
        std::copy_n(b.invTransform, 16, mb.begin());
        const Vec3 point{j.position[0], j.position[1], j.position[2]};
        const auto pa = TransformPoint(point, ma), pb = TransformPoint(point, mb);
        const float da = std::sqrt(pa[0] * pa[0] + pa[1] * pa[1] + pa[2] * pa[2]);
        const float db = std::sqrt(pb[0] * pb[0] + pb[1] * pb[1] + pb[2] * pb[2]);
        float maximum[3];
        for (int c = 0; c < 3; ++c)
            maximum[c] = std::max(std::abs(j.limits[c]), std::abs(j.limits[c + 3]));
        joints_.push_back({j.rigidA, j.rigidB,
                           da + db +
                               std::sqrt(maximum[0] * maximum[0] + maximum[1] * maximum[1] +
                                         maximum[2] * maximum[2])});
        const auto qa = QuaternionFromMatrix(UndoRotation(Rotation(j.rotation), a.rotation));
        const auto qb = QuaternionFromMatrix(UndoRotation(Rotation(j.rotation), b.rotation));
        Creation::CreatePhysJoint(&target, a.body, b.body, pa[0], pa[1], pa[2], qa[0], qa[1], qa[2],
                                  qa[3], pb[0], pb[1], pb[2], qb[0], qb[1], qb[2], qb[3],
                                  j.limits[0], j.limits[1], j.limits[2], j.limits[3], j.limits[4],
                                  j.limits[5], j.limits[6], j.limits[7], j.limits[8], j.limits[9],
                                  j.limits[10], j.limits[11], j.springs[0], j.springs[1],
                                  j.springs[2], j.springs[3], j.springs[4], j.springs[5]);
        constraints_.push_back(world->getConstraint(world->getNumConstraints() - 1));
    }
    id = target.constraintId;
    reason.clear();
    return true;
}
void ModelPhysics::Attach(cpu_model::Model& view) {
    view.rigidTable = rigids_.data();
    view.rigidCount = int(rigids_.size());
    view.jointTable = joints_.data();
    view.jointCount = int(joints_.size());
    view.centerBone = centerBone_;
}
void ModelPhysics::Restore(ModelEvaluation& evaluation) {
    auto& bones = evaluation.Bones();
    for (std::size_t n = 0; n < bones.size(); ++n)
        std::copy(backup_[n].begin(), backup_[n].end(), bones[n].ikBackup);
    for (const auto& rigid : rigids_)
        if (!rigid.staticFlag && rigid.boneIndex >= 0)
            bones[rigid.boneIndex].hasRigidBody = true;
}
void ModelPhysics::Execute(ModelEvaluation& evaluation, CpuPhysicsModelPhase phase) {
    auto view = evaluation.View();
    view.rigidTable = rigids_.data();
    view.rigidCount = int(rigids_.size());
    view.jointTable = joints_.data();
    view.jointCount = int(joints_.size());
    view.centerBone = centerBone_;
    switch (phase) {
    case CpuPhysicsModelPhase::KinematicSync:
        Kernel::ModelKinematicSync(&view);
        break;
    case CpuPhysicsModelPhase::DynamicReseat:
        Kernel::ModelDynamicReseat(&view);
        break;
    case CpuPhysicsModelPhase::Readback:
        Kernel::ModelPhysicsReadback(&view);
        for (std::size_t n = 0; n < backup_.size(); ++n)
            std::copy_n(view.boneTable[n].ikBackup, 7, backup_[n].begin());
        break;
    }
}
std::vector<std::array<float, 38>> ModelPhysics::Snapshot() const {
    std::vector<std::array<float, 38>> out;
    for (const auto& rigid : rigids_) {
        std::array<float, 38> body{};
        Kernel::TransformToMatrix(body.data(), rigid.body->getWorldTransform());
        Kernel::TransformToMatrix(body.data() + 16, rigid.body->getInterpolationWorldTransform());
        for (int c = 0; c < 3; ++c) {
            body[32 + c] = rigid.body->getLinearVelocity()[c];
            body[35 + c] = rigid.body->getAngularVelocity()[c];
        }
        out.push_back(body);
    }
    return out;
}
}
