#include "mikudancestudio/model.hpp"
#include "cpu_simulation_reference.hpp"
#include "../src/app/cpu_simulation_bridge.hpp"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <memory>

using namespace mikudancestudio;
namespace mikudancestudio {
void ModelApplyMorphs(mdl::ModelRecord*);
void SetPhysicsMode(mdl::ModelRecord*,int,mdl::ModelRecord* const*,int);
void ModelKinematicSync(mdl::ModelRecord*);
void ModelDynamicReseat(mdl::ModelRecord*);
void ModelPhysicsReadback(mdl::ModelRecord*);
}
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1; } } while(0)
namespace {
void Identity(float* matrix) { std::fill_n(matrix,16,0.f);for(int n=0;n<4;++n) matrix[n*5]=1; }
struct Model {
    std::unique_ptr<mdl::ModelRecord> record=std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord,4> bones{};
    mdl::BoneOrderEntry selector{};
    mdl::RigidRecord rigid{};
    mdl::IkChain ik{};std::array<std::uint16_t,1> links{1};
    mdl::PmxBoneMorphEntry morphEntry{};
    mdl::MorphRecord morph{};
    mdl::MaterialMorphPool unused{};
    mdl::ModelRecord* Raw() { return record.get(); }
    Model() {
        record->boneCount=4;record->boneTable=bones.data();record->boneOrderCount=1;
        record->boneOrderTable=&selector;selector.linkedModel=-1;
        record->rigidCount=1;record->rigidTable=&rigid;rigid.boneIndex=0;Identity(rigid.invTransform);
        for(auto& b:bones) {
            b.parent=-1;b.slotIndex=-1;b.type=mdl::BoneType::RotateMove;
            b.rotQuat[3]=b.rotQuat2[3]=b.physicsQuat[3]=b.ikBackup[6]=1;
            Identity(b.matInit);Identity(b.matLocal);Identity(b.matWorld);Identity(b.matExtra);
        }
        bones[1].parent=0;bones[1].type=mdl::BoneType::UnderIk;
        bones[2].parent=1;bones[2].type=mdl::BoneType::Effector;bones[2].position[1]=1;
        bones[3].type=mdl::BoneType::Ik;bones[3].position[0]=1;
        ik.boneIndex=3;ik.targetBone=2;ik.linkCount=1;ik.links=links.data();ik.iterations=4;ik.maxAngle=.5f;ik.enabled=1;
        record->ikChainCount=1;record->ikChains=&ik;
    }
    void Pmx() {
        record->physicsMode=2;record->morphCount=1;record->morphs=&morph;
        morph.type=2;morph.boneCount=1;morph.boneEntries=&morphEntry;
        morphEntry.boneIndex=3;morphEntry.translation[1]=.25f;morphEntry.rotation[3]=1;
        (Raw())->boneMorphCount=4;
        // This fixture only morphs one bone, but the restoration workspace is
        // indexed by all bones. Supply it separately below.
        Raw()->materialMorphPools.additive=Raw()->materialMorphPools.multiplicative=&unused;
    }
};
struct World {
    btDefaultCollisionConfiguration config;btCollisionDispatcher dispatcher{&config};
    btDbvtBroadphase broadphase;btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world{&dispatcher,&broadphase,&solver,&config};
    btSphereShape shape{.2f};btDefaultMotionState motion0,motion1;
    btRigidBody body0{{0,&motion0,&shape}},body1{{1,&motion1,&shape,btVector3(.016f,.016f,.016f)}};
    Model first,second;
    std::array<mdl::BoneMorphOffsetRecord,4> morph0{},morph1{};
    std::array<mdl::ModelRecord*,255> slots{};
    explicit World(bool pmx) {
        slots[0]=second.Raw();slots[254]=first.Raw();
        first.record->comboSelIndex2=0;second.record->comboSelIndex2=1;
        second.selector.linkedModel=254;
        first.rigid.body=&body0;second.rigid.body=&body1;second.rigid.mode=1;
        second.bones[0].hasRigidBody=1;
        body0.setCollisionFlags(body0.getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT);
        body0.setActivationState(DISABLE_DEACTIVATION);
        body1.setLinearVelocity(btVector3(.5f,-1,.3f));body1.setAngularVelocity(btVector3(.1f,.2f,.3f));
        world.addRigidBody(&body0);world.addRigidBody(&body1);world.getSolverInfo().m_solverMode&=~1;
        if(pmx) { first.Pmx();second.Pmx();(first.Raw())->boneMorphTable=morph0.data();(second.Raw())->boneMorphTable=morph1.data();
            for(int n=0;n<4;++n) morph0[n].boneIndex=morph1[n].boneIndex=n; }
    }
    ~World() { world.removeRigidBody(&body1);world.removeRigidBody(&body0); }
    CpuSimulationContext Context(bool reference=false) {
        auto ops=LegacyCpuSimulationOps();
        if(reference) {
            ops.pose=[](CpuModelSlots slots,CpuPosePhase phase,int mode,int skip) {
                // Original nested loops, independent of cpu_pose_schedule.cpp.
                for(std::size_t order=0;order<slots.count;++order)
                    for(std::size_t slot=0;slot<slots.count;++slot) {
                        auto* model=slots.data[slot];
                        if(!model || int(slot)==skip || model->comboSelIndex2!=order) continue;
                        ModelApplyMorphs(model);SetPhysicsMode(model,phase==CpuPosePhase::AfterPhysics ? 1 : 0,slots.data,mode);
                    }
            };
            ops.physics=[](CpuModelSlots slots,CpuPhysicsModelPhase phase,CpuSlotOrder,int skip) {
                for(std::size_t slot=0;slot<slots.count;++slot) if(slots.data[slot] && int(slot)!=skip) {
                    if(phase==CpuPhysicsModelPhase::KinematicSync) ModelKinematicSync(slots.data[slot]);
                    else if(phase==CpuPhysicsModelPhase::DynamicReseat) ModelDynamicReseat(slots.data[slot]);
                    else ModelPhysicsReadback(slots.data[slot]);
                }
            };
        }
        return {{slots.data(),slots.size()},&world,ops};
    }
    static void Advance(void* user,CpuSimulationState& s,bool) {
        auto& w=*static_cast<World*>(user);
        w.first.bones[0].trans[0]=s.cursorSeconds*2;w.first.bones[0].trans[1]=4;
        w.second.bones[0].trans[1]=1;
        w.first.bones[3].trans[1]=s.cursorSeconds*.3f;
        w.second.bones[3].trans[2]=s.cursorSeconds*.2f;
        w.first.morph.value=w.second.morph.value=s.cursorSeconds*.5f;
    }
    CpuSimulationHooks Hooks() { return {this,Advance,nullptr}; }
};
bool Equal(const World& a,const World& b,int frame) {
    int model=0;
    for(const auto& pair:{std::make_pair(&a.first,&b.first),std::make_pair(&a.second,&b.second)}) {
        for(int bone=0;bone<4;++bone) {
            const auto& x=pair.first->bones[bone];const auto& y=pair.second->bones[bone];
            for(const auto& field:{std::make_pair(x.matInit,y.matInit),std::make_pair(x.matWorld,y.matWorld),
                                   std::make_pair(x.matLocal,y.matLocal),std::make_pair(x.matExtra,y.matExtra)}) {
                for(int n=0;n<16;++n) if(!std::isfinite(field.first[n]) || !std::isfinite(field.second[n])) return false;
                if(std::memcmp(field.first,field.second,64)) {
                    for(int n=0;n<16;++n) if(std::memcmp(field.first+n,field.second+n,4))
                        std::fprintf(stderr,"model=%d bone=%d frame=%d component=%d actual=%.9g expected=%.9g\n",model,bone,frame,n,field.first[n],field.second[n]);
                    return false;
                }
            }
            if(std::memcmp(x.ikBackup,y.ikBackup,sizeof(x.ikBackup))) return false;
        }
        ++model;
    }
    for(const auto& pair:{std::make_pair(&a.body0,&b.body0),std::make_pair(&a.body1,&b.body1)}) {
        const auto x=cpu_reference::BodyState(*pair.first),y=cpu_reference::BodyState(*pair.second);
        for(float value:x) if(!std::isfinite(value)) return false;
        if(std::memcmp(x.data(),y.data(),x.size()*sizeof(float))) return false;
    }
    return true;
}
}
int main() {
    for(bool pmx:{false,true}) for(int mode=0;mode<=3;++mode) {
        std::printf("case pmx=%d mode=%d\n",int(pmx),mode);
        World native(pmx),reference(pmx);CpuSimulationState s,r;s.physicsMode=r.physicsMode=mode;
        unsigned frame=0;
        for(float target:{0.0f,.033333335f,.11f,.25f,.25f,.02f,.06666667f}) {
            s.remainingSeconds=r.remainingSeconds=frame==2 ? .04f : 1.0f/30.0f;
            s.resetPending=r.resetPending=(frame==0 || frame==5);
            s.bodiesMoved=r.bodiesMoved=frame==3;
            const int skip=frame==2 ? 254 : -1;
            CHECK(RunCpuSimulationCatchup(s,native.Context(),target,skip,native.Hooks()));
            cpu_reference::Catchup(r,reference.Context(true),target,skip,reference.Hooks());
            CHECK(Equal(native,reference,frame));
            CpuSimulationFrame f;f.playbackActive=frame!=4;f.frameCopyPending=frame==3;
            CHECK(RunCpuSimulationFrame(s,native.Context(),f));cpu_reference::Frame(r,reference.Context(true),f);
            CHECK(Equal(native,reference,frame));
            CHECK(s.resetPending==r.resetPending && s.bodiesMoved==r.bodiesMoved);
            CHECK(std::memcmp(&s.remainingSeconds,&r.remainingSeconds,4)==0);++frame;
        }
    }
    // ABI bridge isolation and legacy state edits between catch-up and physics.
    auto app=std::make_unique<MMDApp>();app->CpuSimulation().remainingSeconds=.013f;
    app->PlaybackCursorSeconds()=3;app->PlaybackPhysicsMode()=2;app->GravityY()=-1;
    ImportCpuSimulation(*app);app->GravityY()=.75f;app->PhysicsResetPending()=1;
    auto& s=ImportCpuSimulation(*app);CHECK(s.gravityDirection[1]==.75f && s.resetPending && s.remainingSeconds==.013f);
    ExportCpuSimulation(*app,s);CHECK(app->GravityY()==.75f && app->PlaybackCursorSeconds()==3);
    std::puts("PASS native CPU simulation: PMD/PMX, 255 slots, native IK/bone morph, two bodies in one world, final matrices and body velocities match frozen legacy flow; no GPU");
}
