#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

namespace mds = mikudancestudio;
namespace mdl = mikudancestudio::mdl;

namespace {
int failures;
std::vector<char> events;
mdl::ModelRecord* observedModel;
std::vector<float> poseAtFrameChange;
struct PhysicsChange {
    int bone;
    unsigned char requested, before;
    float position;
};
std::vector<PhysicsChange> physicsChanges;
WNDPROC originalEditProc;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == EM_REPLACESEL && observedModel)
        poseAtFrameChange.push_back(observedModel->boneTable[0].trans[0]);
    return CallWindowProcA(originalEditProc, hwnd, message, w, l);
}

struct Fixture {
    std::unique_ptr<mds::MMDApp> app = std::make_unique<mds::MMDApp>();
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord, 3> bones{};
    std::array<unsigned char, 3> selected{}, edits{};
    std::vector<mdl::BoneKey> keys{mdl::kBoneKeyCapacity};
    std::vector<mdl::MorphKey> morphKeys{mdl::kMorphKeyCapacity};
    std::vector<mdl::DisplayKey> displayKeys{mdl::kDisplayKeyCapacity};
    HWND window;

    Fixture() {
        window = CreateWindowExA(0, "MDSUndoFrameTest", "", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                 GetModuleHandleA(nullptr), nullptr);
        if (!window)
            std::abort();
        const auto control = [&](int id, const char* cls, DWORD style) {
            if (!CreateWindowExA(0, cls, "", WS_CHILD | style, 0, 0, 1, 1, window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 GetModuleHandleA(nullptr), nullptr))
                std::abort();
        };
        control(mds::panel::kCurrentFrameEdit, "EDIT", 0);
        control(mds::panel::kPhysicsCheckbox, "BUTTON", BS_CHECKBOX);
        control(mds::panel::kPhysicsFrameCheckbox, "BUTTON", BS_CHECKBOX);
        originalEditProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(GetDlgItem(window, mds::panel::kCurrentFrameEdit), GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(EditProc)));
        app->MainWindow() = window;
        app->SidebarWidth() = 300;
        app->ModelSlot(0) = model.get();
        model->hwnd = window;
        model->boneCount = static_cast<unsigned>(bones.size());
        model->boneTable = bones.data();
        model->boneSelection = selected.data();
        model->bonePhysicsState = edits.data();
        model->boneKeys = keys.data();
        model->morphKeys = morphKeys.data();
        model->displayKeys = displayKeys.data();
        model->UndoCursor() = model->LatestUndoCursor() = mdl::kUndoHistoryDepth - 1;
        model->searchCursor = 123;
        for (std::size_t i = 0; i < bones.size(); ++i) {
            bones[i].rotQuat[3] = keys[i].rotation[3] = 1.0f;
            bones[i].trans[0] = 10.0f + static_cast<float>(i);
            edits[i] = static_cast<unsigned char>(i + 2);
        }
        events.clear();
        physicsChanges.clear();
        poseAtFrameChange.clear();
    }
    ~Fixture() {
        observedModel = nullptr;
        DestroyWindow(window);
        for (auto& ring : model->undoRings)
            for (auto& slot : ring.slots) {
                ::operator delete(slot.bonePose);
                ::operator delete(slot.keySnapshots);
            }
    }
    mdl::UndoRecord& Undo() { return model->undoRings[0].slots[model->UndoCursor()]; }
    void Register(int frame, int mode = 0) {
        mds::RegisterSelectedBoneKeys(model.get(), frame, mode);
    }
    using KeyState = std::array<mdl::BoneKey, 6>;
    KeyState FirstKeys() {
        KeyState result;
        std::memcpy(result.data(), keys.data(), sizeof(result));
        return result;
    }
    void CheckKeys(const KeyState& expected, const char* message) {
        Check(std::memcmp(keys.data(), expected.data(), sizeof(expected)) == 0, message);
    }
};

