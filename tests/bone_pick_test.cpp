#include "mikudancestudio/model.hpp"
#include "../src/app/bone_selection.hpp"
#include <cstdio>
#include <memory>

using namespace mikudancestudio;
int main() {
    auto model = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord bones[3]{};
    std::uint8_t selection[3]{};
    model->boneCount = 3;
    model->boneTable = bones;
    model->boneSelection = selection;
    for (int i = 0; i < 3; ++i) {
        bones[i].flags = mdl::kBoneFlagVisible;
        bones[i].screenX = 100 + i * 30;
        bones[i].screenY = 100;
    }
    bones[0].type = mdl::BoneType::Move;
    bones[1].type = mdl::BoneType::Ik;
    bones[2].type = mdl::BoneType::RotateMove;
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "%s\n", message);
        }
    };
    for (int mode = 0; mode <= 3; ++mode) {
        for (int i = 0; i < 3; ++i) {
            model->selectedBone = -1;
            check(PickBoneAtPoint(*model, 100 + i * 30, 100, mode, false) &&
                      model->selectedBone == i && selection[i],
                  "A non-rigid move/IK/joint marker must remain pickable");
        }
    }
    bones[2].hasRigidBody = 1;
    for (int mode : {1, 2}) {
        model->selectedBone = -1;
        check(!PickBoneAtPoint(*model, 160, 100, mode, false),
              "Physics-driven rigid joint must not be picked");
    }
    bones[2].physicsDisabled = 1;
    check(PickBoneAtPoint(*model, 160, 100, 2, false),
          "Per-bone physics-off permits picking in mode 2");
    check(!PickBoneAtPoint(*model, 160, 100, 1, false), "Mode 1 still excludes rigid bones");
    bones[0].flags = 0;
    check(!PickBoneAtPoint(*model, 100, 100, 0, false), "Invisible bone was picked");
    bones[0].flags = mdl::kBoneFlagVisible;
    for (auto type : {mdl::BoneType::InertTip, mdl::BoneType::CoRotate}) {
        bones[0].type = type;
        check(!PickBoneAtPoint(*model, 100, 100, 0, false), "Non-pickable bone type was picked");
    }
    bones[0].type = mdl::BoneType::FixedAxis;
    check(PickBoneAtPoint(*model, 107, 100, 0, false),
          "Fixed-axis marker within radius was missed");
    check(!PickBoneAtPoint(*model, 108, 100, 0, false), "Eight-pixel boundary must be excluded");
    bones[1].screenX = 100;
    model->selectedBone = -1;
    for (int expected : {0, 1, 0}) {
        check(PickBoneAtPoint(*model, 100, 100, 0, false) && model->selectedBone == expected,
              "Overlapping markers must cycle and wrap");
    }
    selection[0] = selection[1] = 0;
    check(PickBoneAtPoint(*model, 100, 100, 0, true) && selection[0] && selection[1],
          "Shift-click must toggle all hit markers on");
    check(PickBoneAtPoint(*model, 100, 100, 0, true) && !selection[0] && !selection[1] &&
              model->selectedBone == -1,
          "Shift-click must toggle hit markers off");
    // Box selection uses strict rectangle edges, includes hidden/rigid
    // editable types, and does not choose an active bone until release.
    bones[0].screenX = 100;
    bones[1].screenX = 130;
    bones[2].screenX = 160;
    bones[0].flags = 0;
    bones[0].type = mdl::BoneType::Effector;
    bones[1].type = mdl::BoneType::InertTip;
    bones[2].type = mdl::BoneType::FixedAxis;
    model->selectedBone = 1;
    SelectBonesInScreenRect(*model, 99, 99, 161, 101, false);
    check(selection[0] && !selection[1] && selection[2] && model->selectedBone == 1,
          "Box selection must use its own type filter and defer active-bone choice");
    FinishBoneBoxSelection(*model, false);
    check(model->selectedBone == 0, "Box release prefers selected bone zero for a multi-selection");
    SelectBonesInScreenRect(*model, 100, 99, 161, 101, false);
    FinishBoneBoxSelection(*model, false);
    check(!selection[0] && selection[2] && model->selectedBone == 2,
          "Strict box boundary excludes bone zero; release selects the sole survivor");
    selection[0] = 1;
    SelectBonesInScreenRect(*model, 159, 99, 161, 101, true);
    FinishBoneBoxSelection(*model, true);
    check(selection[0] && selection[2] && model->selectedBone == 2,
          "Shift box preserves outside selections and the active selected bone");
    SelectBonesInScreenRect(*model, 200, 200, 210, 210, false);
    FinishBoneBoxSelection(*model, false);
    check(!selection[0] && !selection[1] && !selection[2] && model->selectedBone == -1,
          "Empty replacement box clears the active selection on release");
    bones[0].flags = mdl::kBoneFlagVisible;
    bones[0].type = static_cast<mdl::BoneType>(0xFF);
    check(PickBoneAtPoint(*model, 100, 100, 0, false),
          "Original signed-byte type comparison must be retained");
    return failures ? 1 : 0;
}
