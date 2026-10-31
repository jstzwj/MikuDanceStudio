// ===========================================================================
// Physics frame update: 0x004B22F0 / 0x004B3460 / 0x004B25D0 + the physics
// section of the FrameDriver (0x0046F0D0..0x0046FEB8 core)
// ===========================================================================
// Original per-frame flow (verified from the binary):
//   FrameDriver 0x46F0xx region, three "physics blocks" sharing one Bullet
//   world (scene->world, app+0x9EDB0):
//     block 1/2 (0x46F12C / 0x46F412): catch-up loops.  While the physics
//       cursor app+0x9E64C is behind the target time, each tick runs
//       [morph apply 0x4970B0 + SetPhysicsMode 0x4A9220 per model (ordered
//       by model+0x2D7D), kinematic sync 0x4B22F0 per model (reverse slot
//       order), world->stepSimulation(1/60, 10, 1/60)] and advances the
//       cursor by 1/60 (dbl_52EA08).  flt_52EA00 = dbl_52EA08 = 1/60.
//     block 3 (0x46FC77): per-model [morph apply 0x4970B0 (0x46FCB1) +
//       SetPhysicsMode], then - gated on
//       (app+0xA066C moved | app+0x9EDB5 settle | app+0x330 frame advanced)
//       and model count 1..3 and app+0xA0B74==0 - the settle section:
//         all models: 0x4B22F0 (kinematic sync)
//         stepSimulation (twice when dragged with 3 models)
//         app+0x9EDB5: 3x [all models: 0x4B3460 (reseat) + step], clear
//         all models: 0x4B25D0 (physics readback into the bones)
//         clear app+0xA066C
//       then a second per-model [morph apply 0x4970B0 (0x46FE93) +
//       SetPhysicsMode(a2=1)] pass (0x46FE5F).  (0x4970B0 was labelled
//       "IK" in early notes - it is the morph application pass; the CCD IK
//       solve runs inside BoneFrameTransform 0x493A60 phase D, reached via
//       SetPhysicsMode.)
//     0x46F7DB..0x46F808: inside the selection gate (var_14A1 selActive &&
//       app+0x2F8==0 && app+0x9ED90==0), `cmp byte [app+0xA03E8],0 / jnz`
//       guards `mov byte [app+0x9EDB5],1`.  0xA03E8 is NOT a "not playing"
//       flag - it is the x86 twin of the x64 0xA137C stay-behind latch: the
//       pump tail (0x47985A..0x47985E) writes this pump's var_14A1 back
//       into it, so the settle request fires on the selActive RISING EDGE
//       only, exactly like x64 0x7FF7CB44C12E (`cmp [0xA137C],0 / jnz /
//       mov [0x9FCC1],1`).
//   stepSimulation is reached through vtable slot 7 (+0x1C) loaded into
//   eax ("call eax" - byte pattern search for FF/2 disp8 finds nothing);
//   parameters timeStep == fixedTimeStep == 1/60f, maxSubSteps = 10.
//
// btRigidBody::saveKinematicState (0x4F1000) reads the motion state into
// m_worldTransform before deriving velocities - stock bullet 2.75 already
// behaves this way (verified against the repo's bullet-2.75 source), so
// 0x4B22F0 only has to write the motion state.
//
// Physics scheduling and reset behavior:
//   - 0x4970B0 (morph application) is ported as ModelApplyMorphs and is
//     called in both ordered per-model passes, matching 0x46FCB1/0x46FE93.
//   - The block 1/2 catch-up loops (frame-step 0x46F12C / playing 0x46F3D6,
//     cursor 0x9E64C driven by 0x9E654 + wall clock, clamped by 0x9E658)
//     live in playback_catchup.cpp (PlaybackCatchup), called by FrameDriver
//     right before this settle section.
//   - 0x4B3460 / x64 sub_7FF7CB4E45D0 resets only the CURRENT transform
//     plus the four velocity slots (linear/angular, current and
//     interpolation); the interpolation world transform is NOT rewritten
//     and neither clearForces nor the motion state nor activation is
//     touched (write sequence 0x7FF7CB4E4856..0x7FF7CB4E4927).  The port
//     uses the matching public Bullet setters rather than writing
//     implementation-private members.
//   - The settle request latches on the selActive rising edge in BOTH
//     originals (x86 `cmp byte [0xA03E8],0` at 0x46F7FF, x64
//     `cmp [0xA137C],r15b` at 0x7FF7CB44C12E; layout_pins.hpp pins
//     selectionActiveLatch to 0xA03E8 / 0xA137C).  The only per-arch delta
//     is the pump-tail latch write-back: x86 0x47985E sits inside the
//     "model selection present && not playing" gate (0x4797C2
//     `jz 0x479ADC` skips the write when model+0x2D90<0 or app+0x330!=0,
//     so during playback the latch keeps its old value and the request
//     re-fires per pass), while x64 0x7FF7CB456F21 writes it back
//     unconditionally.  That write-back lives in frame_driver.cpp (which
//     follows x64); physics_frame.cpp itself is arch-identical here.
//   - x64 gates the flow twice where x86 (the outline above) gates once:
//     gate A 0x7FF7CB44B8E7 (pump counter app+0xA1E18 == 0) skips the
//     whole section, gate B 0x7FF7CB44C65B (accessory dialog app+0xA166D
//     != 0) skips only the pose passes / world pass / readback / moved
//     clear.  The readback loop and the moved clear sit INSIDE the settle
//     gate in BOTH layouts: x86 0x46FD39 `jz loc_46FE5F` and x64
//     0x7FF7CB44C73D `jz 0x7FF7CB44C85F` both jump past them straight to
//     the second pose pass (the only xrefs into x86 0x46FE34 are the
//     in-gate entries 0x46FDD3/0x46FE2D).  The port matches both.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#if defined(_M_IX86)
#include "mikudancestudio/mmd_math.hpp"
#endif
#include <Windows.h>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/physics_world.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "btBulletDynamicsCommon.h"
#include "mikudancestudio/cpu_model_physics.hpp"