void TestRegistration() {
    Fixture f;
    f.selected[1] = 1;
    f.keys[3].frame = 10; // Occupied on an unrelated track.
    f.model->redoDirty = 1;
    const auto before = f.FirstKeys();
    f.Register(20);
    Check(f.model->UndoCursor() == 0 && f.model->LatestUndoCursor() == 0 &&
              f.model->undoDirty == 1 && f.model->redoDirty == 0,
          "registration wraps the ring and opens the undo/redo gates");
    Check(f.keys[1].next == 4 && f.keys[4].previous == 1 && f.keys[4].frame == 20 &&
              f.keys[4].position[0] == 11.0f && f.model->searchCursor == 123,
          "pose insertion scans from boneCount without changing the clipboard cursor");
    Check(f.Undo().recordCount == 2 && f.Undo().keySnapshots[0].index == 1 &&
              f.Undo().keySnapshots[1].index == 4 && f.edits[1] == 0 && f.selected[1] == 1,
          "insertion snapshots touched slots once and clears only the registered edit marker");
    for (int lane = 0; lane < 4; ++lane)
        Check(f.keys[4].interpolation[lane] == 20 && f.keys[4].interpolation[lane + 4] == 20 &&
                  f.keys[4].interpolation[lane + 8] == 107 &&
                  f.keys[4].interpolation[lane + 12] == 107,
              "new pose keys receive default interpolation");
    const auto after = f.FirstKeys();
    std::int32_t frame = 99;
    mds::UndoModelEdit(f.model.get(), frame);
    f.CheckKeys(before, "undo restores the entire inserted key and its predecessor");
    Check(frame == 20 && f.edits[1] == 3, "key undo restores the frame and pending edit flag");
    f.bones[1].trans[0] = 999.0f;
    f.edits[1] = 9;
    mds::RedoModelEdit(f.model.get(), frame);
    f.CheckKeys(after, "redo restores every byte of touched key slots");
    Check(f.bones[1].trans[0] == 999.0f && f.edits[1] == 9,
          "key redo leaves pose application to the caller's refresh chain");

    for (int i = 0; i < 16; ++i)
        f.keys[4].interpolation[i] = static_cast<unsigned char>(i + 31);
    f.bones[1].trans[0] = 42.0f;
    f.Register(20);
    Check(f.Undo().recordCount == 1 && f.Undo().keySnapshots[0].index == 4 &&
              f.keys[4].position[0] == 42.0f && f.keys[4].interpolation[0] == 31 &&
              f.keys[4].interpolation[15] == 46,
          "overwriting a pose preserves custom interpolation with automatic curves off");
    f.Register(10);
    Check(f.keys[1].next == 5 && f.keys[5].next == 4 && f.keys[4].previous == 5 &&
              f.Undo().recordCount == 3 && f.Undo().keySnapshots[0].index == 1 &&
              f.Undo().keySnapshots[1].index == 4 && f.Undo().keySnapshots[2].index == 5,
          "middle insertion keeps the reference link and snapshot order");
    CheckDlgButton(f.window, mds::panel::kPhysicsFrameCheckbox, BST_CHECKED);
    f.bones[1].trans[0] = 35.0f;
    f.Register(10);
    Check(f.keys[5].interpolation[0] == 64 && f.keys[5].interpolation[4] == 0 &&
              f.keys[5].interpolation[8] == 87 && f.keys[5].interpolation[12] == 87 &&
              f.keys[4].interpolation[0] == 40 && f.keys[4].interpolation[4] == 40 &&
              f.keys[5].interpolation[3] == 20 && f.keys[5].interpolation[11] == 107,
          "automatic interpolation rebuilds position neighbours and defaults rotation");
}

void TestPhysicsRegistration() {
    for (int mode = 0; mode < 4; ++mode) {
        for (int disabled = 0; disabled <= 1; ++disabled) {
            Fixture f;
            f.selected[1] = 1;
            auto& bone = f.bones[1];
            bone.hasRigidBody = 1;
            bone.physicsDisabled = static_cast<unsigned char>(disabled);
            bone.ikBackup[0] = 22.0f;
            bone.ikBackup[6] = 1.0f;
            f.Register(5, mode);
            const bool physicsPose = mode == 1 || (mode >= 2 && disabled == 0);
            Check(f.keys[3].position[0] == (physicsPose ? 22.0f : 11.0f) &&
                      bone.trans[0] == (physicsPose ? 22.0f : 11.0f) &&
                      f.keys[3].physicsDisabled == 1 && bone.physicsDisabled == 1,
                  "registration selects current/physics pose before toggling its mode");
            Check(physicsChanges.size() == (mode >= 2 && disabled == 0 ? 1u : 0u),
                  "physics notification occurs only on a transition in modes 2/3");
            if (!physicsChanges.empty())
                Check(physicsChanges[0].bone == 1 && physicsChanges[0].requested == 1 &&
                          physicsChanges[0].before == 0 && physicsChanges[0].position == 22.0f,
                      "physics notification observes the copied pose and old bone mode");
            Check(f.Undo().bonePose[1].position[0] == 11.0f && f.Undo().bonePose[1].poseEdited == 3,
                  "undo captures the edit marker before physics registration");
        }
    }
    Fixture f;
    f.selected[1] = 1;
    f.bones[1].hasRigidBody = f.bones[1].physicsDisabled = 1;
    CheckDlgButton(f.window, mds::panel::kPhysicsCheckbox, BST_CHECKED);
    f.Register(5, 2);
    Check(physicsChanges.size() == 1 && physicsChanges[0].requested == 0 &&
              physicsChanges[0].before == 1 && f.keys[3].physicsDisabled == 0 &&
              f.bones[1].physicsDisabled == 0,
          "checked physics UI re-enables physics after capturing the current pose");
}

