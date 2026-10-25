#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/menu_commands.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>

using namespace mikudancestudio;
namespace {
int failures, cleanCalls, drawCalls;
int jointCalls;
bool drawEnabled;
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
template <class T> T* Allocate(std::size_t n) {
    auto* p = static_cast<T*>(::operator new(n * sizeof(T)));
    std::memset(p, 0, n * sizeof(T));
    return p;
}
constexpr char center[] = "\x83\x5a\x83\x93\x83\x5e\x81\x5b";
constexpr char upper[] = "\x8f\xe3\x94\xbc\x90\x67";
constexpr char knee[] = "\x8d\xb6\x82\xd0\x82\xb4";
constexpr char legIk[] = "\x8d\xb6\x91\xab\x82\x68\x82\x6a";
constexpr char wrist[] = "\x8d\xb6\x8e\xe8\x8e\xf1";
constexpr char shoulder[] = "\x8d\xb6\x8c\xa8";

struct Fixture {
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    Fixture() {
        ModelInitDefaults(model.get());
        model->boneCount = 6;
        model->boneTable = Allocate<mdl::BoneRecord>(6);
        model->bonePhysicsState = Allocate<unsigned char>(6);
        model->boneSelection = Allocate<unsigned char>(6);
        model->boneKeys = Allocate<mdl::BoneKey>(mdl::kBoneKeyCapacity);
        model->morphKeys = Allocate<mdl::MorphKey>(mdl::kMorphKeyCapacity);
        model->displayKeys = Allocate<mdl::DisplayKey>(mdl::kDisplayKeyCapacity);
        const char* names[] = {center, upper, knee, legIk, wrist, shoulder};
        for (int i = 0; i < 6; ++i) {
            std::strcpy(model->boneTable[i].name, names[i]);
            model->boneKeys[i].rotation[3] = 1;
            model->bonePhysicsState[i] = static_cast<unsigned char>(i + 10);
            model->boneSelection[i] = 1;
        }
        ModelInitMorphSlots(model.get());
    }
    ~Fixture() { ModelDispose(model.get()); }
    void Trace(unsigned n = 2) {
        model->poseTraceBuffer = Allocate<mdl::PoseTraceSample>(n);
        model->poseTraceSampleCount = n;
        for (unsigned i = 0; i < n; ++i) {
            auto& sample = model->poseTraceBuffer[i];
            sample.center[0] = 10.0f + i;
            sample.center[1] = 20;
            sample.center[2] = 30;
            sample.upperBody = {4, 1.0f + i, 2, 3};
            sample.leftKnee = {8, 5.0f + i, 6, 7};
            sample.leftLegIk = {{40.0f + i, 50, 60}, {12, 9.0f + i, 10, 11}};
            sample.leftWrist = {16, 13, 14, 15};
            sample.leftShoulder = {20, 17, 18, 19};
        }
    }
    const mdl::BoneKey* Key(int bone, unsigned frame) {
        unsigned i = bone;
        do {
            if (model->boneKeys[i].frame == frame)
                return &model->boneKeys[i];
            i = model->boneKeys[i].next;
        } while (i != 0);
        return nullptr;
    }
};

void Registration() {
    for (unsigned version : {13u, 14u, 15u}) {
        for (int ikState : {-1, 0, 1}) {
            Fixture f;
            auto& model = *f.model;
            model.openniVersion = static_cast<unsigned char>(version);
            if (ikState >= 0) {
                model.ikChainCount = 2;
                model.ikChains = Allocate<mdl::IkChain>(2);
                model.ikChains[0].boneIndex = model.ikChains[1].boneIndex = 3;
                model.ikChains[0].enabled = static_cast<unsigned char>(ikState);
                model.ikChains[1].enabled = static_cast<unsigned char>(!ikState);
            }
            f.Trace();
            model.UndoCursor() = mdl::kUndoHistoryDepth - 1;
            Check(RegisterKinectPoseCapture(&model, 10), "capture registration succeeds");
            const auto* c = f.Key(0, 10);
            const auto* u = f.Key(1, 11);
            Check(c && c->position[0] == 10 && c->rotation[3] == 1,
                  "center name matches original SJIS and writes position/identity");
            Check(u && u->rotation[0] == 2 && u->rotation[1] == 2 && u->rotation[2] == 3 &&
                      u->rotation[3] == 4 && u->position[0] == 0,
                  "second 308-byte sample converts WXYZ to XYZW");
            if (u)
                for (int i = 0; i < 16; ++i)
                    Check(u->interpolation[i] == (i < 8 ? 20 : 107),
                          "interpolation uses channel columns");
            Check((f.Key(2, 10) != nullptr) == (ikState == 0),
                  "knee keys require a matching disabled IK chain");
            const auto* ik = f.Key(3, 10);
            Check((ik != nullptr) == (ikState == 1), "the first matching IK chain wins");
            if (ik)
                Check(ik->position[0] == 40 && ik->rotation[0] == 9 && ik->rotation[3] == 12,
                      "IK quaternion immediately follows position");
            Check((f.Key(4, 10) != nullptr) == (version >= 14), "wrist version gate");
            Check((f.Key(5, 10) != nullptr) == (version >= 15), "shoulder version gate");
            Check(model.UndoCursor() == 0 && model.LatestUndoCursor() == 0 &&
                      model.undoDirty == 1 && model.redoDirty == 0,
                  "undo ring wraps at 30");
            const auto& undo = model.undoRings[0].slots[0];
            Check(undo.operation == mdl::UndoOperation::BoneKeys && undo.frame == 10 &&
                      undo.bonePose[1].poseEdited == 11,
                  "undo snapshots use pose-edited flags");
            Check(undo.recordCount > 0 && undo.keySnapshots[0].index == 0,
                  "undo snapshots precede insertion in original probe order");
            Check(model.poseTraceBuffer == nullptr && model.poseTraceSampleCount == 0 &&
                      model.maxFrame == 11,
                  "successful registration frees trace and updates max frame");
            // Overwrite a current key, then a successor, preserving a later key.
            f.Trace();
            model.poseTraceBuffer[0].upperBody.x = 77;
            Check(RegisterKinectPoseCapture(&model, 10), "existing consecutive keys overwrite");
            Check(f.Key(1, 10) && f.Key(1, 10)->rotation[0] == 77 && f.Key(1, 11),
                  "current/successor overwrite retains the key chain");
        }
    }
}

void RecordingAndLifetime() {
    Fixture f;
    auto& m = *f.model;
    m.currentJoints[mdl::TrackedJoint::Center][0] = 20;
    m.currentJoints[mdl::TrackedJoint::Center][1] = 40;
    m.currentJoints[mdl::TrackedJoint::Center][2] = 60;
    m.skeletonCalibration.heightScale = 2;
    Check(!ModelStandardPoseSetup(&m, true, 1, 0), "start does not signal a stopped recording");
    Check(m.poseTraceRecording == 1 && m.poseTraceSampleCount == 1 &&
              m.poseTraceBuffer[0].center[0] == 10,
          "start allocates trace and captures model-space position");
    Check(m.poseTraceBuffer[0].leftWrist.w == m.boneTable[4].rotQuat[3] &&
              m.poseTraceBuffer[0].leftWrist.w == 1,
          "wrist name matches original SJIS and writes a scalar-first quaternion");
    auto* buffer = m.poseTraceBuffer;
    Check(!ModelStandardPoseSetup(&m, true, 0, 0) && m.poseTraceBuffer == buffer &&
              m.poseTraceSampleCount == 2 && m.poseTraceBuffer[1].center[0] == -10,
          "continued capture reuses allocation and mirrors X at application");
    Check(ModelStandardPoseSetup(&m, false, 0, 0) && m.poseTraceSampleCount == 2 &&
              !m.poseTraceRecording,
          "stop signals once without appending a sample");
    Check(!ModelStandardPoseSetup(&m, false, 0, 0), "idle stop is not repeated");
    m.poseTraceRecording = 1;
    ModelInitMorphSlots(&m);
    Check(m.poseTraceBuffer == buffer && m.poseTraceSampleCount == 2 && !m.poseTraceRecording,
          "tracking reset retains trace ownership and count, matching MMD");
    Check(m.skeletonHistory.joints[0].samples[0][1] == mdl::kInvalidJointY,
          "tracking reset initializes missing history samples");
    // The final sample fits exactly; the pump disables recording before the next append.
    ModelStandardPoseSetup(&m, true, 1, 0);
    m.poseTraceSampleCount = mdl::kPoseTraceCapacity - 1;
    ModelStandardPoseSetup(&m, true, 1, 0);
    Check(m.poseTraceSampleCount == mdl::kPoseTraceCapacity, "last trace sample fits capacity");
    ModelStandardPoseSetup(&m, false, 1, 0);
    ModelDispose(&m);
    Check(!m.poseTraceBuffer && !m.poseTraceSampleCount && !m.poseTraceRecording,
          "disposing an interrupted capture releases owned storage");
    // The fixture destructor can safely repeat disposal for these empty pools.
    m.boneCount = 0;
}

LRESULT CALLBACK CloseOverflow(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_CREATEWND) {
        wchar_t cls[32]{};
        GetClassNameW(reinterpret_cast<HWND>(wp), cls, 32);
        if (std::wcscmp(cls, L"#32770") == 0)
            PostMessageW(reinterpret_cast<HWND>(wp), WM_CLOSE, 0, 0);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
void ExhaustedKeys() {
    Fixture f;
    f.Trace(1);
    auto& model = *f.model;
    for (std::size_t i = model.boneCount; i < mdl::kBoneKeyCapacity; ++i)
        model.boneKeys[i].frame = 7;
    const HHOOK hook = SetWindowsHookExW(WH_CBT, CloseOverflow, nullptr, GetCurrentThreadId());
    if (!hook) {
        Check(false, "overflow message hook");
        return;
    }
    Check(!RegisterKinectPoseCapture(&model, 10), "exhausted keys return failure");
    Check(model.poseTraceBuffer && model.poseTraceSampleCount == 1,
          "exhaustion preserves captured samples, matching the reference");
    UnhookWindowsHookEx(hook);
}

void WINAPI Clean() {
    ++cleanCalls;
}
void WINAPI Draw(bool enabled) {
    ++drawCalls;
    drawEnabled = enabled;
}
void WINAPI Joint(int id, float* p) {
    ++jointCalls;
    p[0] = static_cast<float>(id);
    p[1] = mdl::kInvalidJointY;
    p[2] = 0;
    if (id == 0) {
        p[0] = 20;
        p[1] = 40;
        p[2] = 60;
    }
}
void DriverGates() {
    auto app = std::make_unique<MMDApp>();
    app->state = {};
    app->state.fpsLimit = 30;
    app->state.fpsLimitSaved = 60;
    app->state.depthDeviceEnabled = 1;
    app->state.kinectCaptureActive = 1;
    app->OpenNi().clean = Clean;
    app->OpenNi().drawDepthMap = Draw;
    app->CameraMode() = 0;
    app->state.autoRepeat = 4;
    ManageKinectRecordGate(app.get(), 1);
    Check(drawCalls == 1 && drawEnabled && app->state.autoRepeat == 4,
          "tracking in model mode retains recording gate");
    ManageKinectRecordGate(app.get(), 0);
    Check(app->state.autoRepeat == 0, "losing tracking clears the capture stage");
    DisableKinect(app.get());
    Check(cleanCalls == 1 && !app->state.depthDeviceEnabled && !app->state.kinectCaptureActive &&
              app->state.fpsLimit == 60 && app->PhysicsResetPending() == 1,
          "stop cleans SDK and restores FPS/physics gate");
    Check(app->OpenNi().clean == Clean, "stop retains loaded SDK callbacks");
    ManageKinectRecordGate(app.get(), 0);
    Check(!drawEnabled, "depth drawing is off while capture is idle");

    Fixture first, second;
    app->ModelSlot(0) = first.model.get();
    app->ModelSlot(1) = second.model.get();
    app->state.slotIdx = 0;
    app->OpenNi().jointPosition = Joint;
    app->state.framesPerSecond = 30;
    app->state.openniVersion = 13;
    app->state.kinectMirrorEnabled = 1;
    first.model->skeletonCalibration.heightScale = 2;
    PumpKinectSkeleton(app.get(), 0);
    Check(jointCalls == 0, "inactive tracking does not read the SDK");
    app->state.frameStepPlayback = 1;
    PumpKinectSkeleton(app.get(), 1);
    Check(jointCalls == 0, "frame-step playback blocks skeleton collection");
    app->state.frameStepPlayback = 0;
    app->state.autoRepeat = 4;
    PumpKinectSkeleton(app.get(), 1);
    Check(jointCalls == 18 && first.model->poseTraceSampleCount == 1 &&
              first.model->poseTraceBuffer[0].center[0] == 10,
          "pump fetches, smooths, transforms and records the selected model in order");
    app->state.slotIdx = 1;
    app->state.autoRepeat = 0;
    PumpKinectSkeleton(app.get(), 1);
    Check(jointCalls == 36 && second.model->poseTraceSampleCount == 0 &&
              first.model->poseTraceSampleCount == 1 && first.model->poseTraceRecording,
          "model switching retains the previous model's capture state");
    app->state.slotIdx = 0;
    app->state.autoRepeat = 4;
    PumpKinectSkeleton(app.get(), 1);
    Check(first.model->poseTraceSampleCount == 2, "switching back continues that model's capture");
    app->ModelSlot(0) = app->ModelSlot(1) = nullptr;
}
}
int main() {
    Registration();
    RecordingAndLifetime();
    ExhaustedKeys();
    DriverGates();
    return failures ? 1 : 0;
}