#include "../app/cpu_simulation_bridge.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/cpu_pose_schedule.hpp"
#include "../app/frame_state_dump.hpp"

namespace mikudancestudio {

using d3dx::D3DXMATRIXF;

namespace {

// Use the imported D3DX math functions for the bone-follow chain.
namespace {
D3DXMATRIXF* D3dxRotX(D3DXMATRIXF* o, float a) {
    return d3dx::Get().rotX(o, a);
}
D3DXMATRIXF* D3dxRotY(D3DXMATRIXF* o, float a) {
    return d3dx::Get().rotY(o, a);
}
D3DXMATRIXF* D3dxRotZ(D3DXMATRIXF* o, float a) {
    return d3dx::Get().rotZ(o, a);
}
D3DXMATRIXF* D3dxMul(D3DXMATRIXF* o, const D3DXMATRIXF* a, const D3DXMATRIXF* b) {
    return d3dx::Get().multiply(o, a, b);
}
D3DXMATRIXF* D3dxTranslation(D3DXMATRIXF* o, float x, float y, float z) {
    return d3dx::Get().translation(o, x, y, z);
}
D3DXMATRIXF* D3dxInverse(D3DXMATRIXF* o, const D3DXMATRIXF* m) {
    return d3dx::Get().inverse(o, nullptr, m);
}
float* D3dxQuatFromMatrix(float out[4], const D3DXMATRIXF* m) {
    return d3dx::Get().quatFromMatrix(out, m);
}
float* D3dxQuatMul(float out[4], const float a[4], const float b[4]) {
    return d3dx::Get().quatMultiply(out, a, b);
}
} // namespace

#ifdef MIKUDANCESTUDIO_DIAG
// ---- rigid-body state dumps under MIKUDANCESTUDIO_STATE_DUMP_DIR ---------
// (porting-era A/B tooling; see frame_state_dump.hpp for the gate)

template <typename T> void WriteStateWords(std::FILE* stream, const char* name, const T& value) {
    std::fprintf(stream, "],\"%s\":[", name);
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t word = 0; word < sizeof(value) / 4; ++word) {
        if (word != 0)
            std::fputc(',', stream);
        std::uint32_t bits = 0;
        std::memcpy(&bits, bytes + 4 * word, sizeof(bits));
        std::fprintf(stream, "\"%08X\"", bits);
    }
}

void WriteEmptyStateField(std::FILE* stream, const char* name) {
    std::fprintf(stream, "],\"%s\":[", name);
}

void DumpRigidBodyState(MMDApp* app, const char* fileName) {
    char directory[MAX_PATH]{};
    const DWORD length =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\%s", directory, fileName);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "wb") != 0 || stream == nullptr)
        return;

    std::fputs("{\n  \"schema\": 1,\n  \"models\": [", stream);
    bool firstModel = true;
    // port diagnostic: bound = model-slot capacity (kModelSlotCount)
    for (unsigned slot = 0; slot < static_cast<unsigned>(kModelSlotCount); ++slot) {
        // Model slots are pointers.  The old x86 address expression used a
        // four-byte stride and silently walked through the middle of entries
        // in the x64 build, making diagnostic output (and any future caller
        // copied from it) observe arbitrary models.
        auto* model = app->ModelSlot(slot);
        if (model == nullptr)
            continue;
        auto* rigids = model->rigidTable;
        const int count = static_cast<int>(model->rigidCount);
        if (!firstModel)
            std::fputc(',', stream);
        firstModel = false;
        std::fprintf(stream,
                     "\n    {\"slot\": %u, \"rigid_count\": %d, "
                     "\"rigids\": [",
                     slot, count);
        if (rigids != nullptr && count >= 0 && count <= 100000) {
            for (int i = 0; i < count; ++i) {
                if (i != 0)
                    std::fputc(',', stream);
                const mdl::RigidRecord& rec = rigids[i];
                btRigidBody* body = rec.body;
                std::fprintf(stream,
                             "{\"index\":%d,\"bone\":%d,\"mode\":%d,"
                             "\"world_bits\":[",
                             i, rec.boneIndex,
                             static_cast<int>(static_cast<signed char>(rec.mode)));
                if (body != nullptr) {
                    const auto* transform =
                        reinterpret_cast<const unsigned char*>(&body->getWorldTransform());
                    for (int word = 0; word < 16; ++word) {
                        if (word != 0)
                            std::fputc(',', stream);
                        std::uint32_t value = 0;
                        std::memcpy(&value, transform + 4 * word, 4);
                        std::fprintf(stream, "\"%08X\"", value);
                    }
                }
                if (body != nullptr) {
                    WriteStateWords(stream, "interpolation_world_bits",
                                    body->getInterpolationWorldTransform());
                    WriteStateWords(stream, "interpolation_linear_bits",
                                    body->getInterpolationLinearVelocity());
                    WriteStateWords(stream, "interpolation_angular_bits",
                                    body->getInterpolationAngularVelocity());
                    WriteStateWords(stream, "linear_bits", body->getLinearVelocity());
                    WriteStateWords(stream, "angular_bits", body->getAngularVelocity());
                } else {
                    WriteEmptyStateField(stream, "interpolation_world_bits");
                    WriteEmptyStateField(stream, "interpolation_linear_bits");
                    WriteEmptyStateField(stream, "interpolation_angular_bits");
                    WriteEmptyStateField(stream, "linear_bits");
                    WriteEmptyStateField(stream, "angular_bits");
                }
                std::fputs("]}", stream);
            }
        }
        std::fputs("]}", stream);
    }
    if (!firstModel)
        std::fputc('\n', stream);
    std::fputs("  ]\n}\n", stream);
    std::fclose(stream);
}

void DumpWorldSolverInfo(btDiscreteDynamicsWorld* world) {
    char directory[MAX_PATH]{};
    const DWORD length =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || world == nullptr)
        return;

    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\physics_world.json", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "wb") != 0 || stream == nullptr)
        return;