void TestSelectedSnapshotAndDeletion() {
    Fixture f;
    const std::size_t last = mdl::kBoneKeyCapacity - 1;
    f.keys[last].allocated = 1;
    f.keys[last].position[2] = 8.0f;
    mds::SnapshotSelectedKeysForUndo(f.model.get(), 37);
    Check(f.Undo().recordCount == 1 && f.Undo().keySnapshots[0].index == last &&
              f.Undo().keySnapshots[0].key.position[2] == 8.0f,
          "selected-key snapshots include the final slot on both architectures");
    f.keys[last].allocated = 0;
    f.keys[0].next = 3;
    f.keys[3].frame = 10;
    f.keys[3].previous = 0;
    f.keys[3].next = 4;
    f.keys[3].allocated = 1;
    f.keys[3].position[0] = 4.0f;
    f.keys[4].frame = 20;
    f.keys[4].previous = 3;
    f.morphKeys[3].frame = 15;
    f.morphKeys[3].allocated = 1;
    f.morphKeys[0].next = 3;
    f.displayKeys[2].frame = 15;
    f.displayKeys[2].allocated = 1;
    f.displayKeys[0].next = 2;
    const auto before = f.FirstKeys();
    mds::DeleteMarkedModelKeys(f.model.get(), 10);
    Check(f.keys[0].next == 4 && f.keys[4].previous == 0 && f.keys[3].frame == 0 &&
              f.keys[3].previous == 0 && f.keys[3].next == 0 && f.keys[3].allocated == 0 &&
              f.keys[3].position[0] == 0.0f && f.keys[3].rotation[3] == 1.0f,
          "deletion unlinks and resets a marked non-root bone key");
    const auto deleted = f.FirstKeys();
    std::int32_t frame = 99;
    mds::UndoModelEdit(f.model.get(), frame);
    f.CheckKeys(before, "deletion undo restores the removed key and both neighbours");
    Check(f.morphKeys[3].frame == 0 && f.displayKeys[2].frame == 0,
          "bone undo retains the reference's lack of morph/display undo records");
    mds::RedoModelEdit(f.model.get(), frame);
    f.CheckKeys(deleted, "deletion redo restores the unlinked state");
    f.keys[0].allocated = 1;
    f.keys[0].position[0] = 17.0f;
    f.keys[0].physicsDisabled = 1;
    f.bones[0].hasRigidBody = 1;
    const auto rootBefore = f.FirstKeys();
    mds::DeleteMarkedModelKeys(f.model.get(), 0);
    Check(f.keys[0].next == 4 && f.keys[0].position[0] == 0 && f.keys[0].rotation[3] == 1 &&
              f.keys[0].physicsDisabled == 0 && f.Undo().recordCount == 2,
          "root deletion keeps the track head, resets physics and deduplicates snapshots");
    mds::UndoModelEdit(f.model.get(), frame);
    f.CheckKeys(rootBefore, "root deletion undo restores the root and its successor");
}

void TestChainedUndo() {
    Fixture f;
    f.keys[0].next = 3;
    f.keys[3].frame = 10;
    f.keys[3].allocated = 1;
    const auto before = f.FirstKeys();
    mds::DeleteMarkedModelKeys(f.model.get(), 10);
    mds::BeginRangeScaleBoneUndo(f.model.get(), 10, 1);
    Check(f.model->undoRings[0].slots[0].operation == mdl::UndoOperation::ChainedBoneKeys &&
              f.model->UndoCursor() == 1,
          "range scaling joins the deletion and reinsertion undo entries");
    mds::RegisterBonePoseAtFrame(f.model.get(), 0, 20, 0);
    const auto after = f.FirstKeys();
    std::int32_t frame = 99;
    mds::UndoModelEdit(f.model.get(), frame);
    f.CheckKeys(before, "one undo restores both halves of range scaling");
    Check(f.model->UndoCursor() == mdl::kUndoHistoryDepth - 1 && frame == 10,
          "chained undo wraps across the two entries");
    mds::RedoModelEdit(f.model.get(), frame);
    f.CheckKeys(after, "one redo reapplies both halves of range scaling");
    Check(f.model->UndoCursor() == 1, "chained redo ends on the reinsertion entry");
}

