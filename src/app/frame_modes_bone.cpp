// ===========================================================================
// Bone-drag stage of the frame driver (VA region 0x475A6E..0x477220)
// ===========================================================================
// When a drag is active and a model/bone is selected, modes 1..6 of the
// SECOND-stage mode field (app+844; the first-stage dispatch at 0x46B121
// reads app+836) are reinterpreted as bone rotation:
//   mode 1 : rotate around the camera-space X axis
//   mode 2 : around the camera-space Y axis
//   mode 3 : view-plane rotation - the delta of atan2(dx,dy) between the
//            drag origin->previous and origin->current mouse vectors,
//            wrapped to +-6.28 rad
//   modes 4/5/6 : around the selected bone's local axes (columns of the
//            bone matrix at +0xB4 in "direct" mode app+650676==1, else the
//            basis built by 0x40E670)
// The axis is transformed into the bone's frame by the rows of the bone
// matrix (+0xB4/+0xC4/+0xD4) and a delta quaternion (sin/cos of the scaled
// drag angle; 0x476D6D/0x476DA3 contain no additional half-angle multiply)
// is post-multiplied onto the bone's
// rotation quaternion at +0x14C via D3DXQuaternionMultiply, after setting
// the per-bone dirty flag (model+11672 array).
//
// Scale chain (doubles in .data, verified from the instruction stream):
//   base angle = mouse delta * 0.005   (dbl 0x52E9C0)
//   selector A (app+36==3)  -> *5.0    (dbl 0x52A270, coarse)
//   selector B (app+192==3) -> *0.1    (dbl 0x52BEA8, fine)
//   mode 3 wrap constants +-6.28       (dbl 0x52E8E0 / 0x52E8E8)
//   accessory/light drags   *0.01      (dbl 0x52E9C8)
//
// When app+658292 != 0 the same modes edit records instead of the bone:
//   accessory (app+650420==0): records at app+658300, stride 0xAC, fields
//     +0x40/+0x44/+0x48, "%3.2f" echo to edits 715/716/717 (0x2CB..0x2CD);
//   otherwise: records at app+658480, stride 0x8C, fields +0x30/+0x34/+0x38,
//     echo to edits 754/755/756 (0x2F2..0x2F4).
// The multi-selection propagation loop is ported below from
// 0x476E42..0x477257, including its selected-root filter.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

constexpr double kAngBase = 0.004999999888241291; // dbl 0x52E9C0
constexpr double kCoarse = 5.0;       // dbl 0x52A270
constexpr double kFine = 0.10000000149011612; // dbl 0x52BEA8 (0.1f image)
constexpr double kWrap = 6.28;        // dbl 0x52E8E8 (+) / 0x52E8E0 (-)
constexpr double kAccDrag = 0.01;     // dbl 0x52E9C8

float OriginalSinTimes(float angle, float factor) {
#if defined(_M_IX86)
    float sine;
    float result;
    __asm {
        fld angle
        fsin
        fstp sine
        fld sine
        fmul factor
        fstp result
    }
    return result;
#else
    return static_cast<float>(std::sin(angle) * factor);
#endif
}

float OriginalCos(float angle) {
#if defined(_M_IX86)
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
#else
    return static_cast<float>(std::cos(angle));
#endif
}

// SJIS bone-name needles (.rdata 0x52B7F0..0x52B81C); memcmp lengths in the
// original include the NUL byte.
const char kNeedleStrstr[] = "\x8E\x77";                    // 0x52B7F0
const char kNameR1[] = "\x89\x45\x8E\xE8\x8E\xF1";          // 0x52B7F4 (7)
const char kNameR2[] = "\x89\x45\x82\xD0\x82\xB6";          // 0x52B7FC (7)
const char kNameR3[] = "\x89\x45\x98\x72";                  // 0x52B804 (5)
const char kNameL1[] = "\x8D\xB6\x8E\xE8\x8E\xF1";          // 0x52B80C (7)
const char kNameL2[] = "\x8D\xB6\x82\xD0\x82\xB6";          // 0x52B814 (7)
const char kNameL3[] = "\x8D\xB6\x98\x72";                  // 0x52B81C (5)

float* Vec3Normalize(float v[3]) {
    return d3dx::Get().vec3Normalize(v, v);
}

void QuatMultiply(float out[4], const float a[4], const float b[4]) {
    d3dx::Get().quatMultiply(out, a, b);
}