    const btContactSolverInfo& info = world->getSolverInfo();
    const auto* worldBytes = reinterpret_cast<const unsigned char*>(world);
    const auto* infoBytes = reinterpret_cast<const unsigned char*>(&info);
    std::fprintf(stream,
                 "{\n  \"solver_info_offset\": %u,\n"
                 "  \"solver_info_size\": %u,\n  \"words\": [",
                 static_cast<unsigned>(infoBytes - worldBytes),
                 static_cast<unsigned>(sizeof(info)));
    for (std::size_t i = 0; i < sizeof(info) / 4; ++i) {
        if (i != 0)
            std::fputc(',', stream);
        std::uint32_t word = 0;
        std::memcpy(&word, infoBytes + i * 4, 4);
        std::fprintf(stream, "\"%08X\"", word);
    }
    std::fputs("]\n}\n", stream);
    std::fclose(stream);
}

#endif // MIKUDANCESTUDIO_DIAG

} // namespace

struct NativePhysicsMath {
    void rotX(D3DXMATRIXF* m, float a) { D3dxRotX(m, a); }
    void rotY(D3DXMATRIXF* m, float a) { D3dxRotY(m, a); }
    void rotZ(D3DXMATRIXF* m, float a) { D3dxRotZ(m, a); }
    void mul(D3DXMATRIXF* o, const D3DXMATRIXF* a, const D3DXMATRIXF* b) { D3dxMul(o, a, b); }
    void translation(D3DXMATRIXF* o, float x, float y, float z) { D3dxTranslation(o, x, y, z); }
    bool inverse(D3DXMATRIXF* o, const D3DXMATRIXF* m) { return D3dxInverse(o, m) != nullptr; }
    void quatFromMatrix(float* q, const D3DXMATRIXF* m) { D3dxQuatFromMatrix(q, m); }
    void quatMultiply(float* o, const float* a, const float* b) { D3dxQuatMul(o, a, b); }
};
struct NativePhysicsTraits {
    using Model = mdl::ModelRecord;
    using Bone = mdl::BoneRecord;
    using Rigid = mdl::RigidRecord;
    using Joint = mdl::JointRecord;
    using Matrix = D3DXMATRIXF;
    using Math = NativePhysicsMath;
};
using NativePhysicsKernel = cpu_physics::Kernel<NativePhysicsTraits>;
// VA 0x004B22F0 - push kinematic rigid bodies to the bones.
// For every mode==0 record the bone-follow transform is written into the
// body's motion state; btRigidBody::saveKinematicState (called from
// stepSimulation) pulls it into m_worldTransform and derives velocities.
void ModelKinematicSync(mdl::ModelRecord* m) {
    NativePhysicsKernel::ModelKinematicSync(m);
}

