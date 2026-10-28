#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace mds = mikudancestudio;
namespace mdl = mikudancestudio::mdl;

namespace {
int failures = 0;
std::vector<char> refreshes;
void Check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}
}

namespace mikudancestudio {
void SeekPortableSceneAccessory(MMDApp*, int, int) {}
MMDApp::MMDApp() : state{} {
}
MMDApp* g_Block = nullptr;
// Keep the pose/undo implementation real; observe only the UI boundaries.
void RebuildTimelineTrackPanel(MMDApp*) {
    refreshes.push_back('T');
}
void InvalidateSelectionReadouts(MMDApp*) {
    refreshes.push_back('R');
}
void PostViewRefresh(MMDApp*) {
    refreshes.push_back('V');
}
// The original element constructor is empty; the array constructor is real.
void IdentityCtor(void*) {
}
void NotifyBonePhysicsMode(mdl::ModelRecord*, int, unsigned char) {
    std::fputs("Unexpected physics mode change in pose undo\n", stderr);
    std::abort();
}
}

namespace {
// Fixed ABI assertions keep the typed input tied to the original record format.
using CopiedPose = mdl::BonePoseClipboardRecord;
static_assert(sizeof(CopiedPose) == 0x30);
static_assert(offsetof(CopiedPose, position) == 20 && offsetof(CopiedPose, rotation) == 32);

struct Fixture {
    std::unique_ptr<mds::MMDApp> app = std::make_unique<mds::MMDApp>();
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord, 8> bones{};
    std::array<unsigned char, 8> selected{}, physics{};
    std::array<CopiedPose, 4> copies{};
    Fixture() {
        app->ModelSlot(0) = model.get();
        model->boneCount = static_cast<int>(bones.size());
        model->boneTable = bones.data();
        model->boneSelection = selected.data();
        model->bonePhysicsState = physics.data();
        model->UndoCursor() = model->LatestUndoCursor() = 29;
        model->redoDirty = 1;
        app->BonePoseClipboard() = copies.data();
        for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
            std::snprintf(bones[i].name, sizeof bones[i].name, "bone%d", i);
            bones[i].trans[0] = 10.0f + i;
            bones[i].trans[1] = 20.0f + i;
            bones[i].trans[2] = 30.0f + i;
            bones[i].rotQuat[0] = 0.1f;
            bones[i].rotQuat[1] = 0.2f;
            bones[i].rotQuat[2] = 0.3f;
            bones[i].rotQuat[3] = 0.9f;
            physics[i] = static_cast<unsigned char>(i + 2);
            selected[i] = 1;
        }
        refreshes.clear();
    }
    ~Fixture() {
        for (auto& ring : model->undoRings)
            for (auto& slot : ring.slots) {
                ::operator delete(slot.bonePose);
                ::operator delete(slot.keySnapshots);
            }
    }
    void Copy(int index, const char* name) {
        auto& pose = copies[index];
        strcpy_s(pose.name, name);
        pose.position[0] = 2.0f + index;
        pose.position[1] = 3.0f;
        pose.position[2] = 4.0f;
        pose.rotation[0] = 0.25f;
        pose.rotation[1] = 0.5f;
        pose.rotation[2] = 0.75f;
        pose.rotation[3] = 1.0f;
    }
    void Paste(int count) {
        app->BonePoseClipboardCount() = count;
        mds::PasteMirroredBonePose(app.get(), nullptr);
    }
};

void TestSidePair(const char* sourceName, const char* targetName) {
    Fixture f;
    strcpy_s(f.bones[3].name, sourceName);
    strcpy_s(f.bones[5].name, targetName);
    const auto oldTarget = f.bones[5];
    const auto oldSource = f.bones[3];
    f.Copy(0, sourceName);
    f.Paste(1);
    auto& undo = f.model->undoRings[0].slots[0];
    Check(f.model->UndoCursor() == 0 && f.model->LatestUndoCursor() == 0,
          "Reverse paste must wrap both undo cursors at 30");
    Check(f.model->undoDirty == 1 && f.model->redoDirty == 0 &&
              undo.operation == mdl::UndoOperation::BonePose && undo.recordCount == 1,
          "Reverse paste undo header");
    Check(f.model->selectedBone == 5 && f.selected[5] == 1 && f.selected[3] == 0,
          "Reverse paste must select the opposite-side target");
    Check(std::memcmp(f.bones[3].trans, oldSource.trans, sizeof oldSource.trans) == 0,
          "Paired source bone must remain unchanged");
    Check(f.bones[5].trans[0] == -2 && f.bones[5].trans[1] == 3 && f.bones[5].trans[2] == 4,
          "Mirror position X only");
    Check(f.bones[5].rotQuat[0] == 0.25f && f.bones[5].rotQuat[1] == -0.5f &&
              f.bones[5].rotQuat[2] == -0.75f && f.bones[5].rotQuat[3] == 1,
          "Mirror quaternion Y and Z only");
    Check(undo.bonePose[0].boneIndex == 5 && undo.bonePose[0].poseEdited == 7 &&
              std::memcmp(undo.bonePose[0].position, oldTarget.trans, sizeof oldTarget.trans) ==
                  0 &&
              std::memcmp(undo.bonePose[0].rotation, oldTarget.rotQuat, sizeof oldTarget.rotQuat) ==
                  0,
          "Undo snapshot must contain the target pose and prior physics state");
    Check(f.physics[5] == 1 && f.app->PhysicsResetPending() == 1 &&
              refreshes == std::vector<char>{'T', 'R'},
          "Reverse paste refresh and physics flags");
    int frame = 17;
    mds::UndoModelEdit(f.model.get(), frame);
    Check(std::memcmp(f.bones[5].trans, oldTarget.trans, sizeof oldTarget.trans) == 0 &&
              std::memcmp(f.bones[5].rotQuat, oldTarget.rotQuat, sizeof oldTarget.rotQuat) == 0 &&
              f.physics[5] == 7 && frame == 17,
          "Undo must restore the old target pose");
    mds::RedoModelEdit(f.model.get(), frame);
    Check(f.bones[5].trans[0] == -2 && f.bones[5].rotQuat[1] == -0.5f && f.physics[5] == 1,
          "Redo must restore the mirrored pose");
}