unsigned char* CurrentModel(MMDApp* app) {
    return app->ModelSlot(app->state.slotIdx);
}

bool IsSelectedRoot(unsigned char* model, int index, bool excludeSelected) {
    const auto* selected = static_cast<const unsigned char*>(
        mikudancestudio::mdl::Mdl(model)->boneSelection);
    if (selected == nullptr || selected[index] == 0)
        return false;
    if (excludeSelected && index == mikudancestudio::mdl::Mdl(model)->selectedBone)
        return false;
    auto* bones = mikudancestudio::mdl::Bones(model);
    const mdl::BoneRecord& bone = bones[index];
    if (bone.type != mdl::BoneType::Move && bone.type != mdl::BoneType::Ik)
        return false;
    const int parent = bone.parent;
    return parent == -1 || selected[parent] == 0;
}

void TransformBoneVector(const mdl::BoneRecord& bone, const float matrix[16],
                         const float in[3], float out[3]) {
    out[0] = matrix[0] * in[0] + matrix[4] * in[1] + matrix[8] * in[2];
    out[1] = matrix[1] * in[0] + matrix[5] * in[1] + matrix[9] * in[2];
    out[2] = matrix[2] * in[0] + matrix[6] * in[1] + matrix[10] * in[2];
}

void BoneWorldPoint(const mdl::BoneRecord& bone, float out[3]) {
    const float rest[3] = {bone.position[0], bone.position[1],
                           bone.position[2]};
    TransformBoneVector(bone, bone.matInit, rest, out);
    out[0] += bone.matInit[12];
    out[1] += bone.matInit[13];
    out[2] += bone.matInit[14];
}

void ApplySelectedRootTranslation(unsigned char* model, const float delta[3]) {
    auto* bones = mikudancestudio::mdl::Bones(model);
    auto* dirty = mikudancestudio::mdl::Mdl(model)->bonePhysicsState;
    const int count = mikudancestudio::mdl::Mdl(model)->boneCount;
    if (bones == nullptr)
        return;
    for (int i = 0; i < count; ++i) {
        if (!IsSelectedRoot(model, i, false))
            continue;
        mdl::BoneRecord& bone = bones[i];
        float localDelta[3]{};
        TransformBoneVector(bone, bone.matWorld, delta, localDelta);
        bone.trans[0] += localDelta[0];
        bone.trans[1] += localDelta[1];
        bone.trans[2] += localDelta[2];
        if (dirty != nullptr)
            dirty[i] = 1;
    }
}

void EchoRecord(HWND dialog, int id, const char* format, float value) {
    char text[256]{};
    sprintf_s(text, sizeof(text), format, value);
    SetWindowTextA(GetDlgItem(dialog, id), text);
}

bool EditModeRecord(MMDApp* app, int mode) {
    HWND dialog = app->state.frameCopyDialog;
    if (dialog == nullptr)
        return false;
    const int dy = app->MouseY() - app->PreviousMouseY();

    if (mode >= 4 && mode <= 6) {
        const int axis = mode - 4;
        if (app->state.physicsEditorJointPage != 0) {
            // Physics-editor joint page: drag edits the joint rotation
            // (dialog edits 754..756).
            auto* joints =
                static_cast<mikudancestudio::mdl::JointRecord*>(
                    app->state.jointScratchArray);
            const int index = app->state.selectedJointIndex;
            if (joints != nullptr && index >= 0) {
                float& value = joints[index].rotation[axis];
                value = static_cast<float>(value -
                    static_cast<double>(dy) * kAccDrag);
                EchoRecord(dialog, 754 + axis, "%3.2f",
                    value / g_AnglePiTruncated * g_AngleDegreesScale);
            }
        } else {
            // Rigid-body page: drag edits the body rotation (715..717).
            auto* bodies =
                static_cast<mikudancestudio::mdl::RigidRecord*>(
                    app->state.rigidScratchArray);
            const int index = app->state.selectedRigidIndex;
            if (bodies != nullptr && index >= 0) {
                float& value = bodies[index].rotation[axis];
                value = static_cast<float>(value -
                    static_cast<double>(dy) * kAccDrag);
                EchoRecord(dialog, 715 + axis, "%3.2f",
                    value / g_AnglePiTruncated * g_AngleDegreesScale);
            }
        }
        return true;
    }

    if (mode >= 10 && mode <= 12) {
        const int axis = mode - 10;
        constexpr double kStep = 0.05000000074505806;
        if (app->state.physicsEditorJointPage != 0) {
            // Joint position drag (744..746).
            auto* joints =
                static_cast<mikudancestudio::mdl::JointRecord*>(
                    app->state.jointScratchArray);
            const int index = app->state.selectedJointIndex;
            if (joints != nullptr && index >= 0) {
                float& value = joints[index].position[axis];
                value = static_cast<float>(value - dy * kStep);
                EchoRecord(dialog, 744 + axis, "%5.4f", value);
            }
        } else {
            // Rigid body: shift-drag scales the shape size (709..711),
            // plain drag moves the body (712..714).
            auto* bodies =
                static_cast<mikudancestudio::mdl::RigidRecord*>(
                    app->state.rigidScratchArray);
            const int index = app->state.selectedRigidIndex;
            if (bodies != nullptr && index >= 0) {
                const bool scale = app->ShiftModifierActive();
                float& value = scale ? bodies[index].size[axis]
                                     : bodies[index].position[axis];
                value = static_cast<float>(value - dy * kStep);
                if (scale && value < 0.1f)
                    value = 0.1f;
                EchoRecord(dialog, (scale ? 709 : 712) + axis, "%3.2f", value);
            }
        }
        return true;
    }
    return true;
}