void TestPoseAndEmptyUndo() {
    Fixture f;
    mds::SnapshotPoseBeforeFrameChange(f.model.get(), 47);
    f.bones[0].trans[0] = 77.0f;
    f.edits[0] = 0;
    observedModel = f.model.get();
    std::int32_t frame = 99;
    mds::UndoModelEdit(f.model.get(), frame);
    Check(frame == 47 && f.bones[0].trans[0] == 10.0f && f.edits[0] == 2,
          "pose-with-frame undo restores frame, pose and edit marker");
    Check(!poseAtFrameChange.empty() && poseAtFrameChange.back() == 77.0f,
          "frame edit message precedes pose restoration as in the reference");
    frame = 88;
    mds::RedoModelEdit(f.model.get(), frame);
    Check(frame == 88 && f.bones[0].trans[0] == 77.0f && f.edits[0] == 0,
          "pose redo restores the displaced pose without changing the frame");
    mds::UndoModelEdit(f.model.get(), frame);
    mds::UndoModelEdit(f.model.get(), frame);
    Check(f.model->UndoCursor() == mdl::kUndoHistoryDepth - 1 && f.model->undoDirty == 0,
          "empty undo disables the gate without advancing the cursor");
    f.model->redoDirty = 1;
    f.model->undoRings[1].slots[0].operation = mdl::UndoOperation::Empty;
    mds::RedoModelEdit(f.model.get(), frame);
    Check(f.model->UndoCursor() == 0 && f.model->redoDirty == 0,
          "empty redo advances the cursor before disabling its gate");
}

void TestFrameLineSnapshots() {
    for (bool insert : {true, false}) {
        Fixture f;
        f.selected = {1, 0, 1};
        f.edits = {0, 1, 0};
        f.keys[0].next = 3;
        f.keys[3].frame = 10;
        const auto before = f.FirstKeys();
        f.app->CurrentFrame() = 10;
        if (insert)
            mds::InsertBoneCameraFrameLine(f.app.get());
        else
            mds::DeleteBoneCameraFrameLine(f.app.get());
        Check(f.Undo().bonePose[0].poseEdited == 0 && f.Undo().bonePose[1].poseEdited == 1 &&
                  f.Undo().bonePose[2].poseEdited == 0,
              "frame-line undo snapshots pending edits rather than selection");
        std::int32_t frame = 99;
        mds::UndoModelEdit(f.model.get(), frame);
        f.CheckKeys(before, "frame-line undo restores key records and links");
        Check(f.edits[0] == 0 && f.edits[1] == 1 && f.selected[0] == 1 && f.selected[1] == 0,
              "frame-line undo keeps edit markers distinct from selection");
    }
}

void TestFrameStepping() {
    for (bool forward : {true, false}) {
        Fixture f;
        f.selected = {1, 0, 1};
        f.edits = {0, 1, 0};
        f.app->CurrentFrame() = 10;
        mds::StepFrame(f.app.get(), forward);
        Check(f.model->UndoCursor() == 0 && f.Undo().frame == 10 &&
                  f.Undo().operation == mdl::UndoOperation::BonePoseWithFrame &&
                  f.Undo().bonePose[1].poseEdited == 1,
              "frame stepping snapshots pending pose edits before changing frame");
        Check(f.edits == std::array<unsigned char, 3>{} &&
                  f.selected == std::array<unsigned char, 3>{1, 0, 1} &&
                  f.app->CurrentFrame() == (forward ? 11 : 9) &&
                  events == std::vector<char>{'S', 'P'},
              "frame stepping clears edits, retains selection, then seeks and paints");
    }
    Fixture f;
    f.edits = {};
    f.selected[0] = 1;
    mds::StepFrame(f.app.get(), false);
    Check(f.app->CurrentFrame() == 0 && f.model->UndoCursor() == mdl::kUndoHistoryDepth - 1 &&
              f.selected[0] == 1,
          "selection alone creates no undo, and backward stepping stops at frame zero");
}

