#include "btBulletDynamicsCommon.h"

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

namespace mikudancestudio {
void CmdViewMenu(MMDApp*, HWND, std::uint16_t, std::uint16_t);
}

namespace {
using namespace mikudancestudio;

bool Check(bool condition, const char* message) {
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

void Identity(float matrix[16]) {
    std::memset(matrix, 0, 16 * sizeof(float));
    for (int i = 0; i < 4; ++i)
        matrix[i * 5] = 1.0f;
}

bool CheckPlayback() {
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bones[2]{};
    mdl::BoneKey keys[6]{};
    mdl::MorphRecord morph{};
    mdl::MorphKey morphKeys[3]{};
    mdl::DisplayKey display[3]{};
    mdl::IkChain chain{};
    std::uint8_t flags[3] = {1, 0, 1};
    std::uint8_t boneActive[2]{}, morphActive{};
    std::uint32_t boneCursors[2]{}, morphCursor{};
    model->boneCount = 2;
    model->boneTable = bones;
    model->boneKeys = keys;
    model->boneTrackActive = boneActive;
    model->boneKeyCursors = boneCursors;
    model->morphCount = 1;
    model->morphs = &morph;
    model->morphKeys = morphKeys;
    model->morphTrackActive = &morphActive;
    model->morphKeyCursors = &morphCursor;
    model->displayKeys = display;
    model->ikChains = &chain;
    model->ikChainCount = 1;
    bones[0].slotIndex = -1;
    bones[1].type = mdl::BoneType::InertTip;
    for (int i = 0; i < 3; ++i) {
        keys[i * 2].frame = morphKeys[i].frame = display[i].frame = i * 3;
        keys[i * 2].previous = i ? (i - 1) * 2 : 0;
        keys[i * 2].next = i < 2 ? (i + 1) * 2 : 0;
        keys[i * 2].position[0] = static_cast<float>(i * 6);
        keys[i * 2].rotation[3] = 1.0f;
        morphKeys[i].previous = display[i].previous = i ? i - 1 : 0;
        morphKeys[i].next = display[i].next = i < 2 ? i + 1 : 0;
        morphKeys[i].value = i * 0.5f;
        display[i].visible = flags[i];
        display[i].ikStates = &flags[i];
    }
    // Both originals store seconds * 30 as float. 0.1f hits frame 3,
    // rather than advancing to the key at frame 6 through a double product.
    InitModelTrackCursors(model.get(), 0.1f, 0);
    if (!Check(model->displayKeyCursor == 1 && morphCursor == 1 && boneCursors[0] == 2,
               "playback initialization stops on the exact key"))
        return false;
    InitModelTrackCursors(model.get(), 0.0f, 0);
    AdvanceModelKeyframes(model.get(), 0.05f, 0);
    if (!Check(bones[0].trans[0] == 3.0f && morph.value == 0.25f && model->loadComplete == 1,
               "fractional playback evaluates bone, morph and display tracks"))
        return false;
    AdvanceModelKeyframes(model.get(), 0.1f, 0);
    if (!Check(model->displayKeyCursor == 1 && boneCursors[0] == 2 && morphCursor == 1 &&
                   model->displayTrackActive == 1 && boneActive[0] == 1 && morphActive == 1 &&
                   boneActive[1] == 0 && bones[0].trans[0] == 6.0f && morph.value == 0.5f &&
                   model->loadComplete == 0 && chain.enabled == 0,
               "exact playback key retains active cursors and updates IK visibility"))
        return false;
    AdvanceModelKeyframes(model.get(), 0.2f, 0);
    if (!Check(model->displayTrackActive == 1 && boneActive[0] == 1 && morphActive == 1,
               "last exact key remains active until playback passes it"))
        return false;
    AdvanceModelKeyframes(model.get(), 0.3f, 0);
    return Check(model->displayTrackActive == 0 && boneActive[0] == 0 && morphActive == 0 &&
                     bones[0].trans[0] == 12.0f && morph.value == 1.0f && chain.enabled == 1,
                 "exhausted playback tracks retain their terminal values");
}

bool CheckCursorRounding() {
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::DisplayKey display{};
    mdl::MorphRecord morph{};
    mdl::MorphKey keys[2]{};
    std::uint8_t active{};
    std::uint32_t cursor{};
    model->displayKeys = &display;
    model->morphCount = 1;
    model->morphs = &morph;
    model->morphKeys = keys;
    model->morphTrackActive = &active;
    model->morphKeyCursors = &cursor;
    keys[0].next = 1;
    keys[1].frame = 64;
    keys[1].value = 64.0f; // Power-of-two scaling exposes the evaluated frame exactly.
    struct Case {
        std::uint32_t seconds, frame;
    };
    // Fixed instruction-stage results: x64 RVA 0xD08EC..0xD0925;
    // x86 float stores at 0x4A31E1/0x4A31F7/0x4A322A.
    const Case cases[] = {{0x3DCCCCCD, 0x40400000},
                          {0x3D088889, 0x3F800000},
                          {0x3EAAACDA, 0x41200419},
                          {0x3F9E0419, 0x421423D7}};
    for (const auto& test : cases) {
        float seconds;
        std::memcpy(&seconds, &test.seconds, sizeof(seconds));
        InitModelTrackCursors(model.get(), 0.0f, 0);
        AdvanceModelKeyframes(model.get(), seconds, 0);
        std::uint32_t bits;
        std::memcpy(&bits, &morph.value, sizeof(bits));
        if (!Check(bits == test.frame, "playback cursor preserves the original float stores"))
            return false;
    }
    return true;
}

bool CheckMorphAndPoseSources() {
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bone{};
    mdl::BoneOrderEntry selector{};
    mdl::MorphRecord morph{};
    mdl::PmxBoneMorphEntry entry{};
    mdl::BoneMorphOffsetRecord offset{};
    mdl::ModelMaterialRecord material{};
    mdl::MaterialMorphPool base{}, add{}, multiply{};
    model->boneTable = &bone;
    model->boneCount = 1;
    model->boneOrderTable = &selector;
    selector.linkedModel = -1;
    bone.parent = bone.slotIndex = -1;
    bone.hasRigidBody = 1;
    bone.rotQuat[3] = bone.ikBackup[6] = 1.0f;
    for (int i = 0; i < 3; ++i) {
        bone.trans[i] = static_cast<float>(i + 1);
        bone.ikBackup[i] = static_cast<float>((i + 1) * 10);
        entry.translation[i] = static_cast<float>((i + 1) * 4);
    }
    model->physicsMode = 2;
    model->morphs = &morph;
    model->morphCount = 1;
    model->boneMorphTable = &offset;
    model->boneMorphCount = 1;
    model->materials = &material;
    model->materialCount = 1;
    model->materialMorphPools = {&base, &add, &multiply};
    morph.type = 2;
    morph.value = 0.25f;
    morph.boneEntries = &entry;
    morph.boneCount = 1;
    entry.rotation[3] = 1.0f;
    ModelApplyMorphs(model.get());
    SetPhysicsMode(model.get(), 0, nullptr, 1);
    if (!Check(bone.physicsOffset[0] == 11.0f && bone.matInit[12] == 11.0f &&
                   bone.matInit[13] == 22.0f && bone.matInit[14] == 33.0f,
               "physics backup plus bone morph propagates through the transform pass"))
        return false;
    bone.physicsDisabled = 1;
    SetPhysicsMode(model.get(), 0, nullptr, 2);
    return Check(bone.physicsOffset[0] == 2.0f && bone.matInit[12] == 2.0f &&
                     bone.matInit[13] == 4.0f && bone.matInit[14] == 6.0f,
                 "per-bone physics disable selects the keyed pose before morph application");
}

bool CheckRigidSynchronization() {
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bone{};
    mdl::RigidRecord rigid{};
    btSphereShape shape(1.0f);
    btDefaultMotionState motion;
    btRigidBody::btRigidBodyConstructionInfo info(1.0f, &motion, &shape);
    btRigidBody body(info);
    model->boneTable = &bone;
    model->boneCount = 1;
    model->rigidTable = &rigid;
    model->rigidCount = 1;
    model->centerBone = 0;
    rigid.body = &body;
    bone.parent = -1;
    Identity(bone.matInit);
    Identity(rigid.invTransform);
    bone.matInit[12] = 10.0f;
    bone.position[0] = 2.0f;
    rigid.position[0] = 0.5f;
    body.setActivationState(ISLAND_SLEEPING);
    NotifyBonePhysicsMode(model.get(), 0, 1);
    ModelKinematicSync(model.get());
    btTransform follow;
    motion.getWorldTransform(follow);
    if (!Check(rigid.mode == 0 && body.isKinematicObject() &&
                   body.getActivationState() == ISLAND_SLEEPING &&
                   follow.getOrigin().getX() == 12.5f &&
                   body.getWorldTransform().getOrigin() == btVector3(0, 0, 0),
               "kinematic sync updates the motion state without moving or activating the body"))
        return false;
    NotifyBonePhysicsMode(model.get(), 0, 0);
    body.setLinearVelocity(btVector3(1, 2, 3));
    body.setAngularVelocity(btVector3(4, 5, 6));
    body.setInterpolationLinearVelocity(btVector3(7, 8, 9));
    body.setInterpolationAngularVelocity(btVector3(10, 11, 12));
    body.applyCentralForce(btVector3(3, 4, 5));
    const btTransform interpolation = body.getInterpolationWorldTransform();
    rigid.boneIndex = -1; // Dynamic reseating falls back to centerBone.
    ModelDynamicReseat(model.get());
    if (!Check(
            rigid.mode == 1 && !body.isKinematicObject() &&
                body.getWorldTransform().getOrigin().getX() == 12.5f &&
                body.getLinearVelocity() == btVector3(0, 0, 0) &&
                body.getAngularVelocity() == btVector3(0, 0, 0) &&
                body.getInterpolationLinearVelocity() == btVector3(0, 0, 0) &&
                body.getInterpolationAngularVelocity() == btVector3(0, 0, 0) &&
                body.getInterpolationWorldTransform() == interpolation &&
                body.getTotalForce() == btVector3(3, 4, 5) &&
                body.getActivationState() == ISLAND_SLEEPING,
            "reseat preserves forces, interpolation pose and activation while stopping velocities"))
        return false;
    rigid.boneIndex = 0;
    ModelPhysicsReadback(model.get());
    return Check(bone.matInit[12] == 12.5f && bone.matExtra[12] == 12.5f &&
                     bone.ikBackup[0] == 12.5f && bone.ikBackup[6] == 1.0f,
                 "dynamic readback restores bone world and backup poses");
}

bool CheckRigidCreation() {
    btDefaultCollisionConfiguration config;
    btCollisionDispatcher dispatcher(&config);
    btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world(&dispatcher, &broadphase, &solver, &config);
    PhysicsScene scene{};
    scene.world = &world;
    RigidBodyCreationResult result{};
    float matrix[16];
    Identity(matrix);
    CreateRigidBody(&scene, result, 3, 1, 1, 1, matrix, 0, 1, 0, 0, 0, 0, 0, 0xFFFF);
    if (!Check(result.id == -1 && result.body == nullptr,
               "invalid shape has a typed failure result"))
        return false;
    CreateRigidBody(&scene, result, 0, 1, 1, 1, matrix, 0, 1, 0, 0, 0, 0, 0, 0xFFFF);
    const bool valid =
        Check(result.body != nullptr && result.id == result.body->m_debugBodyId &&
                  world.getNumCollisionObjects() == 1 && result.body->isKinematicObject(),
              "rigid creation returns the actual integer ID and Bullet body");
    if (result.body) {
        world.removeRigidBody(result.body);
        delete result.body->getMotionState();
        delete result.body->getCollisionShape();
        delete result.body;
    }
    return valid;
}

bool CheckPhysicsMenuModes() {
    auto app = std::make_unique<MMDApp>();
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bones[2]{};
    mdl::RigidRecord rigids[2]{};
    btSphereShape shape(1.0f);
    btDefaultMotionState motionA, motionB;
    btRigidBody::btRigidBodyConstructionInfo infoA(1.0f, &motionA, &shape);
    btRigidBody::btRigidBodyConstructionInfo infoB(1.0f, &motionB, &shape);
    btRigidBody bodyA(infoA), bodyB(infoB);
    // Exercise the actual menu handler, including the architecture-specific
    // last slot. A mistaken motion-state sync cannot enable either body.
    app->ModelSlot(kModelSlotCount - 1) = model.get();
    model->boneCount = 2;
    model->boneTable = bones;
    model->rigidCount = 2;
    model->rigidTable = rigids;
    rigids[0].body = &bodyA;
    rigids[1].body = &bodyB;
    rigids[1].boneIndex = 1;
    rigids[1].kinematicFlag = 1; // Restore mode 2 when physics is enabled.
    bones[0].hasRigidBody = bones[1].hasRigidBody = 1;
    bones[0].physicsDisabled = 1;
    struct Case {
        std::uint16_t command;
        int viewMode, modeA, modeB;
    };
    const Case cases[] = {{MDS_CMD_PHYSICS_DISABLED, 0, 0, 0},
                          {MDS_CMD_PHYSICS_ALWAYS, 1, 1, 2},
                          {MDS_CMD_PHYSICS_ON_OFF, 2, 0, 2},
                          {MDS_CMD_PHYSICS_TRACE, 3, 0, 2}};
    for (const auto& test : cases) {
        CmdViewMenu(app.get(), nullptr, test.command, 0);
        if (!Check(app->PlaybackPhysicsMode() == test.viewMode && rigids[0].mode == test.modeA &&
                       rigids[1].mode == test.modeB &&
                       bodyA.isKinematicObject() == (test.modeA == 0) &&
                       bodyB.isKinematicObject() == (test.modeB == 0),
                   "physics menu applies Bullet modes through the final model slot"))
            return false;
    }
    // Permanently static records remain excluded even when their bone participates.
    rigids[0].staticFlag = 1;
    CmdViewMenu(app.get(), nullptr, MDS_CMD_PHYSICS_ALWAYS, 0);
    return Check(rigids[0].mode == 0 && bodyA.isKinematicObject(),
                 "physics menu preserves permanently static bodies");
}
} // namespace

int main() {
    return CheckPlayback() && CheckCursorRounding() && CheckMorphAndPoseSources() &&
                   CheckRigidSynchronization() && CheckRigidCreation() && CheckPhysicsMenuModes()
               ? 0
               : 1;
}
