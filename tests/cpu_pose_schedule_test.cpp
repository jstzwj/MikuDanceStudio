#include "mikudancestudio/cpu_pose_schedule.hpp"
#include "mikudancestudio/model.hpp"
#include "btBulletDynamicsCommon.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace mikudancestudio;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
namespace {
void Identity(float* matrix) { std::fill_n(matrix,16,0.f); for (unsigned n=0;n<4;++n) matrix[n*5]=1; }
struct Model {
    std::unique_ptr<mdl::ModelRecord> record=std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord,4> bones{};
    std::array<mdl::BoneOrderEntry,1> selectors{};
    mdl::RigidRecord rigid{};
    Model() {
        record->boneCount=1; record->boneTable=bones.data(); record->boneOrderCount=1;
        record->boneOrderTable=selectors.data(); selectors[0].linkedModel=-1;
        for (auto& bone:bones) {
            bone.parent=-1; bone.slotIndex=-1; bone.type=mdl::BoneType::RotateMove;
            bone.rotQuat[3]=bone.rotQuat2[3]=bone.physicsQuat[3]=bone.ikBackup[6]=1;
            Identity(bone.matInit); Identity(bone.matLocal); Identity(bone.matWorld); Identity(bone.matExtra);
        }
    }
    mdl::ModelRecord* Raw() { return record.get(); }
};
struct MotionState : btDefaultMotionState {
    std::vector<float> writes;
    void setWorldTransform(const btTransform& transform) override {
        writes.push_back(transform.getOrigin().x()); btDefaultMotionState::setWorldTransform(transform);
    }
};
}
int main() {
    // Highest slot, distinct display orders and tied orders must work without
    // MMDApp, a native window, or any GPU bootstrap. External parents make a
    // changed evaluation order observable in the resulting matrices.
    std::array<mdl::ModelRecord*,255> slots{};
    Model source, attached, chained;
    slots[254]=source.Raw(); slots[0]=attached.Raw(); slots[1]=chained.Raw();
    source.record->comboSelIndex2=0; source.bones[0].trans[0]=3;
    attached.record->comboSelIndex2=chained.record->comboSelIndex2=1;
    attached.bones[0].trans[0]=2; attached.selectors[0].linkedModel=254;
    chained.bones[0].trans[0]=4; chained.selectors[0].linkedModel=0;
    attached.record->boneCount=2; attached.record->maxBoneLayer=1;
    attached.bones[1].parent=0; attached.bones[1].layer=1;
    attached.bones[1].flags=mdl::kBoneFlagAfterPhysics; attached.bones[1].trans[1]=7;
    const CpuModelSlots view{slots.data(),slots.size()};
    RunCpuModelPosePass(view,CpuPosePhase::BeforePhysics,0);
    CHECK(source.bones[0].matInit[12]==3 && attached.bones[0].matInit[12]==5 && chained.bones[0].matInit[12]==9);
    CHECK(attached.bones[1].matInit[13]==0);
    RunCpuModelPosePass(view,CpuPosePhase::AfterPhysics,0);
    CHECK(attached.bones[1].matInit[12]==5 && attached.bones[1].matInit[13]==7);
    source.bones[0].trans[0]=11;
    RunCpuModelPosePass(view,CpuPosePhase::BeforePhysics,0,254);
    CHECK(source.bones[0].matInit[12]==3 && attached.bones[0].matInit[12]==5);
    // Physics pose selection must keep the previous external-parent mirror,
    // and the per-bone disable switch must retain the original mode semantics.
    source.bones[0].hasRigidBody=1; source.bones[0].ikBackup[0]=9;
    RunCpuModelPosePass(view,CpuPosePhase::BeforePhysics,1);
    CHECK(source.bones[0].matInit[12]==9 && source.bones[0].matExtra[12]==3);
    source.bones[0].physicsDisabled=1;
    RunCpuModelPosePass(view,CpuPosePhase::BeforePhysics,2);
    CHECK(source.bones[0].matInit[12]==11 && source.bones[0].matExtra[12]==11);
    source.bones[0].physicsDisabled=0;
    RunCpuModelPosePass(view,CpuPosePhase::BeforePhysics,3);
    CHECK(source.bones[0].matInit[12]==9 && source.bones[0].matExtra[12]==11);

    Model pmx; pmx.record->physicsMode=2; pmx.bones[0].trans[0]=3;
    mdl::BoneMorphOffsetRecord work{}; mdl::PmxBoneMorphEntry entry{}; mdl::MorphRecord morph{};
    mdl::MaterialMorphPool unused{};
    entry.translation[0]=4; entry.rotation[3]=1; morph.type=2; morph.value=.5f;
    morph.boneCount=1; morph.boneEntries=&entry;
    pmx.record->morphCount=1; pmx.record->morphs=&morph;
    (pmx.Raw())->boneMorphCount=1; (pmx.Raw())->boneMorphTable=&work;
    pmx.Raw()->materialMorphPools.additive=pmx.Raw()->materialMorphPools.multiplicative=&unused;
    std::array<mdl::ModelRecord*,1> one{pmx.Raw()};
    RunCpuModelPosePass({one.data(),1},CpuPosePhase::BeforePhysics,0);
    CHECK(pmx.bones[0].matInit[12]==5);
    RunCpuModelPosePass({one.data(),1},CpuPosePhase::BeforePhysics,0);
    CHECK(pmx.bones[0].matInit[12]==5); // restore, not accumulating the prior morph

    Model ik; ik.record->boneCount=4;
    ik.bones[1].parent=0; ik.bones[1].type=mdl::BoneType::UnderIk;
    ik.bones[2].parent=1; ik.bones[2].type=mdl::BoneType::Effector;
    ik.bones[2].position[1]=1; ik.bones[3].position[0]=1;
    std::uint16_t link=1; mdl::IkChain chain{};
    chain.boneIndex=3; chain.targetBone=2; chain.linkCount=1; chain.links=&link;
    chain.enabled=1; chain.iterations=40; chain.maxAngle=.5f;
    ik.record->ikChainCount=1; ik.record->ikChains=&chain; one[0]=ik.Raw();
    RunCpuModelPosePass({one.data(),1},CpuPosePhase::BeforePhysics,0);
    const auto* end=ik.bones[2].matInit;
    CHECK(std::fabs(end[4]+end[12]-1)<.001f && std::fabs(end[5]+end[13])<.001f);
    chain.enabled=0;
    RunCpuModelPosePass({one.data(),1},CpuPosePhase::BeforePhysics,0);
    CHECK(ik.bones[2].matInit[4]==0 && ik.bones[2].matInit[5]==1);

    // Share a motion state only in this test to observe forward/reverse writes.
    // Real model bodies are distinct. This tests the actual Bullet operations.
    MotionState motion; btSphereShape shape(1);
    btRigidBody::btRigidBodyConstructionInfo info(1,&motion,&shape); btRigidBody body(info);
    for (auto* model:{&source,&attached}) {
        model->record->rigidCount=1; model->record->rigidTable=&model->rigid;
        model->rigid.body=&body; model->rigid.boneIndex=0; Identity(model->rigid.invTransform);
    }
    RunCpuPhysicsModelPass(view,CpuPhysicsModelPhase::KinematicSync);
    CHECK(motion.writes==std::vector<float>({13,9}));
    motion.writes.clear();
    RunCpuPhysicsModelPass(view,CpuPhysicsModelPhase::KinematicSync,CpuSlotOrder::Reverse);
    CHECK(motion.writes==std::vector<float>({9,13}));
    motion.writes.clear();
    RunCpuPhysicsModelPass(view,CpuPhysicsModelPhase::KinematicSync,CpuSlotOrder::Forward,254);
    CHECK(motion.writes==std::vector<float>({13}));
    source.rigid.mode=1; body.setLinearVelocity(btVector3(1,2,3));
    RunCpuPhysicsModelPass(view,CpuPhysicsModelPhase::DynamicReseat);
    CHECK(body.getWorldTransform().getOrigin().x()==9 && body.getLinearVelocity()==btVector3(0,0,0));
    btTransform moved; moved.setIdentity(); moved.setOrigin(btVector3(6,2,0)); body.setWorldTransform(moved);
    RunCpuPhysicsModelPass(view,CpuPhysicsModelPhase::Readback);
    CHECK(source.bones[0].matInit[12]==6 && source.bones[0].ikBackup[0]==6 && source.bones[0].ikBackup[1]==2);
    RunCpuModelPosePass({nullptr,0},CpuPosePhase::BeforePhysics,0);
    RunCpuPhysicsModelPass({nullptr,0},CpuPhysicsModelPhase::Readback,CpuSlotOrder::Reverse);
    std::puts("PASS CPU pose schedule: display/slot order, external parent, pre/post layers, selected skip, physics sources, PMX bone morph, native CCD IK, Bullet sync/reseat/readback; no GPU");
}