double PrecisionMultiplier(MMDApp* app, double coarse) {
    if (app->ShiftModifierActive())
        return coarse;
    if (app->CtrlModifierActive())
        return 0.1;
    return 1.0;
}

void ApplyScreenPlaneBoneMove(MMDApp* app, unsigned char* model, int mode) {
    D3DRenderer* render = app->Renderer();
    if (render == nullptr)
        return;
    const int viewWidth = render->screenWidth;    // wrapper+120036
    const int viewHeight = render->screenHeight;  // wrapper+120040
    const float overlayScale = render->viewScale;  // wrapper+120048
    const int outputWidth = app->RenderWidth();
    const int outputHeight = app->RenderHeight();
    if (viewWidth == 0 || viewHeight == 0 || outputHeight == 0 ||
        overlayScale == 0.0f)
        return;

    const double mouseScale = app->state.selectedClipW;
    float delta[3]{};
    if (mode == 7 || mode == 9) {
        double amount = static_cast<double>(
            app->MouseX() - app->PreviousMouseX());
        amount *= mouseScale / viewWidth;
        amount *= static_cast<double>(outputWidth) / outputHeight;
        if (app->CameraPerspective() != 0)
            amount *= -static_cast<double>(app->CameraDistance()) * 0.9;
        amount /= overlayScale;
        amount *= PrecisionMultiplier(app, 10.0);
        const double yaw = app->CameraRotation()[1];
        delta[0] = static_cast<float>(std::cos(yaw) * amount);
        delta[2] = static_cast<float>(std::sin(yaw) * amount);
        ApplySelectedRootTranslation(model, delta);
    }

    if (mode == 8 || mode == 9) {
        double amount = static_cast<double>(
            app->PreviousMouseY() - app->MouseY());
        amount *= mouseScale / viewHeight;
        if (app->CameraPerspective() != 0)
            amount *= -static_cast<double>(app->CameraDistance()) * 0.95;
        else
            amount *= 1.05;
        amount /= overlayScale;
        amount *= PrecisionMultiplier(app, 10.0);
        const double pitch = -static_cast<double>(app->CameraRotation()[0]);
        const double yaw = -static_cast<double>(app->CameraRotation()[1]);
        delta[0] = static_cast<float>(std::sin(yaw) * std::sin(pitch) * amount);
        delta[1] = static_cast<float>(std::cos(pitch) * amount);
        delta[2] = static_cast<float>(std::cos(yaw) * std::sin(pitch) * amount);
        ApplySelectedRootTranslation(model, delta);
    }
}