void TestOrdinaryPaste() {
    Fixture f;
    const auto before = f.bones[4];
    f.Copy(0, "bone4");
    f.app->BonePoseClipboardCount() = 1;
    mds::PasteCopiedBonePose(f.app.get(), nullptr);
    Check(f.bones[4].trans[0] == 2 && f.bones[4].rotQuat[1] == 0.5f && f.selected[4] == 1 &&
              f.selected[3] == 0 && f.physics[4] == 1 && refreshes == std::vector<char>{'T', 'R'},
          "Ordinary paste retains axes and selects only its exact-name target");
    int frame = 23;
    mds::UndoModelEdit(f.model.get(), frame);
    Check(std::memcmp(f.bones[4].trans, before.trans, sizeof before.trans) == 0 &&
              std::memcmp(f.bones[4].rotQuat, before.rotQuat, sizeof before.rotQuat) == 0 &&
              f.physics[4] == 6 && frame == 23,
          "Ordinary-paste undo restores pose and physics state");
    mds::RedoModelEdit(f.model.get(), frame);
    Check(f.bones[4].trans[0] == 2 && f.bones[4].rotQuat[2] == 0.75f,
          "Ordinary-paste redo retains the copied axes");
}

void TestCopy() {
    Fixture f;
    f.selected.fill(0);
    f.selected[2] = f.selected[5] = 1;
    // Copy owns the clipboard; the fixture's normal input array is borrowed.
    f.app->BonePoseClipboard() = nullptr;
    mds::CopySelectedBonePose(f.app.get(), nullptr);
    const auto* poses =
        static_cast<const mdl::BonePoseClipboardRecord*>(f.app->BonePoseClipboard());
    Check(f.app->BonePoseClipboardCount() == 2 && poses &&
              std::strcmp(poses[0].name, "bone2") == 0 &&
              std::strcmp(poses[1].name, "bone5") == 0 &&
              std::memcmp(poses[1].position, f.bones[5].trans, sizeof poses[1].position) == 0 &&
              std::memcmp(poses[1].rotation, f.bones[5].rotQuat, sizeof poses[1].rotation) == 0,
          "Copy snapshots selected current poses in bone-table order");
    f.selected.fill(0);
    const void* previous = f.app->BonePoseClipboard();
    mds::CopySelectedBonePose(f.app.get(), nullptr);
    Check(f.app->BonePoseClipboard() == previous && f.app->BonePoseClipboardCount() == 2,
          "Copy with no selection preserves the previous clipboard");
    ::operator delete(f.app->BonePoseClipboard());
    f.app->BonePoseClipboard() = nullptr;
}