void TestEmptyEditAndRingReuse() {
    Fixture f;
    f.edits = {};
    mds::SnapshotPoseBeforeFrameChange(f.model.get(), 47);
    mds::SnapshotSelectedKeysForUndo(f.model.get(), 47);
    Check(f.model->UndoCursor() == mdl::kUndoHistoryDepth - 1,
          "unmodified pose and empty key selection create no snapshot");
    f.Register(47);
    Check(f.Undo().operation == mdl::UndoOperation::BoneKeys && f.Undo().recordCount == 0,
          "registration with no selected bones still creates the reference's empty edit");
    std::int32_t frame = 99;
    mds::UndoModelEdit(f.model.get(), frame);
    Check(frame == 47 && f.model->UndoCursor() == mdl::kUndoHistoryDepth - 1,
          "an empty key edit restores its frame and wraps undo");
    frame = 99;
    mds::RedoModelEdit(f.model.get(), frame);
    Check(frame == 47 && f.model->UndoCursor() == 0,
          "an empty key edit restores its frame and wraps redo");

    f.selected[0] = 1;
    for (int i = 0; i < mdl::kUndoHistoryDepth + 5; ++i) {
        f.bones[0].trans[0] = static_cast<float>(i);
        f.Register(0);
    }
    const int cursor = f.model->UndoCursor();
    Check(cursor == 5 && f.model->LatestUndoCursor() == cursor,
          "more than one ring depth reuses snapshot storage and wraps the cursor");
    mds::UndoModelEdit(f.model.get(), frame);
    Check(f.keys[0].position[0] == 33.0f, "undo after ring reuse restores the preceding root key");
    mds::RedoModelEdit(f.model.get(), frame);
    Check(f.keys[0].position[0] == 34.0f && f.model->UndoCursor() == cursor,
          "redo after ring reuse restores the latest root key");
}
} // namespace

namespace mikudancestudio {
MMDApp::MMDApp() : state{} {
}
void NotifyBonePhysicsMode(mdl::ModelRecord* model, int bone, unsigned char disabled) {
    physicsChanges.push_back(
        {bone, disabled, model->boneTable[bone].physicsDisabled, model->boneTable[bone].trans[0]});
}
// Edit/snapshot/undo code is real. Observe evaluation and painting boundaries;
// unrelated audio/scene paths must not execute in this fixture.
int SeekModelFrame(mdl::ModelRecord&, int frame, int) {
    events.push_back('S');
    return frame;
}
void PanelPaint(MMDApp*) {
    events.push_back('P');
}
void SelectionReeval(MMDApp*) {
}
void ReloadModels(MMDApp*) {
    std::abort();
}
void RefreshLightPanel(MMDApp*) {
    std::abort();
}
void RefreshSelfShadowPanel(MMDApp*) {
    std::abort();
}
void ApplyGravityTrack(MMDApp*) {
    std::abort();
}
void ApplyAccessoryTrack(MMDApp*, int) {
    std::abort();
}
void SyncAccessoryEditPanel(MMDApp*) {
    std::abort();
}
void ModelApplyMorphs(mdl::ModelRecord*) {
    std::abort();
}
void SetPhysicsMode(mdl::ModelRecord*, int, mdl::ModelRecord* const*, int) {
    std::abort();
}
void PostModelReload(MMDApp*) {
    std::abort();
}
void AviBgOverlayRefresh(MMDApp*) {
    std::abort();
}
void TimelineDrawTicks(int, int) {
    std::abort();
}
void SetFrameNormalized(int) {
    std::abort();
}
void WaveRestartAt(WaveAudioContext*, double) {
    std::abort();
}
} // namespace mikudancestudio

static_assert(
    std::is_same_v<decltype(&mds::UndoModelEdit), void (*)(mdl::ModelRecord*, std::int32_t&)>);
static_assert(
    std::is_same_v<decltype(&mds::RedoModelEdit), void (*)(mdl::ModelRecord*, std::int32_t&)>);

int main() {
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "MDSUndoFrameTest";
    if (!RegisterClassA(&cls))
        return 1;
    TestRegistration();
    TestPhysicsRegistration();
    TestSelectedSnapshotAndDeletion();
    TestChainedUndo();
    TestPoseAndEmptyUndo();
    TestFrameLineSnapshots();
    TestFrameStepping();
    TestEmptyEditAndRingReuse();
    return failures == 0 ? 0 : 1;
}