void ApplyLocalAxisBoneMove(MMDApp* app, unsigned char* model, int mode) {
    const int selectedIndex = mikudancestudio::mdl::Mdl(model)->selectedBone;
    if (selectedIndex < 0)
        return;
    auto* bones = mikudancestudio::mdl::Bones(model);
    if (bones == nullptr)
        return;
    mdl::BoneRecord& selected = bones[selectedIndex];

    double step = 0.05000000074505806;
    if (app->ShiftModifierActive())
        step = 0.5;
    else if (app->CtrlModifierActive())
        step = 0.004999999888241291;
    const int mouseDelta = mode == 12
        ? app->MouseY() - app->PreviousMouseY()
        : app->PreviousMouseY() - app->MouseY();

    float delta[3]{};
    delta[mode - 10] = static_cast<float>(mouseDelta * step);
    if (app->state.coordinateSystem != 1) {
        float basis[16]{};
        BoneLocalAxes(app, basis);
        const int axis = mode - 10;
        float local[3] = {
            basis[axis * 4 + 0] * delta[axis],
            basis[axis * 4 + 1] * delta[axis],
            basis[axis * 4 + 2] * delta[axis]};
        TransformBoneVector(selected, selected.matInit, local, delta);
    }
    ApplySelectedRootTranslation(model, delta);
}

}  // namespace

// VA 0x0040E670 - local-axis basis matrix of the selected bone.
// Fills a 16-float matrix: identity, unless the selected bone carries local
// axes (bone+520..528 non-zero and model physicsMode==2 -> orthonormalized
// basis from bone+520 and bone+532) or its name matches one of the arm
// prefixes (memcmp/strstr above) -> 2D basis from the bone->parent(+460)
// direction (bone+308/+312).
void BoneLocalAxes(MMDApp* app, float out[16]) {
    for (int i = 0; i < 16; ++i)
        out[i] = 0.0f;
    out[0] = out[5] = out[10] = out[15] = 1.0f;

    unsigned char* model = app->SelectedModel();
    if (model == nullptr || mikudancestudio::mdl::Mdl(model)->selectedBone < 0)
        return;
    auto* bones = mikudancestudio::mdl::Bones(model);
    const mdl::BoneRecord& bone =
        bones[mikudancestudio::mdl::Mdl(model)->selectedBone];

    const bool hasAxes = !(bone.localAxes[0] == 0.0f &&
                           bone.localAxes[1] == 0.0f &&
                           bone.localAxes[2] == 0.0f);
    if (hasAxes && mdl::Mdl(model)->physicsMode == 2) {
        float a[3] = {bone.localAxes[0], bone.localAxes[1],
                      bone.localAxes[2]};
        float b[3] = {bone.localAxes[3], bone.localAxes[4],
                      bone.localAxes[5]};
        Vec3Normalize(a);
        Vec3Normalize(b);
        float c[3] = {b[1] * a[2] - b[2] * a[1],
                      b[2] * a[0] - b[0] * a[2],
                      b[0] * a[1] - b[1] * a[0]};
        Vec3Normalize(c);
        c[0] = -c[0];
        c[1] = -c[1];
        c[2] = -c[2];
        float e[3] = {c[2] * a[1] - c[1] * a[2],
                      a[2] * c[0] - c[2] * a[0],
                      a[0] * c[1] - c[0] * a[1]};
        Vec3Normalize(e);
        out[0] = a[0];
        out[1] = a[1];
        out[2] = a[2];
        out[4] = c[0];
        out[5] = c[1];
        out[6] = c[2];
        out[8] = e[0];
        out[9] = e[1];
        out[10] = e[2];
    } else {
        const char* name = bone.name;
        if (std::memcmp(name, kNameR1, 7) == 0 ||
            std::memcmp(name, kNameR2, 7) == 0 ||
            std::memcmp(name, kNameR3, 5) == 0 ||
            std::memcmp(name, kNameL1, 7) == 0 ||
            std::memcmp(name, kNameL2, 7) == 0 ||
            std::memcmp(name, kNameL3, 5) == 0 ||
            std::strstr(name, kNeedleStrstr) != nullptr) {
            const mdl::BoneRecord& p = bones[bone.tailBone];
            const float dx = p.position[0] - bone.position[0];
            const float dy = p.position[1] - bone.position[1];
            const float len = std::sqrt(dx * dx + dy * dy);
            out[0] = dx / len;
            out[1] = dy / len;
            out[4] = -out[1];
            out[5] = out[0];
        }
    }
}