// VA 0x004B3460 / x64 sub_7FF7CB4E45D0 - reseat dynamic rigid bodies onto
// their bone-follow pose.  Teleports every mode>0 body to the follow
// transform and stops it (current transform + the four velocity slots only;
// see ReseatRigidBody for the exact x64 write set).
void ModelDynamicReseat(mdl::ModelRecord* m) {
#ifdef MIKUDANCESTUDIO_DIAG
    const int count = m->rigidCount;
    if (count > 0) {
        auto* rigids = m->rigidTable;
        static int reseatCalls = 0;
        const bool traceReseat = std::getenv("MIKUDANCESTUDIO_TRACE_RESEAT") != nullptr;
        if (traceReseat && reseatCalls < 12) {
            int active = 0;
            for (int i = 0; i < count; ++i)
                if (static_cast<signed char>(rigids[i].mode) > 0)
                    ++active;
            std::fprintf(stderr, "RESEAT call=%d count=%d active(rec80>0)=%d\n", reseatCalls, count,
                         active);
        }
        ++reseatCalls;
        if (traceReseat && reseatCalls <= 3)
            for (int i = 0; i < count; ++i) {
                const auto& rec = rigids[i];
                auto* body = rec.body;
                if (static_cast<signed char>(rec.mode) > 0 && body && body->m_debugBodyId == 3) {
                    unsigned bits;
                    const float x = body->getWorldTransform().getOrigin().getX();
                    std::memcpy(&bits, &x, 4);
                    std::fprintf(stderr, "RESEAT body3 iter=%d rec80=%d pre_origin=%08X\n",
                                 reseatCalls, int(rec.mode), bits);
                }
            }
    }
#endif
    NativePhysicsKernel::ModelDynamicReseat(m);
}

// VA 0x004B25D0 - pull the simulation result back into the bones.
//  part 1: over-stretched joints (distance - limit >= 2) teleport body B
//          onto the clamped position of body A and stop it.
//  part 2: bone world matrix (+52) = rigid offset matrix (+108) *
//          body world transform.
//  part 3: local matrix (+116) = world * inverse(parent world); physics
//          pose pos +0x188 / quat +0x194 derived from it, plus the
//          type-5 and limit-flag corrections and the mode==2 collapse.
void ModelPhysicsReadback(mdl::ModelRecord* m) {
    NativePhysicsKernel::ModelPhysicsReadback(m);
}