void TestPoseReset() {
    {
        Fixture f;
        f.selected.fill(0);
        f.selected[0] = f.selected[2] = 1;
        const auto beforeRoot = f.bones[0];
        const auto beforeBone = f.bones[2];
        mds::ResetSelectedBonePose(f.app.get());
        Check(f.bones[0].trans[0] == 10 && f.bones[0].trans[1] == 0 && f.bones[0].trans[2] == 30 &&
                  f.bones[0].rotQuat[3] == 1,
              "Reset root clears only Y and resets its quaternion");
        Check(f.bones[2].trans[0] == 0 && f.bones[2].trans[1] == 0 && f.bones[2].trans[2] == 0 &&
                  f.bones[2].rotQuat[0] == 0 && f.bones[2].rotQuat[1] == 0 &&
                  f.bones[2].rotQuat[2] == 0 && f.bones[2].rotQuat[3] == 1 && f.physics[2] == 1,
              "Parented reset clears translation and all quaternion axes");
        Check(f.bones[3].trans[0] == 13 && refreshes == std::vector<char>{'V'},
              "Reset leaves unselected bones alone and refreshes once");
        int frame = 9;
        mds::UndoModelEdit(f.model.get(), frame);
        Check(std::memcmp(f.bones[0].trans, beforeRoot.trans, sizeof beforeRoot.trans) == 0 &&
                  std::memcmp(f.bones[2].rotQuat, beforeBone.rotQuat, sizeof beforeBone.rotQuat) ==
                      0 &&
                  f.physics[2] == 4,
              "Reset undo captures the selected poses before mutation");
    }
    {
        Fixture f;
        f.selected.fill(0);
        f.selected[1] = 1;
        f.bones[0].rotQuat[0] = f.bones[0].rotQuat[1] = f.bones[0].rotQuat[2] = 0;
        f.bones[0].rotQuat[3] = 1;
        f.bones[1].parent = -1;
        f.bones[1].position[0] = 2;
        f.bones[1].position[1] = 3;
        f.bones[1].position[2] = 4;
        f.bones[1].rotQuat[0] = f.bones[1].rotQuat[1] = f.bones[1].rotQuat[3] = 0;
        f.bones[1].rotQuat[2] = 1; // 180 degrees about Z.
        mds::ResetSelectedBonePose(f.app.get());
        Check(f.bones[1].trans[0] == 6 && f.bones[1].trans[1] == 14 && f.bones[1].trans[2] == 30 &&
                  f.bones[1].rotQuat[2] == 0 && f.bones[1].rotQuat[3] == 1,
              "Unparented reset applies the own quaternion and root translation");
    }
#if defined(_M_X64) || defined(__x86_64__)
    {
        Fixture f;
        f.selected.fill(0);
        f.selected[1] = 1;
        f.bones[1].parent = -1;
        const float position[] = {0.1f, 0.2f, 0.3f};
        const float rotation[] = {0.1f, 0.2f, 0.3f, 0.9f};
        const float root[] = {0.05f, 0.07f, 0.09f};
        std::memcpy(f.bones[1].position, position, sizeof position);
        std::memcpy(f.bones[1].rotQuat, rotation, sizeof rotation);
        std::memcpy(f.bones[0].trans, root, sizeof root);
        mds::ResetSelectedBonePose(f.app.get());
        std::array<std::uint32_t, 3> bits{};
        std::memcpy(bits.data(), f.bones[1].trans, sizeof bits);
        // Frozen from the original MULSS/ADDSS sequence (RVA 0x40248..0x402FD)
        // with the Microsoft D3DX quaternion matrix. Double intermediates give
        // Y=0x3D8F5C2A, Z=0x3DB851EE instead.
        Check(bits == std::array<std::uint32_t, 3>{0x3D4CCCCD, 0x3D8F5C2D, 0x3DB851EC},
              "x64 reset retains each single-precision rounding step");
    }
#endif
}
}

int main() {
    TestOrdinaryPaste();
    TestCopy();
    TestPoseReset();
    // Expected side mappings come from x64 RVA 0x44580..0x446E9:
    // search the exact-name bone for SJIS left/right, then compare suffixes.
    TestSidePair("\x8D\xB6"
                 "arm",
                 "\x89\x45"
                 "arm");
    TestSidePair("\x89\x45"
                 "arm",
                 "\x8D\xB6"
                 "arm");
    TestSidePair("A\x8D\xB6"
                 "arm",
                 "B\x89\x45"
                 "arm");
    {
        Fixture f;
        strcpy_s(f.bones[3].name, "\x8D\xB6"
                                  "arm");
        strcpy_s(f.bones[5].name, "\x89\x45"
                                  "leg");
        f.Copy(0, f.bones[3].name);
        f.Paste(1);
        Check(f.model->selectedBone == 3 && f.bones[3].trans[0] == -2 && f.bones[5].trans[0] == 15,
              "Different suffix must fall back to exact-name bone");
    }
    {
        Fixture f;
        f.Copy(0, "bone4");
        f.Copy(1, "missing");
        f.Paste(2);
        Check(f.bones[4].trans[0] == -2 && f.model->selectedBone == 4 &&
                  refreshes == std::vector<char>{'T', 'R'},
              "Center bone fallback and missing-name skip");
        const auto& slot = f.model->undoRings[0].slots[0];
        const mdl::BonePoseSnapshot zero{};
        Check(slot.recordCount == 2 && std::memcmp(&slot.bonePose[1], &zero, sizeof zero) == 0,
              "Skipped clipboard entry must retain its zeroed undo slot");
    }
    {
        Fixture f;
        f.Paste(0);
        Check(f.model->UndoCursor() == 29 && f.model->redoDirty == 1 && f.selected[0] == 1 &&
                  refreshes.empty() && f.app->PhysicsResetPending() == 0,
              "Empty clipboard must leave selection, undo and refresh state unchanged");
    }
    return failures == 0 ? 0 : 1;
}