// VA region 0x475A6E..0x477220 - modes 1..6 of the bone-drag stage.
void BoneEditModes(MMDApp* app) {
    const int mode = static_cast<int>(app->state.interactionDragMode);
    if (mode <= 0 || mode > 12)
        return;

    if ((mode <= 6 || mode >= 10) && app->state.frameCopyDialog != nullptr) {
        EditModeRecord(app, mode);
        return;
    }

    unsigned char* model = CurrentModel(app);
    if (model == nullptr)
        return;
    const int sel = mikudancestudio::mdl::Mdl(model)->selectedBone;
    if (sel < 0)
        return;

    if (mode >= 7 && mode <= 9) {
        ApplyScreenPlaneBoneMove(app, model, mode);
        return;
    }
    if (mode >= 10) {
        ApplyLocalAxisBoneMove(app, model, mode);
        return;
    }
    auto* bones = mikudancestudio::mdl::Bones(model);
    mdl::BoneRecord& bone = bones[sel];

    const bool selA = app->state.shiftModifierState == 3;  // coarse selector
    const bool selB = app->state.ctrlModifierState == 3;   // fine selector
    const auto scaleOf = [&](float base) -> float {
        if (selA)
            return static_cast<float>(kCoarse * base);
        if (selB)
            return static_cast<float>(kFine * base);
        return base;
    };

    float ang = 0.0f;
    float axis[3] = {0.0f, 0.0f, 0.0f};
    const float* matInit = bone.matInit;
    const float* matWorld = bone.matWorld;

    if (mode == 1) {
        const float base = static_cast<float>(
            static_cast<double>(app->state.previousMouseX -
                                app->state.mouseX) * kAngBase);
        ang = scaleOf(base);
        const double aX = -static_cast<double>(app->CameraRotation()[0]);
        const double aY = -static_cast<double>(app->CameraRotation()[1]);
        const float ax = static_cast<float>(std::cos(aX));
        const float ay = static_cast<float>(std::sin(aY) * std::sin(aX));
        const float az = static_cast<float>(std::cos(aY) * std::sin(aX));
        axis[0] = matWorld[1] * ax + matWorld[0] * ay + matWorld[2] * az;
        axis[1] = matWorld[6] * az + matWorld[4] * ay + matWorld[5] * ax;
        axis[2] = matWorld[10] * az + matWorld[9] * ax + matWorld[8] * ay;
    } else if (mode == 2) {
        const float base = static_cast<float>(
            static_cast<double>(app->state.previousMouseY -
                                app->state.mouseY) * kAngBase);
        ang = scaleOf(base);
        const double aX = -static_cast<double>(app->CameraRotation()[0]);
        const double aY = -static_cast<double>(app->CameraRotation()[1]);
        const float ax = static_cast<float>(std::cos(aX));
        const float az = static_cast<float>(std::sin(aY));
        axis[0] = matWorld[0] * ax + matWorld[2] * az;
        axis[1] = matWorld[4] * ax + matWorld[6] * az;
        axis[2] = matWorld[8] * ax + matWorld[10] * az;
    } else if (mode == 3) {
        const double angCurRad = std::atan2(
            static_cast<double>(app->state.mouseX - app->state.dragOriginX),
            static_cast<double>(app->state.mouseY - app->state.dragOriginY));
        const double angPrevRad = std::atan2(
            static_cast<double>(app->state.previousMouseX -
                                app->state.dragOriginX),
            static_cast<double>(app->state.previousMouseY -
                                app->state.dragOriginY));
        float ang1 = static_cast<float>(angCurRad * g_MouseScaleA);
        float ang2 = static_cast<float>(angPrevRad * g_MouseScaleA);
        if (kWrap < static_cast<double>(ang1) - ang2)
            ang2 = static_cast<float>(ang2 + kWrap);
        if (static_cast<double>(ang1) - ang2 < -kWrap)
            ang1 = static_cast<float>(ang1 + kWrap);
        ang = scaleOf(ang1 - ang2);
        const double aX = -static_cast<double>(app->CameraRotation()[0]);
        const double aY = -static_cast<double>(app->CameraRotation()[1]);
        const float ax = -static_cast<float>(std::sin(aX));
        const float ay = static_cast<float>(std::sin(aY) * std::cos(aX));
        const float az = static_cast<float>(std::cos(aY) * std::cos(aX));
        axis[0] = matWorld[1] * ax + matWorld[0] * ay + matWorld[2] * az;
        axis[1] = matWorld[6] * az + matWorld[4] * ay + matWorld[5] * ax;
        axis[2] = matWorld[10] * az + matWorld[9] * ax + matWorld[8] * ay;
    } else {
        // modes 4/5/6 - local axes
        const float base = static_cast<float>(
            static_cast<double>(app->state.previousMouseY -
                                app->state.mouseY) * kAngBase);
        ang = scaleOf(base);
        // "direct" mode selector 0x9EDB4 (physicsEditorJointPage; the pre-promotion call
        // read offset 650076, an unpinned typo of the pinned 650676)
        if (app->state.physicsEditorJointPage == 1) {
            const std::size_t k = static_cast<std::size_t>(mode - 4);
            axis[0] = matWorld[k];
            axis[1] = matWorld[4 + k];
            axis[2] = matWorld[8 + k];
        } else {
            float basis[16];
            BoneLocalAxes(app, basis);
            const std::size_t r = static_cast<std::size_t>(mode - 4) * 4;
            const float mx = basis[r];
            const float my = basis[r + 1];
            const float mz = basis[r + 2];
            const float mid0 = matInit[8] * mz + matInit[0] * mx + matInit[4] * my;
            const float mid1 = matInit[9] * mz + matInit[1] * mx + matInit[5] * my;
            const float mid2 = matInit[10] * mz + matInit[6] * my + matInit[2] * mx;
            axis[0] = matWorld[1] * mid1 + matWorld[0] * mid0 + matWorld[2] * mid2;
            axis[1] = matWorld[6] * mid2 + matWorld[4] * mid0 + matWorld[5] * mid1;
            axis[2] = mid2 * matWorld[10] + matWorld[9] * mid1 + matWorld[8] * mid0;
            Vec3Normalize(axis);
        }
    }

    {
        // ---- bone edit ------------------------------------------------------
        if ((bone.flags & mdl::kBoneFlagFixedAxis) ==
                mdl::kBoneFlagFixedAxis &&
            (bone.type == mdl::BoneType::UnderIk ||
             bone.type == mdl::BoneType::FixedAxis)) {
            if (mdl::Mdl(model)->physicsMode == 2) {
                axis[0] = bone.axis[0];
                axis[1] = bone.axis[1];
                axis[2] = bone.axis[2];
            } else {
                const mdl::BoneRecord& p = bones[bone.tailBone];
                axis[0] = p.position[0] - bone.position[0];
                axis[1] = p.position[1] - bone.position[1];
                axis[2] = p.position[2] - bone.position[2];
                Vec3Normalize(axis);
            }
        }
        unsigned char* dirty =
            mikudancestudio::mdl::Mdl(model)->bonePhysicsState;
        if (dirty != nullptr)
            dirty[sel] = 1;

        // 0x476D6D/0x476DA3: sin/cos of the FULL drag angle (the 0.5
        // half-angle multiplication does NOT exist in the instruction
        // stream; var_14D4 already carries the scaled full angle)
        float dq[4] = {OriginalSinTimes(ang, axis[0]),
                       OriginalSinTimes(ang, axis[1]),
                       OriginalSinTimes(ang, axis[2]),
                       OriginalCos(ang)};
        float out[4];
        QuatMultiply(out, bone.rotQuat, dq);
        std::memcpy(bone.rotQuat, out, sizeof(out));

        // child-bone propagation loop (0x476E42..0x477257, lifted from
        // disassembly): every type-1/2 bone of the marked wave whose
        // parent is also marked rotates about the selected bone's world
        // position; kinematic pose slots +320/+332 are updated so the
        // transform chain picks the change up.
        {
            float piv[3]{};
            BoneWorldPoint(bone, piv);
            const float invq[4] = {-dq[0], -dq[1], -dq[2], dq[3]};
            const int n = mikudancestudio::mdl::Mdl(model)->boneCount;
            for (int i = 0; i < n; ++i) {
                mdl::BoneRecord& ch = bones[i];
                if (!IsSelectedRoot(model, i, true))
                    continue;
                if (dirty != nullptr)
                    dirty[i] = 1;                    // t4 mark (0x476FB2)
                float point[3]{};
                BoneWorldPoint(ch, point);
                const float off[4] = {
                    point[0] - piv[0], point[1] - piv[1], point[2] - piv[2],
                    0.0f};
                float r1[4], r2[4];
                QuatMultiply(r1, invq, off);         // 0x4770A6
                QuatMultiply(r2, r1, dq);            // 0x4770FB sandwich
                ch.trans[0] += r2[0] - off[0];
                ch.trans[1] += r2[1] - off[1];
                ch.trans[2] += r2[2] - off[2];
                float oq[4];
                QuatMultiply(oq, ch.rotQuat, dq);    // 0x4771EB
                std::memcpy(ch.rotQuat, oq, sizeof(oq));
            }
        }
        return;
    }
}

}  // namespace mikudancestudio