// Physics section of the FrameDriver (original 0x46F0D0..0x46FEB8 core).
// One shared Bullet world steps at a fixed 1/60 with a 10-substep cap;
// kinematic bodies are pushed from the bones before the step and the
// dynamic result is read back after the settle iterations.
void PhysicsFrame(MMDApp* app, unsigned char selActive) {
    auto& s = *app;
    // ---- gate A: the shared message-seen / pump counter ------------------
    // ONE field in both originals (x64 app+0xA1E18 / x86 app+0xA0D6C =
    // state.messageSeen), with three jobs: the Present gate in section 9 of
    // FrameDriver, the pump-region gates, and this physics gate. Every
    // window message (MainWndProc entry) and each dialog-proc entry point
    // resets it to 1; the pump prologue (AdvanceFrameRenderGate,
    // 0x7FF7CB44755E..0x7FF7CB4475DD / x86 0x46B0B1..0x46B117, called at the
    // top of FrameDriver) refreshes it to 1 while playing ([0x368]/[0x330]),
    // while the idle-physics suppress toggle is OFF ([0xA54CC] / x86
    // 0xA4420 = FrameVolumeControlEnabled, menu 299) or while the modeless
    // English-name dialog is open ([0xA1B98] / x86 0xA0B50 = EnglishNameDialog,
    // menu 259); otherwise it walks 1->2->3->0.  0x7FF7CB44B8E7
    // (`cmp [app+0xA1E18],0 / jz -> 0x7FF7CB44C8D6`, x86 twin 0x46EFBE)
    // skips the ENTIRE physics section - wind, gravity, settle request,
    // pose passes, world pass and readback - once it has decayed to 0, i.e.
    // two pump passes after the last trigger dropped.  A mouse hovering the
    // window keeps it alive through WM_MOUSEMOVE, like the original.
    // (PlaybackCatchup is not behind this gate in the port: the x64
    // catch-up blocks share it but can only run while playing, which
    // already holds the counter open.)
    if (s.state.messageSeen == 0)
        return; // 0x7FF7CB44B8E7 -> 0x7FF7CB44C8D6
    PhysicsScene* scene = s.Physics();
    if (scene == nullptr)
        return;
    btDiscreteDynamicsWorld* world = scene->world; // slot 64 / 0x40
    if (world == nullptr)
        return;
    auto& simulation = ImportCpuSimulation(s);
    CpuSimulationFrame frame;
    frame.deltaSeconds = s.DeltaTime();
    frame.selectionActive = selActive != 0;
    frame.selectionWasActive = s.state.selectionActiveLatch != 0;
    frame.modelMode = s.state.optflag[0] != 0;
    frame.frameStep = s.FrameStepPlayback() != 0;
    frame.playbackActive = s.PlaybackActive() != 0;
    frame.accessoryEditing = s.state.accessoryEditDialogOpen != 0;
    frame.frameCopyPending = s.FrameCopyOrPhysicsDialog() != 0;
    CpuSimulationHooks hooks{app, nullptr, nullptr};
#ifdef MIKUDANCESTUDIO_DIAG
    char stageCaptureValue[2]{};
    static LONG stagesCaptured = 0;
    // Arm on the first executable physics pass, including the synchronous
    // PMM-load pass.  Waiting for optflag[0] to clear observes a world that has
    // already been stepped and makes the original/MikuDanceStudio solver inputs
    // incomparable.
    const bool captureStages =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_PHYSICS_STAGES", stageCaptureValue,
                                sizeof(stageCaptureValue)) == 1 &&
        stageCaptureValue[0] == '1' &&
        // Startup executes an empty-scene physics pass before a PMM is
        // chosen.  Do not consume the one-shot capture there: the useful
        // baseline is the first pass that actually owns model rigid bodies.
        s.ModelSlots()[0] != nullptr && InterlockedCompareExchange(&stagesCaptured, 1, 0) == 0;
#endif
#ifdef MIKUDANCESTUDIO_DIAG
    frame.skipIdleWorld = getenv("MIKUDANCESTUDIO_IDLE_NO_STEP") != nullptr;
    if (const char* forced = getenv("MIKUDANCESTUDIO_FORCE_MAINDT")) {
        frame.overrideMainTimeStep = true;
        frame.mainTimeStep = static_cast<float>(atof(forced));
    }
    static bool zeroLocalTimeAfterLoad = getenv("MIKUDANCESTUDIO_ZERO_LT_AFTER_LOAD") != nullptr;
    struct Trace {
        MMDApp* app;
        btDiscreteDynamicsWorld* world;
        bool capture, trace;
        bool* zeroLocalTime;
        int steps = 0;
        const char* labels[16]{};
        int mode = 0;
        bool settle = false, moved = false;
    } trace{app, world, captureStages, getenv("MIKUDANCESTUDIO_TRACE_STEPS") != nullptr,
            &zeroLocalTimeAfterLoad};
    hooks.user = &trace;
    hooks.observe = [](void* user, const CpuSimulationState& state, CpuSimulationEvent event,
                       int index, float) {
        auto& t = *static_cast<Trace*>(user);
        auto* app = t.app;
        ExportCpuSimulation(*app, state);
        const auto rigid = [&](const char* name) {
            if (t.capture)
                DumpRigidBodyState(app, name);
        };
        const auto pose = [&](const char* name) {
            if (t.capture)
                DumpFrameEntryState(app, name, false);
        };
        switch (event) {
        case CpuSimulationEvent::GravityApplied:
            if (t.capture) {
                ArmBoneTransformIkProbe();
                DumpWorldSolverInfo(t.world);
            }
            break;
        case CpuSimulationEvent::BeforePose:
            t.mode = state.physicsMode;
            t.settle = state.resetPending != 0;
            t.moved = state.bodiesMoved != 0;
            if (t.trace) {
                const float localTime =
                    static_cast<const PhysicsWorld*>(t.world)->SubstepRemainder();
                unsigned bits = 0;
                std::memcpy(&bits, &localTime, 4);
                fprintf(stderr, "PHYSFRAME enter settle=%d count=%d localTime=%08X\n",
                        int(t.settle), t.mode, bits);
            }
            pose("physics_pose0.json");
            break;
        case CpuSimulationEvent::KinematicSync:
            rigid("physics_sync.rigids.json");
            pose("physics_sync.json");
            break;
        case CpuSimulationEvent::MainStep:
        case CpuSimulationEvent::ExtraStep:
        case CpuSimulationEvent::SettleStep:
            if (t.steps < 16)
                t.labels[t.steps] = event == CpuSimulationEvent::MainStep    ? "main"
                                    : event == CpuSimulationEvent::ExtraStep ? "extra"
                                                                             : "settle";
            ++t.steps;
            break;
        case CpuSimulationEvent::Stepped:
            if (index) {
                char name[64]{};
                std::snprintf(name, sizeof(name), "physics_settle_post%d.rigids.json", index);
                rigid(name);
            } else {
                rigid("physics_step.rigids.json");
                pose("physics_step.json");
            }
            break;
        case CpuSimulationEvent::SettleReseat: {
            char name[64]{};
            std::snprintf(name, sizeof(name), "physics_settle_pre%d.rigids.json", index);
            rigid(name);
            break;
        }
        case CpuSimulationEvent::Settled:
            rigid("physics_settle.rigids.json");
            pose("physics_settle.json");
            break;
        case CpuSimulationEvent::Readback:
            rigid("physics_readback.rigids.json");
            pose("physics_readback.json");
            break;
        case CpuSimulationEvent::AfterPose:
            if (*t.zeroLocalTime) {
                static_cast<PhysicsWorld*>(t.world)->ClearSubstepRemainder();
                *t.zeroLocalTime = false;
            }
            pose("physics_pose1.json");
            break;
        default:
            break;
        }
    };
#endif
    RunCpuSimulationFrame(simulation, EditorCpuSimulationContext(s), frame, hooks);
    ExportCpuSimulation(s, simulation);
#ifdef MIKUDANCESTUDIO_DIAG
    if (trace.trace && trace.steps > 0) {
        fprintf(stderr, "PHYSFRAME steps=%d settle=%d moved=%d count=%d labels=", trace.steps,
                int(trace.settle), int(trace.moved), trace.mode);
        for (int n = 0; n < trace.steps && n < 16; ++n)
            fprintf(stderr, "%s%s", n ? "," : "", trace.labels[n]);
        fprintf(stderr, "\n");
    }
#endif
}

} // namespace mikudancestudio
